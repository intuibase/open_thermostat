#pragma once

#include <mbedtls/sha256.h>
#include <mbedtls/version.h>

namespace heating::crypto {

inline int sha256Start(mbedtls_sha256_context *context, int is224) {
#if MBEDTLS_VERSION_MAJOR >= 3
	return mbedtls_sha256_starts(context, is224);
#else
	return mbedtls_sha256_starts_ret(context, is224);
#endif
}

inline int sha256Update(mbedtls_sha256_context *context, const unsigned char *data, size_t size) {
#if MBEDTLS_VERSION_MAJOR >= 3
	return mbedtls_sha256_update(context, data, size);
#else
	return mbedtls_sha256_update_ret(context, data, size);
#endif
}

inline int sha256Finish(mbedtls_sha256_context *context, unsigned char *output) {
#if MBEDTLS_VERSION_MAJOR >= 3
	return mbedtls_sha256_finish(context, output);
#else
	return mbedtls_sha256_finish_ret(context, output);
#endif
}

} // namespace heating::crypto
