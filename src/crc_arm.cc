#include "common.h"
#include "crc_common.h"

#if defined(PLATFORM_ARM) && defined(_MSC_VER) && defined(__clang__) && !defined(__ARM_FEATURE_CRC32)
// I don't think GYP provides a nice way to detect whether MSVC or clang-cl is being used, but it doesn't use clang-cl by default, so a warning here is probably sufficient
HEDLEY_WARNING("CRC32 acceleration is not been enabled under ARM clang-cl by default; add `-march=armv8-a+crc` to additional compiler arguments to enable");
#endif

#if defined(__ARM_FEATURE_CRC32) || (defined(_M_ARM64) && !defined(__clang__)) // MSVC doesn't support CRC for ARM32

/* ARMv8 accelerated CRC */
#if defined(_MSC_VER) && !defined(__clang__)
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

void crc_arm_set_funcs(crc_func* _do_crc32, crc_func* _do_crc32_incremental) {
	*_do_crc32 = &do_crc32_arm;
	*_do_crc32_incremental = &do_crc32_incremental_arm;
}
#else
void crc_arm_set_funcs(crc_func* _do_crc32, crc_func* _do_crc32_incremental) {}
#endif
