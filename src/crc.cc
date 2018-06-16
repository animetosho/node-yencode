#include "common.h"


#define PACK_4(arr) (((uint_fast32_t)arr[0] << 24) | ((uint_fast32_t)arr[1] << 16) | ((uint_fast32_t)arr[2] << 8) | (uint_fast32_t)arr[3])
#define UNPACK_4(arr, val) { \
	arr[0] = (unsigned char)(val >> 24) & 0xFF; \
	arr[1] = (unsigned char)(val >> 16) & 0xFF; \
	arr[2] = (unsigned char)(val >>  8) & 0xFF; \
	arr[3] = (unsigned char)val & 0xFF; \
}

#include "interface.h"
crcutil_interface::CRC* crc = NULL;

#if defined(__ARM_FEATURE_CRC32) || defined(_M_ARM64) /* TODO: AArch32 for MSVC? */
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
#endif

crcutil_interface::CRC* crcI = NULL;

#ifdef X86_PCLMULQDQ_CRC
#include "crc_folding.c"
static void do_crc32_clmul(const void* data, size_t length, unsigned char out[4]) {
	uint32_t tmp = crc_fold((const unsigned char*)data, (long)length);
	UNPACK_4(out, tmp);
}
static void do_crc32_incremental_clmul(const void* data, size_t length, unsigned char init[4]) {
	if(!crcI) {
		crcI = crcutil_interface::CRC::Create(
			0xEDB88320, 0, 32, false, 0, 0, 0, 0, NULL);
		// instance never deleted... oh well...
	}
	
	// TODO: think of a nicer way to do this than a combine
	crcutil_interface::UINT64 crc1_ = PACK_4(init);
	crcutil_interface::UINT64 crc2_ = crc_fold((const unsigned char*)data, (long)length);
	crcI->Concatenate(crc2_, 0, length, &crc1_);
	UNPACK_4(init, crc1_);
}
#endif




static void do_crc32_generic(const void* data, size_t length, unsigned char out[4]) {
	if(!crc) {
		crc = crcutil_interface::CRC::Create(
			0xEDB88320, 0, 32, true, 0, 0, 0, 0, NULL);
		// instance never deleted... oh well...
	}
	crcutil_interface::UINT64 tmp = 0;
	crc->Compute(data, length, &tmp);
	UNPACK_4(out, tmp);
}
void (*_do_crc32)(const void*, size_t, unsigned char[4]) = &do_crc32_generic;

static void do_crc32_incremental_generic(const void* data, size_t length, unsigned char init[4]) {
	if(!crcI) {
		crcI = crcutil_interface::CRC::Create(
			0xEDB88320, 0, 32, false, 0, 0, 0, 0, NULL);
		// instance never deleted... oh well...
	}
	
	crcutil_interface::UINT64 tmp = PACK_4(init) ^ 0xffffffff;
	crcI->Compute(data, length, &tmp);
	tmp ^= 0xffffffff;
	UNPACK_4(init, tmp);
}
void (*_do_crc32_incremental)(const void*, size_t, unsigned char[4]) = &do_crc32_incremental_generic;



void do_crc32_combine(unsigned char crc1[4], const unsigned char crc2[4], size_t len2) {
	if(!crc) {
		crc = crcutil_interface::CRC::Create(
			0xEDB88320, 0, 32, true, 0, 0, 0, 0, NULL);
		// instance never deleted... oh well...
	}
	crcutil_interface::UINT64 crc1_ = PACK_4(crc1), crc2_ = PACK_4(crc2);
	crc->Concatenate(crc2_, 0, len2, &crc1_);
	UNPACK_4(crc1, crc1_);
}

void do_crc32_zeros(unsigned char crc1[4], size_t len) {
	if(!crc) {
		crc = crcutil_interface::CRC::Create(
			0xEDB88320, 0, 32, true, 0, 0, 0, 0, NULL);
		// instance never deleted... oh well...
	}
	crcutil_interface::UINT64 crc_ = 0;
	crc->CrcOfZeroes(len, &crc_);
	UNPACK_4(crc1, crc_);
}

void crc_init() {
#ifdef X86_PCLMULQDQ_CRC
	if((cpu_flags() & 0x80202) == 0x80202) { // SSE4.1 + SSSE3 + CLMUL
		_do_crc32 = &do_crc32_clmul;
		_do_crc32_incremental = &do_crc32_incremental_clmul;
	}
#endif
#if defined(__ARM_FEATURE_CRC32) || defined(_M_ARM64)
	if(
# if defined(AT_HWCAP2)
		getauxval(AT_HWCAP2) & HWCAP2_CRC32
# elif defined(ANDROID_CPU_FAMILY_ARM) && defined(ARCH_AARCH64)
		android_getCpuFeatures() & ANDROID_CPU_ARM64_FEATURE_CRC32
	/* no 32-bit flag - presumably CRC not allowed on 32-bit CPUs on Android */
# else
		true /* assume available if compiled as such */
# endif
	) {
		_do_crc32 = &do_crc32_arm;
		_do_crc32_incremental = &do_crc32_incremental_arm;
	}
#endif
}
