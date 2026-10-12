#include "Transport.hpp"
#include "HttpErrors.hpp"
#include <curl/curl.h>
#include <mbedtls/ssl.h>
#include <mbedtls/x509.h>
#include <algorithm>
#include <atomic>
#include <charconv>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <exception>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <thread>

namespace HttpNetwork {
namespace {

using Clock = std::chrono::steady_clock;

class CurlTransport final : public ITransport {
public:
    ~CurlTransport() override {
        Abort();
        if (worker.joinable()) worker.join();
    }

    int Send(const Settings& value, const std::string& url, const std::string& method, const void* data, std::size_t size) override {
        static const auto initialized = curl_global_init(CURL_GLOBAL_DEFAULT);
        if (initialized != CURLE_OK) throw std::runtime_error("curl_global_init failed");
        settings = value;
        std::string payload;
        if (size) payload.assign(static_cast<const char*>(data), size);
        worker = std::thread([this, url, method, payload = std::move(payload)] {
            try { run(url, method, payload); }
            catch (...) {
                std::lock_guard lock(mutex);
                failure = std::current_exception();
                done = true;
                wake.notify_all();
            }
        });
        std::unique_lock lock(mutex);
        wake.wait(lock, [&] { return ready || done || aborted.load(); });
        if (failure) std::rethrow_exception(failure);
        return aborted.load() ? ERROR_HTTP_ABORTED : result;
    }

    int Read(void* data, std::size_t size) override {
        std::unique_lock lock(mutex);
        wake.wait(lock, [&] { return !body.empty() || done || aborted.load(); });
        if (failure) std::rethrow_exception(failure);
        if (aborted.load()) return ERROR_HTTP_ABORTED;
        if (result != 0) return result;
        const auto count = std::min(size, body.size());
        for (std::size_t i = 0; i < count; ++i) {
            static_cast<char*>(data)[i] = body.front();
            body.pop_front();
        }
        wake.notify_all();
        return static_cast<int>(count);
    }

    void Abort() override {
        aborted.store(true);
        wake.notify_all();
    }

    int Error() const override { return nativeError.load(); }
    std::uint32_t SslError() const override { return sslError.load(); }

private:
    template<typename T>
    static void option(CURL* easy, CURLoption name, T value) {
        if (curl_easy_setopt(easy, name, value) != CURLE_OK) throw std::runtime_error("curl_easy_setopt failed");
    }

    static int verify(void* context, mbedtls_x509_crt*, int, std::uint32_t* flags) {
        const auto bits = static_cast<CurlTransport*>(context)->settings.sslFlags;
        if (!(bits & 1u)) *flags = 0;
        else {
            if (!(bits & 4u)) *flags &= ~MBEDTLS_X509_BADCERT_CN_MISMATCH;
            if (!(bits & 0x18u)) *flags &= ~(MBEDTLS_X509_BADCERT_EXPIRED | MBEDTLS_X509_BADCERT_FUTURE);
            if (!(bits & 0x20u)) *flags &= ~MBEDTLS_X509_BADCERT_NOT_TRUSTED;
        }
        static_cast<CurlTransport*>(context)->sslError.fetch_or(*flags);
        return 0;
    }

    static CURLcode tlsContext(CURL*, void* config, void* context) {
        mbedtls_ssl_conf_verify(static_cast<mbedtls_ssl_config*>(config), verify, context);
        return CURLE_OK;
    }

    static std::size_t headerCallback(char* data, std::size_t size, std::size_t count, void* context) {
        auto& self = *static_cast<CurlTransport*>(context);
        try { return self.header(std::string_view(data, size * count)); }
        catch (...) { self.callbackFailure = std::current_exception(); return 0; }
    }

    static std::size_t bodyCallback(char* data, std::size_t size, std::size_t count, void* context) {
        auto& self = *static_cast<CurlTransport*>(context);
        try { return self.receive(std::string_view(data, size * count)); }
        catch (...) { self.callbackFailure = std::current_exception(); return 0; }
    }

    std::size_t header(std::string_view line) {
        std::lock_guard lock(mutex);
        progress = Clock::now();
        if (ready) return line.size();
        if (line.starts_with("HTTP/")) {
            headers.clear();
            hasLength = false;
            length = 0;
            redirectLocation = false;
            const auto space = line.find(' ');
            if (space == std::string_view::npos) throw std::runtime_error("HTTP response has no status");
            const auto parsed = std::from_chars(line.data() + space + 1, line.data() + line.size(), status);
            if (parsed.ec != std::errc{}) throw std::runtime_error("HTTP response has invalid status");
        }
        if (headers.size() + line.size() > settings.headerLimit) {
            result = ERROR_HTTP_HEADER_TOO_LARGE;
            return 0;
        }
        headers.append(line);
        std::string name(line.substr(0, line.find(':')));
        std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c; });
        if (name == "location") redirectLocation = true;
        if (name == "content-length") {
            auto text = line.substr(line.find(':') + 1);
            while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) text.remove_prefix(1);
            const auto parsed = std::from_chars(text.data(), text.data() + text.size(), length);
            hasLength = parsed.ec == std::errc{};
        }
        if (line == "\r\n" && status >= 200 && !(settings.redirect && redirectLocation && status >= 300 && status < 400)) {
            ready = true;
            wake.notify_all();
        }
        return line.size();
    }

    std::size_t receive(std::string_view bytes) {
        if (settings.redirect && redirectLocation && status >= 300 && status < 400) return bytes.size();
        std::unique_lock lock(mutex);
        wake.wait(lock, [&] { return body.size() + bytes.size() <= 65536 || aborted.load(); });
        if (aborted.load()) return 0;
        body.insert(body.end(), bytes.begin(), bytes.end());
        progress = Clock::now();
        wake.notify_all();
        return bytes.size();
    }

    void run(const std::string& url, const std::string& method, const std::string& payload) {
        auto* easy = curl_easy_init();
        auto* multi = curl_multi_init();
        if (!easy || !multi) {
            if (easy) curl_easy_cleanup(easy);
            if (multi) curl_multi_cleanup(multi);
            throw std::runtime_error("curl handle allocation failed");
        }
        struct Cleanup {
            CURL* easy;
            CURLM* multi;
            curl_slist* list = nullptr;
            ~Cleanup() { curl_multi_remove_handle(multi, easy); curl_multi_cleanup(multi); curl_easy_cleanup(easy); curl_slist_free_all(list); }
        } cleanup{easy, multi};
        for (const auto& [name, value] : settings.headers) {
            auto* next = curl_slist_append(cleanup.list, (name + ": " + value).c_str());
            if (!next) throw std::bad_alloc();
            cleanup.list = next;
        }
        const bool secure = url.size() >= 6 && std::equal(url.begin(), url.begin() + 6, "https:", [](unsigned char value, char expected) {
            return (value >= 'A' && value <= 'Z' ? value + ('a' - 'A') : value) == expected;
        });
        option(easy, CURLOPT_URL, url.c_str());
        option(easy, CURLOPT_PROTOCOLS_STR, "http,https");
        option(easy, CURLOPT_REDIR_PROTOCOLS_STR, secure ? "https" : "http,https");
        option(easy, CURLOPT_PROXY, "");
        option(easy, CURLOPT_NOSIGNAL, 1L);
        option(easy, CURLOPT_USERAGENT, settings.agent.c_str());
        option(easy, CURLOPT_HTTP_VERSION, settings.version == 1 ? CURL_HTTP_VERSION_1_0 : CURL_HTTP_VERSION_1_1);
        option(easy, CURLOPT_CUSTOMREQUEST, method.c_str());
        if (method == "HEAD") option(easy, CURLOPT_NOBODY, 1L);
        if (!payload.empty() || method == "POST" || method == "PUT") {
            option(easy, CURLOPT_POSTFIELDS, payload.data());
            option(easy, CURLOPT_POSTFIELDSIZE_LARGE, static_cast<curl_off_t>(payload.size()));
        }
        option(easy, CURLOPT_HTTPHEADER, cleanup.list);
        option(easy, CURLOPT_FOLLOWLOCATION, settings.redirect ? CURLFOLLOW_OBEYCODE : 0L);
        option(easy, CURLOPT_MAXREDIRS, 20L);
        option(easy, CURLOPT_FORBID_REUSE, settings.keepAlive ? 0L : 1L);
        option(easy, CURLOPT_ACCEPT_ENCODING, settings.gzip ? "gzip" : nullptr);
        option(easy, CURLOPT_SSL_VERIFYPEER, 1L);
        option(easy, CURLOPT_SSL_VERIFYHOST, (settings.sslFlags & 5u) == 5u ? 2L : 0L);
        option(easy, CURLOPT_SSL_CTX_FUNCTION, tlsContext);
        option(easy, CURLOPT_SSL_CTX_DATA, this);
        option(easy, CURLOPT_HEADERFUNCTION, headerCallback);
        option(easy, CURLOPT_HEADERDATA, this);
        option(easy, CURLOPT_WRITEFUNCTION, bodyCallback);
        option(easy, CURLOPT_WRITEDATA, this);
        if (curl_multi_add_handle(multi, easy) != CURLM_OK) throw std::runtime_error("curl_multi_add_handle failed");
        progress = Clock::now();
        auto phase = 0;
        curl_off_t uploaded = 0;
        int running = 1;
        CURLcode code = CURLE_OK;
        while (running && !aborted.load()) {
            if (curl_multi_perform(multi, &running) != CURLM_OK) throw std::runtime_error("curl_multi_perform failed");
            if (callbackFailure) std::rethrow_exception(callbackFailure);
            curl_off_t resolved = 0;
            curl_off_t connected = 0;
            curl_easy_getinfo(easy, CURLINFO_NAMELOOKUP_TIME_T, &resolved);
            curl_easy_getinfo(easy, secure ? CURLINFO_APPCONNECT_TIME_T : CURLINFO_CONNECT_TIME_T, &connected);
            const auto nextPhase = connected > 0 ? 2 : resolved > 0 ? 1 : 0;
            if (nextPhase != phase) { phase = nextPhase; progress = Clock::now(); }
            curl_off_t sent = 0;
            curl_easy_getinfo(easy, CURLINFO_SIZE_UPLOAD_T, &sent);
            if (sent != uploaded) { uploaded = sent; progress = Clock::now(); }
            const auto timeout = phase == 0 ? settings.resolveTimeout : phase == 1 ? settings.connectTimeout : static_cast<std::uint64_t>(uploaded) < payload.size() ? settings.sendTimeout : settings.receiveTimeout;
            if (running && timeout != 0 && Clock::now() - progress >= std::chrono::microseconds(timeout)) {
                code = CURLE_OPERATION_TIMEDOUT;
                break;
            }
            if (running && curl_multi_poll(multi, nullptr, 0, 25, nullptr) != CURLM_OK) throw std::runtime_error("curl_multi_poll failed");
        }
        int remaining = 0;
        while (auto* message = curl_multi_info_read(multi, &remaining)) if (message->msg == CURLMSG_DONE) code = message->data.result;
        nativeError.store(static_cast<int>(code));
        std::lock_guard lock(mutex);
        if (aborted.load()) result = ERROR_HTTP_ABORTED;
        else if (result == 0 && code != CURLE_OK) {
            if (code == CURLE_OPERATION_TIMEDOUT) result = ERROR_HTTP_TIMEOUT;
            else if (code == CURLE_PEER_FAILED_VERIFICATION || code == CURLE_SSL_CONNECT_ERROR || code == CURLE_SSL_CACERT_BADFILE) {
                result = ERROR_HTTP_SSL;
                if (sslError.load() == 0) sslError.store(static_cast<std::uint32_t>(code));
            } else result = ERROR_NETWORK;
        }
        done = true;
        wake.notify_all();
    }

    Settings settings;
    std::mutex mutex;
    std::condition_variable wake;
    std::deque<char> body;
    std::thread worker;
    std::atomic<bool> aborted{false};
    std::atomic<int> nativeError{0};
    std::atomic<std::uint32_t> sslError{0};
    std::exception_ptr failure;
    std::exception_ptr callbackFailure;
    Clock::time_point progress;
    int result = 0;
    bool ready = false;
    bool done = false;
    bool redirectLocation = false;
};

}

std::shared_ptr<ITransport> CreateTransport_nid_no_patch() { return std::make_shared<CurlTransport>(); }

}
