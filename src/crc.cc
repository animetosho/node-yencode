#include "crc_common.h"

#include "interface.h"
crcutil_interface::CRC* crc = NULL;

#if defined(PLATFORM_X86) && !defined(__ILP32__)
static uint32_t do_crc32_incremental_generic(const void* data, size_t length, uint32_t init) {
	// use optimised ASM on x86 platforms
	crcutil_interface::UINT64 tmp = init;
	crc->Compute(data, length, &tmp);
	return (uint32_t)tmp;
}
#else
static uint32_t* HEDLEY_RESTRICT crc_slice_table;
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
# if defined(__GNUC__) || defined(__clang__)
#  define bswap32 __builtin_bswap32
# else
static inline uint32_t bswap32(uint32_t x) {
	return (x >> 24) | ((x >> 8) & 0x0000FF00) | ((x << 8) & 0x00FF0000) | (x << 24);
}
# endif
#endif

#define CRC32_GENERIC_CHAINS 4 // newer processors may prefer 8
static uint32_t do_crc32_incremental_generic(const void* data, size_t length, uint32_t init) {
	const uint32_t* crc_base_table = crc_slice_table + 4*256; // this also seems to help MSVC's optimiser, which otherwise keeps trying to add to crc_slice_table every time it's referenced
	uint32_t crc[CRC32_GENERIC_CHAINS]; // Clang seems to be more spill happy with an array over individual variables :(
	crc[0] = ~init;
	uint8_t* current8 = (uint8_t*)data;
	
	// align to multiple of 4
	if(((uintptr_t)current8 & 1) && length >= 1) {
		crc[0] = (crc[0] >> 8) ^ crc_base_table[(crc[0] & 0xFF) ^ *current8++];
		length--;
	}
	if(((uintptr_t)current8 & 2) && length >= 2) {
		crc[0] = (crc[0] >> 8) ^ crc_base_table[(crc[0] & 0xFF) ^ *current8++];
		crc[0] = (crc[0] >> 8) ^ crc_base_table[(crc[0] & 0xFF) ^ *current8++];
		length -= 2;
	}
	
	uint8_t* end8 = current8 + length;
	uint32_t* current = (uint32_t*)current8;
	if(length >= 8*CRC32_GENERIC_CHAINS-4) {
		size_t lenMain = ((length-(CRC32_GENERIC_CHAINS-1)*4) / 4);
		uint32_t* end = current + (lenMain / CRC32_GENERIC_CHAINS) * CRC32_GENERIC_CHAINS;
		for(int c=1; c<CRC32_GENERIC_CHAINS; c++)
			crc[c] = 0;
		while(current != end) {
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
			#define CRC_PROC4(v, in) \
				v ^= bswap32(in); \
				v = crc_slice_table[v >> 24] ^ crc_slice_table[0x100L + ((v >> 16) & 0xff)] ^ crc_slice_table[0x200L + ((v >> 8) & 0xff)] ^ crc_slice_table[0x300L + (v & 0xff)]
#else
			#define CRC_PROC4(v, in) \
				v ^= (in); \
				v = crc_slice_table[v >> 24] ^ crc_slice_table[0x100L + ((v >> 16) & 0xff)] ^ crc_slice_table[0x200L + ((v >> 8) & 0xff)] ^ crc_slice_table[0x300L + (v & 0xff)]
#endif
			for(int c=0; c<CRC32_GENERIC_CHAINS; c++) {
				CRC_PROC4(crc[c], *current);
				current++;
			}
		}
		// aggregate accumulators
		current8 = (uint8_t*)current;
		#if (CRC32_GENERIC_CHAINS & (CRC32_GENERIC_CHAINS-1)) == 0
		// assume that lengths which are a multiple of 4/8/16/32 are common
		if((end8 - current8) & (CRC32_GENERIC_CHAINS*4)) {
			CRC_PROC4(crc[0], *current);
			current8 += 4;
			
			for(int c=1; c<CRC32_GENERIC_CHAINS; c++) {
				for(int i=0; i<4; i++)
					crc[c] = (crc[c] >> 8) ^ crc_base_table[(crc[c] & 0xff) ^ *current8++];
				crc[(c+1) & ~CRC32_GENERIC_CHAINS] ^= crc[c];
			}
		} else
		#endif
		#undef CRC_PROC4
		for(int c=1; c<CRC32_GENERIC_CHAINS; c++) {
			for(int i=0; i<4; i++)
				crc[0] = (crc[0] >> 8) ^ crc_base_table[(crc[0] & 0xff) ^ *current8++];
			crc[0] ^= crc[c];
		}
	}
	
	// tail loop
	while(current8 != end8) {
		crc[0] = (crc[0] >> 8) ^ crc_base_table[(crc[0] & 0xFF) ^ *current8++];
	}
	return ~crc[0];
}
static void generate_crc32_slice_table() {
	crc_slice_table = (uint32_t*)malloc(5*256*sizeof(uint32_t));
	// generate standard byte-by-byte table
	uint32_t* crc_base_table = crc_slice_table + 4*256;
	for(int v=0; v<256; v++) {
		uint32_t crc = v;
		for(int j = 0; j < 8; j++) {
			crc = (crc >> 1) ^ (-(int32_t)(crc & 1) & 0xEDB88320);
		}
		crc_base_table[v] = crc;
	}
	
	// generate slice-by-4 shifted across for X independent chains
	for(int v=0; v<256; v++) {
		uint32_t crc = crc_base_table[v];
		#if CRC32_GENERIC_CHAINS > 1
		for(int i=0; i<4*CRC32_GENERIC_CHAINS-5; i++)
			crc = (crc >> 8) ^ crc_base_table[crc & 0xff];
		for(int i=0; i<4; i++) {
			crc = (crc >> 8) ^ crc_base_table[crc & 0xff];
			crc_slice_table[i*256 + v] = crc;
		}
		#else
		for(int i=0; i<4; i++) {
			crc_slice_table[i*256 + v] = crc;
			crc = (crc >> 8) ^ crc_base_table[crc & 0xff];
		}
		#endif
	}
}
#endif

extern "C" {
	crc_func _do_crc32_incremental = &do_crc32_incremental_generic;
	int _crc32_isa = ISA_GENERIC;
}


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

void crc_clmul_set_funcs();
void crc_clmul256_set_funcs();
void crc_arm_set_funcs();

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
# elif defined(__APPLE__)
#  include <sys/types.h>
#  include <sys/sysctl.h>
# elif defined(__has_include)
#  if __has_include(<sys/auxv.h>)
#   include <sys/auxv.h>
#   ifdef __FreeBSD__
static unsigned long getauxval(unsigned long cap) {
	unsigned long ret;
	elf_aux_info(cap, &ret, sizeof(ret));
	return ret;
}
#   endif
#   if __has_include(<asm/hwcap.h>)
#    include <asm/hwcap.h>
#   endif
#  endif
# endif
#endif
void crc_init() {
	crc = crcutil_interface::CRC::Create(
		0xEDB88320, 0, 32, true, 0, 0, 0, 0, NULL);
	// instance never deleted... oh well...
	
#if !defined(PLATFORM_X86) || defined(__ILP32__)
	generate_crc32_slice_table();
#endif
	
#ifdef PLATFORM_X86
	int support = cpu_supports_crc_isa();
	if(support == 2)
		crc_clmul256_set_funcs();
	else if(support == 1)
		crc_clmul_set_funcs();
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
		crc_arm_set_funcs();
	}
#endif
}
