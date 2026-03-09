/*
 * SHA-256 implementation
 * Public domain, based on FIPS 180-4
 */

#ifndef BLANG_SHA256_H_
#define BLANG_SHA256_H_

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
	uint8_t data[64];
	uint32_t datalen;
	uint64_t bitlen;
	uint32_t state[8];
} SHA256_CTX;

void sha256_init( SHA256_CTX *ctx );
void sha256_update( SHA256_CTX *ctx, const uint8_t *data, size_t len );
void sha256_final( SHA256_CTX *ctx, uint8_t hash[32] );

#ifdef __cplusplus
}
#endif

#endif /* BLANG_SHA256_H_ */
