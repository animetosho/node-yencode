#include "common.h"
#include "crc_common.h"


#include "interface.h"
crcutil_interface::CRC* crc = NULL;

crcutil_interface::CRC* crcI = NULL;

// CLMUL method
extern "C" {
	uint32_t crc_fold(const unsigned char *src, long len);
}
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

void crc_arm_set_funcs();

void crc_init() {
#ifdef PLATFORM_X86
	if((cpu_flags() & 0x80202) == 0x80202) { // SSE4.1 + SSSE3 + CLMUL
		// TODO: consider splitting this off into crc_folding.c ? (this is only useful for very old compilers which don't support these features though)
		_do_crc32 = &do_crc32_clmul;
		_do_crc32_incremental = &do_crc32_incremental_clmul;
	}
#endif
#ifdef PLATFORM_ARM7
	if(
# if defined(AT_HWCAP2) && defined(HWCAP2_CRC32)
		getauxval(AT_HWCAP2) & HWCAP2_CRC32
# elif defined(AT_HWCAP) && defined(HWCAP_CRC32)
		getauxval(AT_HWCAP) & HWCAP_CRC32
# elif defined(ANDROID_CPU_FAMILY_ARM) && defined(__aarch64__)
		android_getCpuFeatures() & ANDROID_CPU_ARM64_FEATURE_CRC32
	/* no 32-bit flag - presumably CRC not allowed on 32-bit CPUs on Android */
# elif defined(__ARM_FEATURE_CRC32)
		true /* assume available if compiled as such */
# else
		false
# endif
	) {
		crc_arm_set_funcs();
	}
#endif
}
