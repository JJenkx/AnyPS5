#include "AudioPropagation.hpp"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace {

using namespace AudioPropagation;

constexpr std::uint64_t SystemMagic = [] {
    constexpr char text[] = "APSYSTEM";
    std::uint64_t value = 0;
    for (int index = 7; index >= 0; --index) value = (value << 8u) | static_cast<unsigned char>(text[index]);
    return value;
}();

struct SystemState {
    std::uint64_t magic;
    std::uint64_t reserved;
};

enum class Kind { Room, Portal, Source, Material, Path };

struct Object {
    Kind kind;
    AudioPropagationHandle system;
};

struct PathState {
    AudioPath data{};
    float gain = 0.0f;
};

struct Registry {
    std::mutex mutex;
    std::unordered_set<AudioPropagationHandle> systems;
    std::unordered_map<AudioPropagationHandle, Object> objects;
    std::unordered_map<AudioPropagationHandle, Object> orphans;
    std::unordered_map<AudioPropagationHandle, std::array<AudioPropagationHandle, MaxPaths>> sourcePaths;
    std::unordered_map<AudioPropagationHandle, PathState> paths;
    AudioPropagationHandle next = 1;
};

Registry& registry() {
    static Registry state;
    return state;
}

void require(bool condition, const char* function) {
    if (!condition) throw std::invalid_argument(std::string(function) + ": invalid argument");
}

void requireDescriptor(const AudioPropagationStructDescriptor* desc, std::uint32_t id, std::size_t size, const char* function) {
    require(desc != nullptr && desc->id == id && desc->size == size, function);
}

void requireAttributes(const Attribute* attributes, std::uint32_t count, const char* function) {
    require(count == 0 || attributes != nullptr, function);
    for (std::uint32_t index = 0; index < count; ++index) {
        require(attributes[index].value != nullptr && attributes[index].valueSize != 0, function);
    }
}

bool liveSystem(Registry& state, AudioPropagationHandle system) {
    if (!state.systems.contains(system)) return false;
    return reinterpret_cast<const SystemState*>(system)->magic == SystemMagic;
}

void requireSystem(Registry& state, AudioPropagationHandle system, const char* function) {
    require(liveSystem(state, system), function);
}

const Object& requireObject(Registry& state, AudioPropagationHandle handle, Kind kind, const char* function) {
    const auto found = state.objects.find(handle);
    require(found != state.objects.end() && found->second.kind == kind && liveSystem(state, found->second.system), function);
    return found->second;
}

AudioPropagationHandle createObjectLocked(Registry& state, AudioPropagationHandle system, Kind kind, AudioPropagationHandle* out, const char* function) {
    require(out != nullptr, function);
    requireSystem(state, system, function);
    const auto handle = state.next++;
    state.objects.emplace(handle, Object{kind, system});
    *out = handle;
    return handle;
}

AudioPropagationHandle createObject(AudioPropagationHandle system, Kind kind, AudioPropagationHandle* out, const char* function) {
    auto& state = registry();
    std::lock_guard lock(state.mutex);
    return createObjectLocked(state, system, kind, out, function);
}

void destroyObject(AudioPropagationHandle system, AudioPropagationHandle handle, Kind kind, const char* function) {
    auto& state = registry();
    std::lock_guard lock(state.mutex);
    const auto orphan = state.orphans.find(handle);
    if (orphan != state.orphans.end() && orphan->second.kind == kind && orphan->second.system == system) {
        state.orphans.erase(orphan);
        return;
    }
    require(requireObject(state, handle, kind, function).system == system, function);
    if (kind == Kind::Source) {
        for (const auto path : state.sourcePaths.at(handle)) {
            state.paths.erase(path);
            state.objects.erase(path);
        }
        state.sourcePaths.erase(handle);
    }
    state.objects.erase(handle);
}

void requireRaysOutput(std::uint32_t* count, const char* function) {
    require(count != nullptr, function);
    *count = 0;
}

void requireNoPropagation(bool none, const char* function) {
    if (!none) throw std::runtime_error(std::string(function) + ": rays or audio paths are outside the no-propagation model");
}

}

extern "C" {

std::int32_t APS5_VABI sceAudioPropagationSystemQueryMemory(const void* options, AudioPropagationSystemMemory* outMemory) {
    require(options != nullptr, __func__);
    requireDescriptor(outMemory != nullptr ? &outMemory->desc : nullptr, SystemMemoryId, SystemMemorySize, __func__);
    outMemory->size_cpu_mem = sizeof(SystemState);
    outMemory->size_gpu_mem = 0;
    return 0;
}

std::int32_t APS5_VABI sceAudioPropagationSystemCreate(const void* options, const AudioPropagationSystemMemory* memory, AudioPropagationHandle* outSystem) {
    require(options != nullptr && outSystem != nullptr, __func__);
    requireDescriptor(memory != nullptr ? &memory->desc : nullptr, SystemMemoryId, SystemMemorySize, __func__);
    const auto address = reinterpret_cast<std::uintptr_t>(memory->p_cpu_mem);
    require(address != 0 && address % alignof(SystemState) == 0 && memory->size_cpu_mem >= sizeof(SystemState) && memory->size_gpu_mem == 0, __func__);
    auto& state = registry();
    std::lock_guard lock(state.mutex);
    require(!state.systems.contains(address), __func__);
    *static_cast<SystemState*>(memory->p_cpu_mem) = SystemState{SystemMagic, 0};
    state.systems.insert(address);
    *outSystem = address;
    return 0;
}

std::int32_t APS5_VABI sceAudioPropagationSystemDestroy(AudioPropagationHandle system) {
    auto& state = registry();
    std::lock_guard lock(state.mutex);
    requireSystem(state, system, __func__);
    std::erase_if(state.paths, [&](const auto& entry) { return state.objects.at(entry.first).system == system; });
    std::erase_if(state.sourcePaths, [&](const auto& entry) { return state.objects.at(entry.first).system == system; });
    std::erase_if(state.objects, [&](const auto& entry) {
        if (entry.second.system != system) return false;
        if (entry.second.kind != Kind::Path) state.orphans.insert(entry);
        return true;
    });
    reinterpret_cast<SystemState*>(system)->magic = 0;
    state.systems.erase(system);
    return 0;
}

std::int32_t APS5_VABI sceAudioPropagationSystemSetAttributes(AudioPropagationHandle system, const Attribute* attributes, std::uint32_t count) {
    auto& state = registry();
    std::lock_guard lock(state.mutex);
    requireSystem(state, system, __func__);
    requireAttributes(attributes, count, __func__);
    return 0;
}

std::int32_t APS5_VABI sceAudioPropagationSystemRegisterMaterial(AudioPropagationHandle system, const AudioPropagationStructDescriptor* material, AudioPropagationHandle* outMaterial) {
    requireDescriptor(material, MaterialId, MaterialSize, __func__);
    createObject(system, Kind::Material, outMaterial, __func__);
    return 0;
}

std::int32_t APS5_VABI sceAudioPropagationSystemUnregisterMaterial(AudioPropagationHandle material) {
    auto& state = registry();
    std::lock_guard lock(state.mutex);
    const auto orphan = state.orphans.find(material);
    if (orphan != state.orphans.end() && orphan->second.kind == Kind::Material) {
        state.orphans.erase(orphan);
        return 0;
    }
    requireObject(state, material, Kind::Material, __func__);
    state.objects.erase(material);
    return 0;
}

std::int32_t APS5_VABI sceAudioPropagationSystemGetRays(AudioPropagationHandle system, void* rays, std::uint32_t* count) {
    auto& state = registry();
    std::lock_guard lock(state.mutex);
    requireSystem(state, system, __func__);
    require(rays != nullptr, __func__);
    requireRaysOutput(count, __func__);
    return 0;
}

std::int32_t APS5_VABI sceAudioPropagationSystemSetRays(AudioPropagationHandle system, const void* rays, std::uint32_t count) {
    auto& state = registry();
    std::lock_guard lock(state.mutex);
    requireSystem(state, system, __func__);
    require(count == 0 || rays != nullptr, __func__);
    requireNoPropagation(count == 0, __func__);
    return 0;
}

std::int32_t APS5_VABI sceAudioPropagationRoomCreate(AudioPropagationHandle system, AudioPropagationHandle* outRoom) {
    createObject(system, Kind::Room, outRoom, __func__);
    return 0;
}

std::int32_t APS5_VABI sceAudioPropagationRoomDestroy(AudioPropagationHandle system, AudioPropagationHandle room) {
    destroyObject(system, room, Kind::Room, __func__);
    return 0;
}

std::int32_t APS5_VABI sceAudioPropagationPortalCreate(AudioPropagationHandle system, const PortalParams* params, AudioPropagationHandle* outPortal) {
    requireDescriptor(params != nullptr ? &params->desc : nullptr, PortalParamsId, PortalParamsSize, __func__);
    auto& state = registry();
    std::lock_guard lock(state.mutex);
    for (const auto room : params->rooms) require(requireObject(state, room, Kind::Room, __func__).system == system, __func__);
    createObjectLocked(state, system, Kind::Portal, outPortal, __func__);
    return 0;
}

std::int32_t APS5_VABI sceAudioPropagationPortalDestroy(AudioPropagationHandle system, AudioPropagationHandle portal) {
    destroyObject(system, portal, Kind::Portal, __func__);
    return 0;
}

std::int32_t APS5_VABI sceAudioPropagationPortalSetAttributes(AudioPropagationHandle portal, const Attribute* attributes, std::uint32_t count) {
    auto& state = registry();
    std::lock_guard lock(state.mutex);
    requireObject(state, portal, Kind::Portal, __func__);
    requireAttributes(attributes, count, __func__);
    return 0;
}

std::int32_t APS5_VABI sceAudioPropagationSourceCreate(AudioPropagationHandle system, AudioPropagationHandle* outSource) {
    require(outSource != nullptr, __func__);
    auto& state = registry();
    std::lock_guard lock(state.mutex);
    AudioPropagationHandle source = 0;
    createObjectLocked(state, system, Kind::Source, &source, __func__);
    std::array<AudioPropagationHandle, MaxPaths> handles{};
    try {
        for (auto& handle : handles) {
            createObjectLocked(state, system, Kind::Path, &handle, __func__);
            PathState path{};
            path.data.desc = {PathId, sizeof(AudioPath)};
            path.data.flags = PathInvalid;
            state.paths.emplace(handle, path);
        }
        state.sourcePaths.emplace(source, handles);
    } catch (...) {
        for (const auto handle : handles) {
            state.paths.erase(handle);
            state.objects.erase(handle);
        }
        state.objects.erase(source);
        throw;
    }
    *outSource = source;
    return 0;
}

std::int32_t APS5_VABI sceAudioPropagationSourceDestroy(AudioPropagationHandle system, AudioPropagationHandle source) {
    destroyObject(system, source, Kind::Source, __func__);
    return 0;
}

std::int32_t APS5_VABI sceAudioPropagationSourceSetAttributes(AudioPropagationHandle source, const Attribute* attributes, std::uint32_t count) {
    auto& state = registry();
    std::lock_guard lock(state.mutex);
    requireObject(state, source, Kind::Source, __func__);
    requireAttributes(attributes, count, __func__);
    return 0;
}

std::int32_t APS5_VABI sceAudioPropagationSourceGetAudioPathCount(AudioPropagationHandle source, std::uint32_t* count) {
    auto& state = registry();
    std::lock_guard lock(state.mutex);
    requireObject(state, source, Kind::Source, __func__);
    require(count != nullptr, __func__);
    *count = MaxPaths;
    return 0;
}

std::int32_t APS5_VABI sceAudioPropagationSourceGetAudioPath(AudioPropagationHandle source, std::uint32_t index, AudioPropagationHandle* outPath) {
    auto& state = registry();
    std::lock_guard lock(state.mutex);
    requireObject(state, source, Kind::Source, __func__);
    require(outPath != nullptr && index < MaxPaths, __func__);
    *outPath = state.sourcePaths.at(source)[index];
    return 0;
}

std::int32_t APS5_VABI sceAudioPropagationPathGetNumPoints(AudioPropagationHandle path, std::uint32_t* count) {
    auto& state = registry();
    std::lock_guard lock(state.mutex);
    requireObject(state, path, Kind::Path, __func__);
    require(count != nullptr, __func__);
    *count = state.paths.at(path).data.numPoints;
    return 0;
}

std::int32_t APS5_VABI sceAudioPropagationSourceGetRays(AudioPropagationHandle source, void* rays, std::uint32_t* count) {
    auto& state = registry();
    std::lock_guard lock(state.mutex);
    requireObject(state, source, Kind::Source, __func__);
    require(rays != nullptr, __func__);
    requireRaysOutput(count, __func__);
    return 0;
}

std::int32_t APS5_VABI sceAudioPropagationSourceCalculateAudioPaths(AudioPropagationHandle source, const void* rays, std::uint32_t rayCount, std::uint32_t flags, void* paths, std::uint32_t pathCount) {
    auto& state = registry();
    std::lock_guard lock(state.mutex);
    requireObject(state, source, Kind::Source, __func__);
    static_cast<void>(rays);
    static_cast<void>(flags);
    static_cast<void>(paths);
    requireNoPropagation(rayCount == 0 && pathCount == 0, __func__);
    return 0;
}

std::int32_t APS5_VABI sceAudioPropagationSourceSetAudioPaths(AudioPropagationHandle source, const void* entries, std::uint32_t count) {
    auto& state = registry();
    std::lock_guard lock(state.mutex);
    requireObject(state, source, Kind::Source, __func__);
    static_cast<void>(entries);
    requireNoPropagation(count == 0, __func__);
    return 0;
}

std::int32_t APS5_VABI sceAudioPropagationSourceSetAudioPath(AudioPropagationHandle path, const void* data, float gain) {
    auto& state = registry();
    std::lock_guard lock(state.mutex);
    const auto system = requireObject(state, path, Kind::Path, __func__).system;
    requireDescriptor(data != nullptr ? &static_cast<const AudioPath*>(data)->desc : nullptr, PathId, sizeof(AudioPath), __func__);
    const auto replacement = *static_cast<const AudioPath*>(data);
    require(std::isfinite(gain) && gain >= 0.0f && replacement.numPoints <= MaxPathPoints && (replacement.flags & ~(PathInvalid | PathPortal)) == 0 && replacement.pad == 0, __func__);
    for (std::uint32_t index = 0; index < replacement.numPoints; ++index) {
        const auto& point = replacement.points[index];
        requireDescriptor(&point.desc, PathPointId, sizeof(PathPoint), __func__);
        require(std::isfinite(point.gain) && point.gain >= 0.0f && std::isfinite(point.position.x) && std::isfinite(point.position.y) && std::isfinite(point.position.z) && point.position.pad == 0, __func__);
        if (point.material != 0) require(requireObject(state, point.material, Kind::Material, __func__).system == system, __func__);
    }
    state.paths.at(path) = PathState{replacement, gain};
    return 0;
}

std::int32_t APS5_VABI sceAudioPropagationSourceRender(AudioPropagationHandle system, const RenderInfo* infos, std::uint32_t count) {
    require(infos != nullptr && count != 0, __func__);
    auto& state = registry();
    std::lock_guard lock(state.mutex);
    requireSystem(state, system, __func__);
    for (std::uint32_t index = 0; index < count; ++index) {
        const auto& info = infos[index];
        requireDescriptor(&info.desc, RenderInfoId, RenderInfoSize, __func__);
        require(requireObject(state, info.source, Kind::Source, __func__).system == system, __func__);
        require(info.output != nullptr && info.outputSize != 0, __func__);
        if (info.format != RenderFormat) throw std::runtime_error(std::string(__func__) + ": render format " + std::to_string(info.format) + " is unknown");
        for (const auto handle : state.sourcePaths.at(info.source)) {
            const auto& path = state.paths.at(handle);
            if (path.gain > 0.0f && path.data.numPoints != 0 && (path.data.flags & PathInvalid) == 0) {
                throw std::runtime_error(std::string(__func__) + ": nonempty audio path rendering is not implemented");
            }
        }
    }
    for (std::uint32_t index = 0; index < count; ++index) std::memset(infos[index].output, 0, infos[index].outputSize);
    return 0;
}

}
