#ifndef __YENC_CRC_H
#define __YENC_CRC_H

#ifdef __cplusplus
extern "C" {
#endif



typedef uint32_t (*crc_func)(const void*, size_t, uint32_t);
extern crc_func _do_crc32_incremental;

extern int _crc32_isa;
#define do_crc32 (*_do_crc32_incremental)
static inline int crc32_isa_level() {
	return _crc32_isa;
}


#if !defined(__GNUC__) && defined(_MSC_VER)
# include <intrin.h>
#endif
// computes `(n*8) % 0xffffffff` (well, almost) using some bit-hacks
static inline uint32_t crc32_bytepow(uint64_t n) {
#ifdef __GNUC__
	unsigned res;
	unsigned carry = __builtin_uadd_overflow(n >> 32, n, &res);
	res += carry;
	return (res << 3) | (res >> 29);
#elif defined(_MSC_VER)
	unsigned res;
	unsigned char carry = _addcarry_u32(0, n >> 32, n, &res);
	_addcarry_u32(carry, res, 0, &res);
	return _rotl(res, 3);
#else
	n = (n >> 32) + (n & 0xffffffff);
	n <<= 3;
	n += n >> 32;
	return n;
#endif
}

uint32_t crc32_mul2pow(uint32_t n, uint32_t base);
uint32_t crc32_multiply(uint32_t a, uint32_t b);

static inline uint32_t crc32_combine(uint32_t crc1, uint32_t crc2, uint64_t len2) {
	return crc32_mul2pow(crc32_bytepow(len2), crc1) ^ crc2;
}
static inline uint32_t crc32_zeros(uint32_t crc1, uint64_t len) {
	return ~crc32_mul2pow(crc32_bytepow(len), ~crc1);
}
static inline uint32_t crc32_unzero(uint32_t crc1, uint64_t len) {
	return ~crc32_mul2pow(~crc32_bytepow(len), ~crc1);
}
static inline uint32_t crc32_2pow(uint32_t n) {
	return crc32_mul2pow(n, 0x80000000);
}
static inline uint32_t crc32_256pow(uint64_t n) {
	return crc32_mul2pow(crc32_bytepow(n), 0x80000000);
}

void crc_init();



#ifdef __cplusplus
}
#endif
#endif
