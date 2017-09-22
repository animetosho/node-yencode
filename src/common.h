#ifndef __YENC_COMMON
#define __YENC_COMMON

// MSVC compatibility
#if (defined(_M_IX86_FP) && _M_IX86_FP == 2) || defined(_M_X64)
	#define __SSE2__ 1
	#define __SSSE3__ 1
	//#define __SSE4_1__ 1
	#if defined(_MSC_VER) && _MSC_VER >= 1600
		#define X86_PCLMULQDQ_CRC 1
	#endif
#endif
#ifdef _MSC_VER
#define __BYTE_ORDER__ 1234
#define __ORDER_BIG_ENDIAN__ 4321
#include <intrin.h>
#endif

#if defined(__x86_64__) || defined(__i386__)
#if !defined(X86_PCLMULQDQ_CRC) && defined(__PCLMUL__) && defined(__SSSE3__) && defined(__SSE4_1__)
	#define X86_PCLMULQDQ_CRC 1
#endif
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

#ifdef _MSC_VER
#define ALIGN_32(v) __declspec(align(32)) v
#else
#define ALIGN_32(v) v __attribute__((aligned(32)))
#endif

#ifdef __SSSE3__
#include <tmmintrin.h>
#endif
#ifdef __SSE4_1__
#include <smmintrin.h>
#endif
#ifdef __POPCNT__
#include <nmmintrin.h>
#endif
/*
#ifdef __AVX2__
#include <immintrin.h>
#endif
*/

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



static bool cpu_supports_shuffle() {
#ifdef __SSSE3__
	int flags;
#ifdef _MSC_VER
	int cpuInfo[4];
	__cpuid(cpuInfo, 1);
	flags = cpuInfo[2];
#else
	// conveniently stolen from zlib-ng
	__asm__ __volatile__ (
		"cpuid"
	: "=c" (flags)
	: "a" (1)
	: "%edx", "%ebx"
	);
#endif
	
	int fastYencMask = 0x200;
#ifdef __POPCNT__
	fastYencMask |= 0x800000;
#endif
	
	return (flags & fastYencMask) == fastYencMask;
	
#else
	return false;
#endif
}


#include <v8.h>


#endif /* __YENC_COMMON */
