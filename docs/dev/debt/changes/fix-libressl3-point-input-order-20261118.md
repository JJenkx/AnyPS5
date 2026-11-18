# libSceLibreSsl3

## change [o2i_ECPublicKey]

- [o2i_ECPublicKey](../../../core/libs/prx/libSceLibreSsl3/src/EcAsn1.cpp) (libSceLibreSsl3) - console behavior is unmeasured. A failed decode queues only `ERR_R_EC_LIB`, omitting the lower decoder reason; compressed points on primes not 3 mod 4 throw. Like [LibreSSL 4.0.0](https://github.com/libressl/openbsd/blob/ce488b78e0bd994780569df718596b9776ff681a/src/lib/libcrypto/ec/ec_asn1.c), a missing key/group queues `ERR_R_PASSED_NULL_PARAMETER`, a created point stays on ordinary decode failure, and an off-curve uncompressed/hybrid point overwrites the existing point. Success clears the first octet's low bit for the conversion form and advances `*in` by `len`. Guest input copies and range-overflow checks run before creating a missing point; their failures throw as host validation. Fault behavior is not console-measured. A null `in` throws. Only libSceLibreSsl keys work.
