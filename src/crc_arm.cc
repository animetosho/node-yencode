#include "common.h"

#if defined(__ARM_FEATURE_CRC32) || defined(_M_ARM64) /* TODO: AArch32 for MSVC? */
#include "crc_common.h"

/* ARMv8 accelerated CRC */
#ifdef _MSC_VER
#include <intrin.h>
#else
#include <arm_acle.h>
#endif

// inspired/stolen off https://github.com/jocover/crc32_armv8/blob/master/crc32_armv8.c
static uint32_t arm_crc_calc(uint32_t crc, const unsigned char *src, long len) {
	
	// initial alignment
	if (len >= 16) { // 16 is an arbitrary number; it just needs to be >=8
		if ((uintptr_t)src & sizeof(uint8_t)) {
			crc = __crc32b(crc, *src);
			src++;
			len--;
		}
		if ((uintptr_t)src & sizeof(uint16_t)) {
			crc = __crc32h(crc, *((uint16_t *)src));
			src += sizeof(uint16_t);
			len -= sizeof(uint16_t);
		}
		
#ifdef __aarch64__
		if ((uintptr_t)src & sizeof(uint32_t)) {
			crc = __crc32w(crc, *((uint32_t *)src));
			src += sizeof(uint32_t);
			len -= sizeof(uint32_t);
		}
	}
	while ((len -= sizeof(uint64_t)) >= 0) {
		crc = __crc32d(crc, *((uint64_t *)src));
		src += sizeof(uint64_t);
	}
	if (len & sizeof(uint32_t)) {
		crc = __crc32w(crc, *((uint32_t *)src));
		src += sizeof(uint32_t);
	}
#else
	}
	while ((len -= sizeof(uint32_t)) >= 0) {
		crc = __crc32w(crc, *((uint32_t *)src));
		src += sizeof(uint32_t);
	}
#endif
	if (len & sizeof(uint16_t)) {
		crc = __crc32h(crc, *((uint16_t *)src));
		src += sizeof(uint16_t);
	}
	if (len & sizeof(uint8_t))
		crc = __crc32b(crc, *src);
	
	return crc;
}

static void do_crc32_arm(const void* data, size_t length, unsigned char out[4]) {
	uint32_t crc = arm_crc_calc(~0, (const unsigned char*)data, (long)length);
	UNPACK_4(out, ~crc);
}
static void do_crc32_incremental_arm(const void* data, size_t length, unsigned char init[4]) {
	uint32_t crc = PACK_4(init);
	crc = arm_crc_calc(~crc, (const unsigned char*)data, (long)length);
	UNPACK_4(init, ~crc);
}

#include "crc.h"
void crc_arm_set_funcs() {
	_do_crc32 = &do_crc32_arm;
	_do_crc32_incremental = &do_crc32_incremental_arm;
}
#else
void crc_arm_set_funcs() {}
#endif
