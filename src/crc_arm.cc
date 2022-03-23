#include "crc_common.h"

#if defined(PLATFORM_ARM) && defined(_MSC_VER) && defined(__clang__) && !defined(__ARM_FEATURE_CRC32)
// I don't think GYP provides a nice way to detect whether MSVC or clang-cl is being used, but it doesn't use clang-cl by default, so a warning here is probably sufficient
HEDLEY_WARNING("CRC32 acceleration is not been enabled under ARM clang-cl by default; add `-march=armv8-a+crc` to additional compiler arguments to enable");
#endif

// disable CRC on GCC versions with broken arm_acle.h
#if defined(__ARM_FEATURE_CRC32) && defined(HEDLEY_GCC_VERSION)
# if !defined(__aarch64__) && HEDLEY_GCC_VERSION_CHECK(7,0,0) && !HEDLEY_GCC_VERSION_CHECK(8,1,1)
#  undef __ARM_FEATURE_CRC32
HEDLEY_WARNING("CRC32 acceleration has been disabled due to broken arm_acle.h shipped in GCC 7.0 - 8.1 [https://gcc.gnu.org/bugzilla/show_bug.cgi?id=81497]. If you need this feature, please use a different compiler or version of GCC");
# endif
# if defined(__aarch64__) && HEDLEY_GCC_VERSION_CHECK(9,4,0) && !HEDLEY_GCC_VERSION_CHECK(9,5,0)
#  undef __ARM_FEATURE_CRC32
HEDLEY_WARNING("CRC32 acceleration has been disabled due to broken arm_acle.h shipped in GCC 9.4 [https://gcc.gnu.org/bugzilla/show_bug.cgi?id=100985]. If you need this feature, please use a different compiler or version of GCC");
# endif
#endif

#if defined(__ARM_FEATURE_CRC32) || (defined(_M_ARM64) && !defined(__clang__)) // MSVC doesn't support CRC for ARM32

/* ARMv8 accelerated CRC */
#if defined(_MSC_VER) && !defined(__clang__)
#include <intrin.h>
#else
#include <arm_acle.h>
#endif


#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
# ifdef __GNUC__
#  define _LE16 __builtin_bswap16
#  define _LE32 __builtin_bswap32
#  define _LE64 __builtin_bswap64
# else
// currently not supported
#  error No endian swap intrinsic defined
# endif
#else
# define _LE16(x) (x)
# define _LE32(x) (x)
# define _LE64(x) (x)
#endif

#ifdef __aarch64__
# define WORD_T uint64_t
# define WORDSIZE_LOG 3  // sizeof(WORD_T) == 1<<WORDSIZE_LOG
# define CRC_WORD(crc, data) __crc32d(crc, _LE64(data))
#else
# define WORD_T uint32_t
# define WORDSIZE_LOG 2  // sizeof(WORD_T) == 1<<WORDSIZE_LOG
# define CRC_WORD(crc, data) __crc32w(crc, _LE32(data))
#endif


// exploit CPU pipelining during CRC computation; unfortunately I haven't been able to measure any benefit
// - Neoverse N1: no noticeable difference
// - Cortex A53: actually runs a bit slower
//#define ENABLE_PIPELINE_OPT 1

#ifdef ENABLE_PIPELINE_OPT
// workaround MSVC complaining "unary minus operator applied to unsigned type, result still unsigned"
#define NEGATE(n) (uint32_t)(-((int32_t)(n)))

static HEDLEY_ALWAYS_INLINE uint32_t crc_multiply(uint32_t a, uint32_t b) {
	uint32_t res = 0;
	for(int i=0; i<31; i++) {
		res ^= NEGATE(b>>31) & a;
		a = ((a >> 1) ^ (0xEDB88320 & NEGATE(a&1)));
		b <<= 1;
	}
	res ^= NEGATE(b>>31) & a;
	return res;
}

static const uint32_t crc_power[] = { // pre-computed 2^n, with first 3 entries removed (saves a shift)
	0x00800000, 0x00008000, 0xedb88320, 0xb1e6b092, 0xa06a2517, 0xed627dae, 0x88d14467, 0xd7bbfe6a,
	0xec447f11, 0x8e7ea170, 0x6427800e, 0x4d47bae0, 0x09fe548f, 0x83852d0f, 0x30362f1a, 0x7b5a9cc3,
	0x31fec169, 0x9fec022a, 0x6c8dedc4, 0x15d6874d, 0x5fde7a4e, 0xbad90e37, 0x2e4e5eef, 0x4eaba214,
	0xa8a472c0, 0x429a969e, 0x148d302a, 0xc40ba6d0, 0xc4e22c3c, 0x40000000, 0x20000000, 0x08000000
};
/* above table can be computed with
	int main(void) {
		uint32_t k = 0x80000000 >> 1;
		for (size_t i = 0; i < 32+3; ++i) {
			if(i>2) printf("0x%08x, ", k);
			k = crc_multiply(k, k);
		}
		return 0;
	}
*/
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
			crc = __crc32h(crc, _LE16(*((uint16_t *)src)));
			src += sizeof(uint16_t);
			len -= sizeof(uint16_t);
		}
#ifdef __aarch64__
		if ((uintptr_t)src & sizeof(uint32_t)) {
			crc = __crc32w(crc, _LE32(*((uint32_t *)src)));
			src += sizeof(uint32_t);
			len -= sizeof(uint32_t);
		}
#endif
	}
	
	const WORD_T* srcW = (const WORD_T*)src;
	
#ifdef ENABLE_PIPELINE_OPT
	// uses ideas from https://github.com/komrad36/crc#option-13-golden
	// (this is a slightly less efficient, but much simpler implementation of the idea)
	const unsigned SPLIT_WORDS_LOG = 10;  // make sure it's at least 2
	const unsigned SPLIT_WORDS = 1<<SPLIT_WORDS_LOG;
	while(len >= (long)(sizeof(WORD_T)*SPLIT_WORDS*2)) {
		// compute 2x CRCs concurrently to leverage piplining
		uint32_t crc2 = 0;
		for(unsigned i=0; i<SPLIT_WORDS; i+=4) {
			crc = CRC_WORD(crc, *srcW);
			crc2 = CRC_WORD(crc2, *(srcW + SPLIT_WORDS));
			srcW++;
			crc = CRC_WORD(crc, *srcW);
			crc2 = CRC_WORD(crc2, *(srcW + SPLIT_WORDS));
			srcW++;
			crc = CRC_WORD(crc, *srcW);
			crc2 = CRC_WORD(crc2, *(srcW + SPLIT_WORDS));
			srcW++;
			crc = CRC_WORD(crc, *srcW);
			crc2 = CRC_WORD(crc2, *(srcW + SPLIT_WORDS));
			srcW++;
		}
		// merge the CRCs
		// since we're multiplying by a fixed number, it could be sped up with some lookup tables
		crc = crc_multiply(crc, crc_power[SPLIT_WORDS_LOG + WORDSIZE_LOG]) ^ crc2;
		srcW += SPLIT_WORDS;
		len -= sizeof(WORD_T)*SPLIT_WORDS*2;
	}
#endif
	
	while ((len -= sizeof(WORD_T)*8) >= 0) {
		crc = CRC_WORD(crc, *(srcW++));
		crc = CRC_WORD(crc, *(srcW++));
		crc = CRC_WORD(crc, *(srcW++));
		crc = CRC_WORD(crc, *(srcW++));
		crc = CRC_WORD(crc, *(srcW++));
		crc = CRC_WORD(crc, *(srcW++));
		crc = CRC_WORD(crc, *(srcW++));
		crc = CRC_WORD(crc, *(srcW++));
	}
	if (len & sizeof(WORD_T)*4) {
		crc = CRC_WORD(crc, *(srcW++));
		crc = CRC_WORD(crc, *(srcW++));
		crc = CRC_WORD(crc, *(srcW++));
		crc = CRC_WORD(crc, *(srcW++));
	}
	if (len & sizeof(WORD_T)*2) {
		crc = CRC_WORD(crc, *(srcW++));
		crc = CRC_WORD(crc, *(srcW++));
	}
	if (len & sizeof(WORD_T)) {
		crc = CRC_WORD(crc, *(srcW++));
	}
	src = (const unsigned char*)srcW;
	
#ifdef __aarch64__
	if (len & sizeof(uint32_t)) {
		crc = __crc32w(crc, _LE32(*((uint32_t *)src)));
		src += sizeof(uint32_t);
	}
#endif
	if (len & sizeof(uint16_t)) {
		crc = __crc32h(crc, _LE16(*((uint16_t *)src)));
		src += sizeof(uint16_t);
	}
	if (len & sizeof(uint8_t))
		crc = __crc32b(crc, *src);
	
	return crc;
}

static uint32_t do_crc32_incremental_arm(const void* data, size_t length, uint32_t init) {
	return ~arm_crc_calc(~init, (const unsigned char*)data, (long)length);
}

void crc_arm_set_funcs(crc_func* _do_crc32_incremental) {
	*_do_crc32_incremental = &do_crc32_incremental_arm;
}
#else
void crc_arm_set_funcs(crc_func* _do_crc32_incremental) {
	(void)_do_crc32_incremental;
}
#endif
