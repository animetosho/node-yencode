#include "crc_common.h"

#include "interface.h"
crcutil_interface::CRC* crc = NULL;

static uint32_t do_crc32_incremental_generic(const void* data, size_t length, uint32_t init) {
	crcutil_interface::UINT64 tmp = init;
	crc->Compute(data, length, &tmp);
	return (uint32_t)tmp;
}
crc_func _do_crc32_incremental = &do_crc32_incremental_generic;



uint32_t do_crc32_combine(uint32_t crc1, uint32_t crc2, size_t len2) {
	crcutil_interface::UINT64 crc1_ = crc1, crc2_ = crc2;
	crc->Concatenate(crc2_, 0, len2, &crc1_);
	return (uint32_t)crc1_;
}

uint32_t do_crc32_zeros(uint32_t crc1, size_t len) {
	crcutil_interface::UINT64 crc_ = crc1;
	crc->CrcOfZeroes(len, &crc_);
	return (uint32_t)crc_;
}

void crc_clmul_set_funcs(crc_func*);
void crc_clmul256_set_funcs(crc_func*);
void crc_arm_set_funcs(crc_func*);

#ifdef PLATFORM_X86
int cpu_supports_crc_isa();
#endif

#if defined(PLATFORM_ARM) && defined(_WIN32)
# define WIN32_LEAN_AND_MEAN
# include <Windows.h>
#endif
#ifdef PLATFORM_ARM
# ifdef __ANDROID__
#  include <cpu-features.h>
# elif defined(__linux__) || (defined(__FreeBSD__) && __FreeBSD__ >= 12)
#  include <sys/auxv.h>
#  include <asm/hwcap.h>
# elif (defined(__FreeBSD__) && __FreeBSD__ < 12)
#  include <sys/sysctl.h>
#  include <asm/hwcap.h>
# elif defined(__APPLE__)
#  include <sys/types.h>
#  include <sys/sysctl.h>
# endif
# ifdef __FreeBSD__
static unsigned long getauxval(unsigned long cap) {
	unsigned long ret;
	elf_aux_info(cap, &ret, sizeof(ret));
	return ret;
}
# endif
#endif
void crc_init() {
	crc = crcutil_interface::CRC::Create(
		0xEDB88320, 0, 32, true, 0, 0, 0, 0, NULL);
	// instance never deleted... oh well...
	
#ifdef PLATFORM_X86
	int support = cpu_supports_crc_isa();
	if(support == 2)
		crc_clmul256_set_funcs(&_do_crc32_incremental);
	else if(support == 1)
		crc_clmul_set_funcs(&_do_crc32_incremental);
#endif
#ifdef PLATFORM_ARM
# ifdef __APPLE__
	int supported = 0;
	size_t len = sizeof(supported);
	if(sysctlbyname("hw.optional.armv8_crc32", &supported, &len, NULL, 0))
		supported = 0;
# endif
	if(
# if defined(AT_HWCAP2) && defined(HWCAP2_CRC32)
		getauxval(AT_HWCAP2) & HWCAP2_CRC32
# elif defined(AT_HWCAP) && defined(HWCAP_CRC32)
		getauxval(AT_HWCAP) & HWCAP_CRC32
# elif defined(ANDROID_CPU_FAMILY_ARM) && defined(__aarch64__)
		android_getCpuFeatures() & ANDROID_CPU_ARM64_FEATURE_CRC32
# elif defined(ANDROID_CPU_FAMILY_ARM) /* aarch32 */
		android_getCpuFeatures() & ANDROID_CPU_ARM_FEATURE_CRC32
# elif defined(_WIN32)
		IsProcessorFeaturePresent(PF_ARM_V8_CRC32_INSTRUCTIONS_AVAILABLE)
# elif defined(__APPLE__)
		supported
# elif defined(__ARM_FEATURE_CRC32)
		true /* assume available if compiled as such */
# else
		false
# endif
	) {
		crc_arm_set_funcs(&_do_crc32_incremental);
	}
#endif
}
