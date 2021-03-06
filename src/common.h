#ifndef __YENC_COMMON
#define __YENC_COMMON

#include "hedley.h"

#if defined(__x86_64__) || \
    defined(__amd64__ ) || \
    defined(__LP64    ) || \
    defined(_M_X64    ) || \
    defined(_M_AMD64  ) || \
    defined(_WIN64    )
	#define PLATFORM_AMD64 1
#endif
#if defined(PLATFORM_AMD64) || \
    defined(__i386__  ) || \
    defined(__i486__  ) || \
    defined(__i586__  ) || \
    defined(__i686__  ) || \
    defined(_M_I86    ) || \
    defined(_M_IX86   ) || \
    defined(_WIN32    )
	#define PLATFORM_X86 1
#endif
#if defined(__aarch64__) || \
    defined(__armv7__  ) || \
    defined(__arm__    ) || \
    defined(_M_ARM64   ) || \
    defined(_M_ARM     ) || \
    defined(__ARM_ARCH_6__ ) || \
    defined(__ARM_ARCH_7__ ) || \
    defined(__ARM_ARCH_7A__) || \
    defined(__ARM_ARCH_8A__) || \
    (defined(__ARM_ARCH    ) && __ARM_ARCH >= 6)
	#define PLATFORM_ARM 1
#endif


#if defined(_MSC_VER) || defined(__MINGW32__) || defined(__MINGW64__)
	#define ALIGN_ALLOC(buf, len, align) *(void**)&(buf) = _aligned_malloc((len), align)
	#define ALIGN_FREE _aligned_free
#elif defined(__cplusplus) && __cplusplus >= 201100 && !(defined(_MSC_VER) && (defined(__clang__) || defined(_M_ARM64) || defined(_M_ARM))) && !defined(__APPLE__)
	// C++11 method
	// len needs to be a multiple of alignment, although it sometimes works if it isn't...
	#include <cstdlib>
	#define ALIGN_ALLOC(buf, len, align) *(void**)&(buf) = aligned_alloc(align, ((len) + (align)-1) & ~((align)-1))
	#define ALIGN_FREE free
#else
	#include <stdlib.h>
	#define ALIGN_ALLOC(buf, len, align) if(posix_memalign((void**)&(buf), align, (len))) (buf) = NULL
	#define ALIGN_FREE free
#endif


// MSVC compatibility
#if ((defined(_M_IX86_FP) && _M_IX86_FP == 2) || defined(_M_X64)) && !defined(__clang__)
	#define __SSE2__ 1
	#define __SSSE3__ 1
	#define __SSE4_1__ 1
	#if defined(_MSC_VER) && _MSC_VER >= 1600
		#define __POPCNT__ 1
		#define __LZCNT__ 1
	#endif
	#if !defined(__AVX__) && (_MSC_VER >= 1700 && defined(__SSE2__))
		#define __AVX__ 1
	#endif
	#if !defined(__AVX2__) && (_MSC_VER >= 1800 && defined(__SSE2__))
		#define __AVX2__ 1
		#define __BMI2__ 1
	#endif
	/* AVX512 requires VS 15.3 */
	#if !defined(__AVX512F__) && (_MSC_VER >= 1911 && defined(__AVX__))
		#define __AVX512BW__ 1
		#define __AVX512F__ 1
	#endif
	/* AVX512VL not available until VS 15.5 */
	#if defined(__AVX512F__) && _MSC_VER >= 1912
		#define __AVX512VL__ 1
	#endif
	#if defined(__AVX512F__) && _MSC_VER >= 1920
		#define __AVX512VBMI__ 1
		#define __AVX512VBMI2__ 1
	#endif
#endif
#if defined(_M_ARM64)
	#define __aarch64__ 1
	#define __ARM_NEON 1
#endif
#if defined(_M_ARM)
	/*#define __ARM_NEON 1*/
#endif
#ifdef _MSC_VER
#define __BYTE_ORDER__ 1234
#define __ORDER_BIG_ENDIAN__ 4321
#include <intrin.h>
#endif


// combine two 8-bit ints into a 16-bit one
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#define UINT16_PACK(a, b) (((a) << 8) | (b))
#define UINT32_PACK(a, b, c, d) (((a) << 24) | ((b) << 16) | ((c) << 8) | (d))
#define UINT32_16_PACK(a, b) (((a) << 16) | (b))
#else
#define UINT16_PACK(a, b) ((a) | ((b) << 8))
#define UINT32_PACK(a, b, c, d) ((a) | ((b) << 8) | ((c) << 16) | ((d) << 24))
#define UINT32_16_PACK(a, b) ((a) | ((b) << 16))
#endif

#ifdef __SSE2__
#include <emmintrin.h>
#define XMM_SIZE 16 /*== (signed int)sizeof(__m128i)*/

#ifdef __SSSE3__
#include <tmmintrin.h>
#endif
#ifdef __POPCNT__
#include <nmmintrin.h>
// POPCNT can never return a negative result, but GCC doesn't seem to realise this, so typecast it to hint it better
#define popcnt32 (unsigned int)_mm_popcnt_u32 
#endif

#if defined(__AVX2__) || defined(__AVX512F__)
#include <immintrin.h>
#endif


#if defined(__tune_core2__) || defined(__tune_atom__)
/* on older Intel CPUs, plus first gen Atom, it is faster to store XMM registers in half */
# define STOREU_XMM(dest, xmm) \
  _mm_storel_epi64((__m128i*)(dest), xmm); \
  _mm_storeh_pi(((__m64*)(dest) +1), _mm_castsi128_ps(xmm))
#else
# define STOREU_XMM(dest, xmm) \
  _mm_storeu_si128((__m128i*)(dest), xmm)
#endif

#endif

#ifdef __ARM_NEON
# include <arm_neon.h>
#endif
#ifdef PLATFORM_ARM
bool cpu_supports_neon();
#endif

#ifdef _MSC_VER
#define ALIGN_TO(a, v) __declspec(align(a)) v
#else
#define ALIGN_TO(a, v) v __attribute__((aligned(a)))
#endif


#ifdef PLATFORM_X86
enum YEncDecIsaLevel {
	ISA_FEATURE_POPCNT = 0x1,
	ISA_FEATURE_LZCNT = 0x2,
	ISA_LEVEL_SSE2 = 0x100,
	ISA_LEVEL_SSSE3 = 0x200,
	ISA_LEVEL_SSE41 = 0x300,
	ISA_LEVEL_SSE4_POPCNT = 0x301,
	ISA_LEVEL_AVX = 0x381, // same as above, just used as a differentiator for `cpu_supports_isa`
	ISA_LEVEL_AVX2 = 0x383, // also includes BMI1/2 and LZCNT
	ISA_LEVEL_AVX3 = 0x403, // SKX variant; AVX512VL + AVX512BW
	ISA_LEVEL_VBMI2 = 0x503 // ICL
};
#ifdef _MSC_VER
// native tuning not supported in MSVC
# define ISA_NATIVE ISA_LEVEL_SSE2
#else
# if defined(__AVX512VBMI2__)
#  define _ISA_NATIVE ISA_LEVEL_VBMI2
# elif defined(__AVX512BW__)
#  define _ISA_NATIVE ISA_LEVEL_AVX3
# elif defined(__AVX2__)
#  define _ISA_NATIVE ISA_LEVEL_AVX2
# elif defined(__SSE4_1__)
#  define _ISA_NATIVE ISA_LEVEL_SSE41
# elif defined(__SSSE3__)
#  define _ISA_NATIVE ISA_LEVEL_SSSE3
# else
#  define _ISA_NATIVE ISA_LEVEL_SSE2
# endif
# if defined(__POPCNT__)
#  if defined(__LZCNT__)
#   define ISA_NATIVE (enum YEncDecIsaLevel)(_ISA_NATIVE | ISA_FEATURE_POPCNT | ISA_FEATURE_LZCNT)
#  else 
#   define ISA_NATIVE (enum YEncDecIsaLevel)(_ISA_NATIVE | ISA_FEATURE_POPCNT)
#  endif
# else
#  define ISA_NATIVE _ISA_NATIVE
# endif
#endif

#ifdef _MSC_VER
# define _cpuid1(ar) __cpuid(ar, 1)
#else
# include <cpuid.h>
# define _cpuid1(ar) __cpuid(1, ar[0], ar[1], ar[2], ar[3])
#endif

int cpu_supports_isa();
#endif // PLATFORM_X86

#include <string.h>
#if !defined(_MSC_VER) || defined(_STDINT) || _MSC_VER >= 1900
# include <stdint.h>
# include <stddef.h>
#else
/* Workaround for older MSVC not supporting stdint.h - just pull it from V8 */
# include <v8.h>
#endif


// GCC 8/9/10(dev) fails to optimize cases where KNOT should be used, so use intrinsic explicitly; Clang 6+ has no issue, but Clang 6/7 doesn't have the intrinsic; MSVC 2019 also fails and lacks the intrinsic
#if defined(__GNUC__) && __GNUC__ >= 7
# define KNOT16 _knot_mask16
# define KNOT32 _knot_mask32
#else
# define KNOT16(x) ((__mmask16)~(x))
# define KNOT32(x) ((__mmask32)~(x))
#endif

// weird thing with Apple's Clang; doesn't seem to always occur, so assume that Clang >= 9 is fine: https://github.com/animetosho/node-yencode/issues/8#issuecomment-583385864
// seems that Clang < 3.6 also uses the old name
#if defined(__clang__) && ((defined(__APPLE__) && __clang_major__ < 9) || __clang_major__ < 3 || (__clang_major__ == 3 && __clang_minor__ < 6))
# define _lzcnt_u32 __lzcnt32
#endif

#ifdef __GNUC__
# if __GNUC__ >= 9
#  define LIKELIHOOD(p, c) (HEDLEY_PREDICT(!!(c), 1, p))
# else
#  define LIKELIHOOD(p, c) (p>0.3 && p<0.7 ? HEDLEY_UNPREDICTABLE(!!(c)) : __builtin_expect(!!(c), (p >= 0.5)))
# endif
#else
# define LIKELIHOOD(p, c) (c)
#endif

#endif /* __YENC_COMMON */
