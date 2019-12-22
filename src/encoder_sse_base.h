#include "common.h"

#include "encoder.h"
#include "encoder_common.h"

#if defined(__x86_64__) || \
    defined(__amd64__ ) || \
    defined(__LP64    ) || \
    defined(_M_X64    ) || \
    defined(_M_AMD64  ) || \
    defined(_WIN64    )
# define PLATFORM_AMD64 1
#endif

#if defined(__clang__) && __clang_major__ == 6 && __clang_minor__ == 0
// VBMI2 introduced in clang 6.0, but 128-bit functions misnamed there; fixed in clang 7.0, but we'll handle those on 6.0
# define _mm_mask_expand_epi8 _mm128_mask_expand_epi8
#endif


#pragma pack(16)
struct TShufMix {
	__m128i shuf, mix;
};
#pragma pack()

static struct {
	struct TShufMix ALIGN_TO(32, shufMix[256]);
	const unsigned char BitsSetTable256plus8[256];
	const uint32_t eolLastChar[256];
	const uint16_t eolFirstMask[256];
	uint16_t expandMask[256];
	
#if defined(__LZCNT__) && defined(__tune_amdfam10__)
	const int8_t ALIGN_TO(16, expand_maskmix_lzc[33*2*32]);
	const int8_t ALIGN_TO(16, expand_shufmaskmix_lzc[33*2*32]);
#else
	const int8_t ALIGN_TO(16, expand_maskmix_bsr[33*2*32]);
	const int8_t ALIGN_TO(16, expand_shufmaskmix_bsr[33*2*32]);
#endif
} lookups = {
	/*shufMix*/ {},
	/*BitsSetTable256plus8*/ {
		#   define B2(n) n+8,     n+9,     n+9,     n+10
		#   define B4(n) B2(n), B2(n+1), B2(n+1), B2(n+2)
		#   define B6(n) B4(n), B4(n+1), B4(n+1), B4(n+2)
		    B6(0), B6(1), B6(1), B6(2)
		#undef B2
		#undef B4
		#undef B6
	},
	#define _B1(n) _B(n), _B(n+1), _B(n+2), _B(n+3)
	#define _B2(n) _B1(n), _B1(n+4), _B1(n+8), _B1(n+12)
	#define _B3(n) _B2(n), _B2(n+16), _B2(n+32), _B2(n+48)
	#define _BX _B3(0), _B3(64), _B3(128), _B3(192)
	/*eolLastChar*/ {
		#define _B(n) ((n == 214+'\t' || n == 214+' ' || n == 214+'\0' || n == 214+'\n' || n == 214+'\r' || n == '='-42) ? (((n+42+64)&0xff)<<8)+0x0a0d003d : ((n+42)&0xff)+0x0a0d00)
			_BX
		#undef _B
	},
	/*eolFirstMask*/ {
		#define _B(n) ((n == 214+'\t' || n == 214+' ' || n == '.'-42) ? 1 : 0)
			_BX
		#undef _B
	},
	#undef _B1
	#undef _B2
	#undef _B3
	#undef _BX
	/*expandMask*/ {},
	
	
	#define _MASK(n) \
		_M1(n,0), _M1(n,1), _M1(n,2), _M1(n,3), _M1(n,4), _M1(n,5), _M1(n,6), _M1(n,7), \
		_M1(n,8), _M1(n,9), _M1(n,10), _M1(n,11), _M1(n,12), _M1(n,13), _M1(n,14), _M1(n,15), \
		_M1(n,16), _M1(n,17), _M1(n,18), _M1(n,19), _M1(n,20), _M1(n,21), _M1(n,22), _M1(n,23), \
		_M1(n,24), _M1(n,25), _M1(n,26), _M1(n,27), _M1(n,28), _M1(n,29), _M1(n,30), _M1(n,31)
	#define _SHUFMASK(n) \
		_S1(n,0), _S1(n,1), _S1(n,2), _S1(n,3), _S1(n,4), _S1(n,5), _S1(n,6), _S1(n,7), \
		_S1(n,8), _S1(n,9), _S1(n,10), _S1(n,11), _S1(n,12), _S1(n,13), _S1(n,14), _S1(n,15), \
		_M2(n,16), _M2(n,17), _M2(n,18), _M2(n,19), _M2(n,20), _M2(n,21), _M2(n,22), _M2(n,23), \
		_M2(n,24), _M2(n,25), _M2(n,26), _M2(n,27), _M2(n,28), _M2(n,29), _M2(n,30), _M2(n,31)
	// TODO: consider making _MASK work better for ANDN w/ cmp*
	
	
	#define _M1(n,k) n>k?-1:0
	#define _M2(n,k) n>=k?-1:0
	#define _S1(n,k) n==k?-1:(k-(n<k))
	#define _X1(n, m) n==m ? '=' : 42+64*(n==m-1)
	#define _MIX(n) _X1(n,0), _X1(n,1), _X1(n,2), _X1(n,3), _X1(n,4), _X1(n,5), _X1(n,6), _X1(n,7), \
		_X1(n,8), _X1(n,9), _X1(n,10), _X1(n,11), _X1(n,12), _X1(n,13), _X1(n,14), _X1(n,15), \
		_X1(n,16), _X1(n,17), _X1(n,18), _X1(n,19), _X1(n,20), _X1(n,21), _X1(n,22), _X1(n,23), \
		_X1(n,24), _X1(n,25), _X1(n,26), _X1(n,27), _X1(n,28), _X1(n,29), _X1(n,30), _X1(n,31)
	#define _MX(n) _MASK(n), _MIX(n)
	#define _SX(n) _SHUFMASK(n), _MIX(n)
	
	#if defined(__LZCNT__) && defined(__tune_amdfam10__)
	/*expand_maskmix_lzc*/ {
		_MX(31), _MX(30), _MX(29), _MX(28), _MX(27), _MX(26), _MX(25), _MX(24),
		_MX(23), _MX(22), _MX(21), _MX(20), _MX(19), _MX(18), _MX(17), _MX(16),
		_MX(15), _MX(14), _MX(13), _MX(12), _MX(11), _MX(10), _MX( 9), _MX( 8),
		_MX( 7), _MX( 6), _MX( 5), _MX( 4), _MX( 3), _MX( 2), _MX( 1), _MX( 0),
		_MX(32)
	},
	/*expand_shufmaskmix_lzc*/ {
		_SX(31), _SX(30), _SX(29), _SX(28), _SX(27), _SX(26), _SX(25), _SX(24),
		_SX(23), _SX(22), _SX(21), _SX(20), _SX(19), _SX(18), _SX(17), _SX(16),
		_SX(15), _SX(14), _SX(13), _SX(12), _SX(11), _SX(10), _SX( 9), _SX( 8),
		_SX( 7), _SX( 6), _SX( 5), _SX( 4), _SX( 3), _SX( 2), _SX( 1), _SX( 0),
		_SX(32)
	},
	#else
	/*expand_maskmix_bsr*/ {
		_MX(32),
		_MX( 0), _MX( 1), _MX( 2), _MX( 3), _MX( 4), _MX( 5), _MX( 6), _MX( 7),
		_MX( 8), _MX( 9), _MX(10), _MX(11), _MX(12), _MX(13), _MX(14), _MX(15),
		_MX(16), _MX(17), _MX(18), _MX(19), _MX(20), _MX(21), _MX(22), _MX(23),
		_MX(24), _MX(25), _MX(26), _MX(27), _MX(28), _MX(29), _MX(30), _MX(31)
	},
	/*expand_shufmaskmix_bsr*/ {
		_SX(32),
		_SX( 0), _SX( 1), _SX( 2), _SX( 3), _SX( 4), _SX( 5), _SX( 6), _SX( 7),
		_SX( 8), _SX( 9), _SX(10), _SX(11), _SX(12), _SX(13), _SX(14), _SX(15),
		_SX(16), _SX(17), _SX(18), _SX(19), _SX(20), _SX(21), _SX(22), _SX(23),
		_SX(24), _SX(25), _SX(26), _SX(27), _SX(28), _SX(29), _SX(30), _SX(31)
	}
	#endif
	#undef _MX
	#undef _SX
	#undef _MIX
	#undef _X1
	#undef _MASK
	#undef _SHUFMASK
	#undef _M1
	#undef _M2
	#undef _S1
};


static void encoder_sse_lut() {
	for(int i=0; i<256; i++) {
		int k = i;
		uint8_t* res = (uint8_t*)(&(lookups.shufMix[i].shuf));
		uint16_t expand = 0;
		int p = 0;
		for(int j=0; j<8; j++) {
			if(k & 1) {
				res[j+p] = 0xf0 + j;
				p++;
			}
			expand |= 1<<(j+p);
			res[j+p] = j;
			k >>= 1;
		}
		for(; p<8; p++)
			res[8+p] = 8+p +0x40; // +0x40 is an arbitrary value to make debugging slightly easier?  the top bit cannot be set
		
		lookups.expandMask[i] = expand;
		
		// calculate add mask for mixing escape chars in
		__m128i shuf = _mm_load_si128((__m128i*)res);
		__m128i maskEsc = _mm_cmpeq_epi8(_mm_and_si128(shuf, _mm_set1_epi8(-16)), _mm_set1_epi8(-16)); // -16 == 0xf0
		__m128i addMask = _mm_and_si128(_mm_slli_si128(maskEsc, 1), _mm_set1_epi8(64));
		addMask = _mm_or_si128(addMask, _mm_and_si128(maskEsc, _mm_set1_epi8('='-42)));
		addMask = _mm_add_epi8(addMask, _mm_set1_epi8(42));
		
		_mm_store_si128(&(lookups.shufMix[i].mix), addMask);
	}
}


// for LZCNT/BSF
#ifdef _MSC_VER
# include <intrin.h>
# include <ammintrin.h>
# define _BSR_VAR(var, src) var; _BitScanReverse((unsigned long*)&var, src)
#elif defined(__GNUC__)
// have seen Clang not like _bit_scan_reverse
# include <x86intrin.h> // for lzcnt
# define _BSR_VAR(var, src) var = (31^__builtin_clz(src))
#else
# include <x86intrin.h>
# define _BSR_VAR(var, src) var = _bit_scan_reverse(src)
#endif

static HEDLEY_ALWAYS_INLINE __m128i sse2_expand_bytes(int mask, __m128i data) {
	while(mask) {
		// get highest bit
#if defined(__LZCNT__) && defined(__tune_amdfam10__)
		unsigned long bitIndex = _lzcnt_u32(mask);
		__m128i mergeMask = _mm_load_si128((const __m128i*)lookups.expand_maskmix_lzc + bitIndex*4);
		mask ^= 0x80000000U>>bitIndex;
#else
		// TODO: consider LUT for when BSR is slow
		unsigned long _BSR_VAR(bitIndex, mask);
		__m128i mergeMask = _mm_load_si128((const __m128i*)lookups.expand_maskmix_bsr + (bitIndex+1)*4);
		mask ^= 1<<bitIndex;
#endif
		// perform expansion
		data = _mm_or_si128(
			_mm_and_si128(mergeMask, data),
			_mm_slli_si128(_mm_andnot_si128(mergeMask, data), 1)
		);
	}
	return data;
}


template<enum YEncDecIsaLevel use_isa>
HEDLEY_ALWAYS_INLINE void do_encode_sse(int line_size, int* colOffset, const uint8_t* HEDLEY_RESTRICT srcEnd, uint8_t* HEDLEY_RESTRICT& dest, size_t& len) {
	if(len < XMM_SIZE*4+1 || line_size < XMM_SIZE) return;
	
	// slower CPUs prefer to branch as mispredict penalty is probably small relative to general execution
#if defined(__tune_atom__) || defined(__tune_silvermont__) || defined(__tune_btver1__)
	const bool _PREFER_BRANCHING = true;
#else
	const bool _PREFER_BRANCHING = (use_isa < ISA_LEVEL_SSSE3);
#endif
	
	
	uint8_t *p = dest; // destination pointer
	long i = -(long)len; // input position
	long lineSizeOffset = -line_size +1;
	//long col = *colOffset - line_size +1; // for some reason, this causes GCC-8 to spill an extra register, causing the main loop to run ~5% slower, so use the alternative version below
	long col = *colOffset + lineSizeOffset;
	
	// offset position to enable simpler loop condition checking
	const int INPUT_OFFSET = XMM_SIZE*4+1 -1; // EOL handling performs a full 128b read, -1 to change <= to <
	i += INPUT_OFFSET;
	const uint8_t* es = srcEnd - INPUT_OFFSET;
	
	if(LIKELIHOOD(0.001, col >= 0)) {
		uint8_t c = es[i++];
		if(col == 0) {
			uint32_t eolChar = lookups.eolLastChar[c];
			*(uint32_t*)p = eolChar;
			p += 3 + (eolChar>>27);
			col = -line_size+1;
		} else {
			if (LIKELIHOOD(0.0273, escapedLUT[c]!=0)) {
				*(uint32_t*)p = UINT32_16_PACK(UINT16_PACK('\r', '\n'), (uint32_t)escapedLUT[c]);
				p += 4;
				col = 2-line_size + 1;
			} else {
				*(uint32_t*)p = UINT32_PACK('\r', '\n', (uint32_t)(c+42), 0);
				p += 3;
				col = 2-line_size;
			}
		}
	}
	if (LIKELIHOOD(0.999, col == -line_size+1)) {
		uint8_t c = es[i++];
		if (LIKELIHOOD(0.0273, escapedLUT[c] != 0)) {
			*(uint16_t*)p = escapedLUT[c];
			p += 2;
			col += 2;
		} else {
			*(p++) = c + 42;
			col += 1;
		}
	}
	do {
		__m128i dataA = _mm_loadu_si128((__m128i *)(es + i)); // probably not worth the effort to align
		__m128i dataB = _mm_loadu_si128((__m128i *)(es + i) +1);
		
		i += XMM_SIZE*2;
		// search for special chars
		__m128i cmpA, cmpB;
#if defined(__SSSE3__) && !defined(__tune_atom__) && !defined(__tune_silvermont__) && !defined(__tune_btver1__)
		if(use_isa >= ISA_LEVEL_SSSE3) {
			cmpA = _mm_cmpeq_epi8(
				_mm_shuffle_epi8(_mm_set_epi8(
					'\0'-42,-42,'\r'-42,'.'-42,'='-42,'\0'-42,'\t'-42,'\n'-42,-42,-42,'\r'-42,-42,'='-42,' '-42,-42,'\n'-42
				), _mm_abs_epi8(dataA)),
				dataA
			);
			cmpB = _mm_cmpeq_epi8(
				_mm_shuffle_epi8(_mm_set_epi8(
					'\0'-42,-42,'\r'-42,'.'-42,'='-42,'\0'-42,'\t'-42,'\n'-42,-42,-42,'\r'-42,-42,'='-42,' '-42,-42,'\n'-42
				), _mm_abs_epi8(dataB)),
				dataB
			);
		} else
#endif
		{
			cmpA = _mm_or_si128(
				_mm_or_si128(
					_mm_cmpeq_epi8(dataA, _mm_set1_epi8(-42)),
					_mm_cmpeq_epi8(dataA, _mm_set1_epi8('\n'-42))
				),
				_mm_or_si128(
					_mm_cmpeq_epi8(dataA, _mm_set1_epi8('='-42)),
					_mm_cmpeq_epi8(dataA, _mm_set1_epi8('\r'-42))
				)
			);
			cmpB = _mm_or_si128(
				_mm_or_si128(
					_mm_cmpeq_epi8(dataB, _mm_set1_epi8(-42)),
					_mm_cmpeq_epi8(dataB, _mm_set1_epi8('\n'-42))
				),
				_mm_or_si128(
					_mm_cmpeq_epi8(dataB, _mm_set1_epi8('='-42)),
					_mm_cmpeq_epi8(dataB, _mm_set1_epi8('\r'-42))
				)
			);
		}
		
		unsigned maskA = _mm_movemask_epi8(cmpA);
		unsigned maskB = _mm_movemask_epi8(cmpB);
		
		_encode_loop_branch:
		uint32_t mask = (maskB<<16) | maskA;
		long bitIndex; // because you can't goto past variable declarations...
		long maskBits;
		
		bool manyBitsSet;
#if defined(__POPCNT__) && !defined(__tune_btver1__)
		if(use_isa >= ISA_LEVEL_AVX) {
			maskBits = popcnt32(mask);
			manyBitsSet = maskBits > 1;
		} else
#endif
		{
			manyBitsSet = (mask & (mask-1)) != 0;
		}
		
		if (LIKELIHOOD(0.089, manyBitsSet)) {
			uint8_t m1 = maskA & 0xFF;
			uint8_t m2 = maskA >> 8;
			uint8_t m3 = maskB & 0xFF;
			uint8_t m4 = maskB >> 8;
			unsigned int shuf1Len, shuf2Len, shuf3Len, shufTotalLen;
			__m128i shuf1A, shuf1B, shuf2A, shuf2B; // only used for SSSE3 path
			__m128i data1A, data1B, data2A, data2B;
			
#if defined(__AVX512VBMI2__) && defined(__AVX512VL__) && defined(__AVX512BW__)
			if(use_isa >= ISA_LEVEL_VBMI2) {
				dataA = _mm_sub_epi8(dataA, _mm_set1_epi8(-42));
				dataA = _mm_ternarylogic_epi32(dataA, cmpA, _mm_set1_epi8(64), 0xf8); // data | (cmp & 64)
				dataB = _mm_sub_epi8(dataB, _mm_set1_epi8(-42));
				dataB = _mm_ternarylogic_epi32(dataB, cmpB, _mm_set1_epi8(64), 0xf8);
				
				data2A = _mm_mask_expand_epi8(_mm_set1_epi8('='), lookups.expandMask[m2], _mm_srli_si128(dataA, 8));
				data1A = _mm_mask_expand_epi8(_mm_set1_epi8('='), lookups.expandMask[m1], dataA);
				data2B = _mm_mask_expand_epi8(_mm_set1_epi8('='), lookups.expandMask[m4], _mm_srli_si128(dataB, 8));
				data1B = _mm_mask_expand_epi8(_mm_set1_epi8('='), lookups.expandMask[m3], dataB);
			} else
#endif
#ifdef __SSSE3__
			if(use_isa >= ISA_LEVEL_SSSE3) {
				// perform lookup for shuffle mask
				shuf1A = _mm_load_si128(&(lookups.shufMix[m1].shuf));
				shuf2A = _mm_load_si128(&(lookups.shufMix[m2].shuf));
				shuf1B = _mm_load_si128(&(lookups.shufMix[m3].shuf));
				shuf2B = _mm_load_si128(&(lookups.shufMix[m4].shuf));
				
				// second mask processes on second half, so add to the offsets
				shuf2A = _mm_or_si128(shuf2A, _mm_set1_epi8(88));
				shuf2B = _mm_or_si128(shuf2B, _mm_set1_epi8(88));
				
				// expand halves
				data2A = _mm_shuffle_epi8(dataA, shuf2A);
				data1A = _mm_shuffle_epi8(dataA, shuf1A);
				data2B = _mm_shuffle_epi8(dataB, shuf2B);
				data1B = _mm_shuffle_epi8(dataB, shuf1B);
				
				// add in escaped chars
				data1A = _mm_add_epi8(data1A, _mm_load_si128(&(lookups.shufMix[m1].mix)));
				data2A = _mm_add_epi8(data2A, _mm_load_si128(&(lookups.shufMix[m2].mix)));
				data1B = _mm_add_epi8(data1B, _mm_load_si128(&(lookups.shufMix[m3].mix)));
				data2B = _mm_add_epi8(data2B, _mm_load_si128(&(lookups.shufMix[m4].mix)));
			} else
#endif
			{
				// TODO: consider 1 bit shortcuts
				if(maskA) {
					data1A = sse2_expand_bytes(m1, dataA);
					data2A = sse2_expand_bytes(m2, _mm_srli_si128(dataA, 8));
					data1A = _mm_add_epi8(data1A, _mm_load_si128(&(lookups.shufMix[m1].mix)));
					data2A = _mm_add_epi8(data2A, _mm_load_si128(&(lookups.shufMix[m2].mix)));
					shuf1Len = lookups.BitsSetTable256plus8[m1];
					shuf2Len = shuf1Len + lookups.BitsSetTable256plus8[m2];
					STOREU_XMM(p, data1A);
					STOREU_XMM(p+shuf1Len, data2A);
					p += shuf2Len;
				} else {
					dataA = _mm_sub_epi8(dataA, _mm_set1_epi8(-42));
					STOREU_XMM(p, dataA);
					p += XMM_SIZE;
					shuf1Len = 8;
					shuf2Len = 16;
				}
				if(maskB) {
					data1B = sse2_expand_bytes(m3, dataB);
					data2B = sse2_expand_bytes(m4, _mm_srli_si128(dataB, 8));
					data1B = _mm_add_epi8(data1B, _mm_load_si128(&(lookups.shufMix[m3].mix)));
					data2B = _mm_add_epi8(data2B, _mm_load_si128(&(lookups.shufMix[m4].mix)));
					shuf3Len = lookups.BitsSetTable256plus8[m3];
					unsigned int shuf4Len = shuf3Len + lookups.BitsSetTable256plus8[m4];
					STOREU_XMM(p, data1B);
					STOREU_XMM(p+shuf3Len, data2B);
					p += shuf4Len;
					shuf3Len += shuf2Len;
					shufTotalLen = shuf2Len + shuf4Len;
				} else {
					dataB = _mm_sub_epi8(dataB, _mm_set1_epi8(-42));
					STOREU_XMM(p, dataB);
					p += XMM_SIZE;
					shuf3Len = shuf2Len + 8;
					shufTotalLen = shuf2Len + 16;
				}
			}
			
			if(use_isa >= ISA_LEVEL_SSSE3) {
				// store out
#if defined(__POPCNT__) && !defined(__tune_btver1__)
				if(use_isa >= ISA_LEVEL_AVX) {
					// TODO: check/tweak these
					shuf2Len = popcnt32(maskA) + 16;
# if defined(__tune_znver2__) || defined(__tune_znver1__) || defined(__tune_btver2__)
					shuf1Len = popcnt32(m1) + 8;
					shuf3Len = popcnt32(m3) + shuf2Len + 8;
# else
					shuf1Len = lookups.BitsSetTable256plus8[m1];
					shuf3Len = lookups.BitsSetTable256plus8[m3] + shuf2Len;
# endif
					shufTotalLen = maskBits + XMM_SIZE*2;
				} else
#endif
				{
					shuf1Len = lookups.BitsSetTable256plus8[m1];
					shuf2Len = shuf1Len + lookups.BitsSetTable256plus8[m2];
					shuf3Len = shuf2Len + lookups.BitsSetTable256plus8[m3];
					shufTotalLen = shuf3Len + lookups.BitsSetTable256plus8[m4];
				}
				STOREU_XMM(p, data1A);
				STOREU_XMM(p+shuf1Len, data2A);
				STOREU_XMM(p+shuf2Len, data1B);
				STOREU_XMM(p+shuf3Len, data2B);
				p += shufTotalLen;
			}
			col += shufTotalLen;
			
			if(LIKELIHOOD(0.3 /*guess, using 128b lines*/, col >= 0)) {
				unsigned int bitCount, shiftAmt;
				long pastMid = col - (shufTotalLen - shuf2Len);
				uint32_t eqMask;
				if(HEDLEY_UNLIKELY(pastMid >= 0)) {
					shiftAmt = shufTotalLen - col -1;
					if(use_isa >= ISA_LEVEL_VBMI2 || use_isa < ISA_LEVEL_SSSE3) {
						eqMask =
						  ((uint32_t)lookups.expandMask[m2] << shuf1Len)
						  | (uint32_t)lookups.expandMask[m1];
						i -= (shufTotalLen - shuf2Len) - 16;
					} else {
						eqMask =
						  ((uint32_t)_mm_movemask_epi8(shuf2A) << shuf1Len)
						  | (uint32_t)_mm_movemask_epi8(shuf1A);
						i += (shufTotalLen - shuf2Len) - 16;
					}
				} else {
					shiftAmt = -pastMid -1; // == ~pastMid
					if(use_isa >= ISA_LEVEL_VBMI2 || use_isa < ISA_LEVEL_SSSE3) {
						eqMask =
						  ((uint32_t)lookups.expandMask[m4] << (shuf3Len-shuf2Len))
						  | (uint32_t)lookups.expandMask[m3];
					} else {
						eqMask =
						  ((uint32_t)_mm_movemask_epi8(shuf2B) << (shuf3Len-shuf2Len))
						  | (uint32_t)_mm_movemask_epi8(shuf1B);
					}
				}
				
				if(use_isa >= ISA_LEVEL_VBMI2 || use_isa < ISA_LEVEL_SSSE3) {
#if defined(__GNUC__) && defined(PLATFORM_AMD64)
					// be careful to avoid partial flag stalls on Intel P6 CPUs (SHR+ADC will likely stall)
# if !(defined(__tune_amdfam10__) || defined(__tune_k8__))
					if(use_isa >= ISA_LEVEL_VBMI2)
# endif
					{
						asm(
							"shrl $1, %[eqMask] \n"
							"shrl %%cl, %[eqMask] \n" // TODO: can use shrq to avoid above shift?
							"adcl %[col], %[p] \n"
							: [eqMask]"+r"(eqMask), [p]"+r"(p)
							: "c"(shiftAmt), [col]"r"(~col)
						);
					}
# if !(defined(__tune_amdfam10__) || defined(__tune_k8__))
					else
# else
					if(0)
# endif
#endif
					{
						eqMask >>= shiftAmt;
						p -= col;
						if(LIKELIHOOD(0.98, (eqMask & 1) != 1))
							p--;
						else
							i++;
					}
				} else {
					eqMask >>= shiftAmt;
					col += eqMask & 1; // revert if escape char
				}
				
#if defined(__POPCNT__)
				if(use_isa >= ISA_LEVEL_AVX) {
					bitCount = popcnt32(eqMask);
				} else
#endif
				{
					unsigned char cnt = lookups.BitsSetTable256plus8[eqMask & 0xff];
					cnt += lookups.BitsSetTable256plus8[(eqMask>>8) & 0xff];
					cnt += lookups.BitsSetTable256plus8[(eqMask>>16) & 0xff];
					cnt += lookups.BitsSetTable256plus8[(eqMask>>24) & 0xff];
					bitCount = cnt-32;
				}
				
				if(use_isa >= ISA_LEVEL_VBMI2 || use_isa < ISA_LEVEL_SSSE3) {
					i -= bitCount;
				} else {
					i += bitCount;
					p -= col;
					i -= col;
				}
				goto _encode_eol_handle_pre;
			}
		} else {
			if(_PREFER_BRANCHING && LIKELIHOOD(0.663, !mask)) {
				dataA = _mm_sub_epi8(dataA, _mm_set1_epi8(-42));
				dataB = _mm_sub_epi8(dataB, _mm_set1_epi8(-42));
				STOREU_XMM(p, dataA);
				STOREU_XMM(p+XMM_SIZE, dataB);
				p += XMM_SIZE*2;
				col += XMM_SIZE*2;
				if(LIKELIHOOD(0.15, col >= 0)) {
					p -= col;
					i -= col;
					
					goto _encode_eol_handle_pre;
				}
				continue;
			}
			// shortcut for common case of only 1 bit set
#if defined(__AVX512VL__) && defined(__AVX512BW__)
			if(use_isa >= ISA_LEVEL_AVX3) {
				dataA = _mm_sub_epi8(dataA, _mm_set1_epi8(-42));
				dataA = _mm_ternarylogic_epi32(dataA, cmpA, _mm_set1_epi8(64), 0xf8); // data | (cmp & 64)
				dataB = _mm_sub_epi8(dataB, _mm_set1_epi8(-42));
				dataB = _mm_ternarylogic_epi32(dataB, cmpB, _mm_set1_epi8(64), 0xf8);
				
				// store last char
				_mm_mask_storeu_epi8(p+XMM_SIZE+1, 1<<15, dataB);
				
				uint32_t blendMask = ~(mask-1);
				dataB = _mm_mask_alignr_epi8(dataB, blendMask>>16, dataB, dataA, 15);
				dataB = _mm_ternarylogic_epi32(dataB, cmpB, _mm_set1_epi8('='), 0xb8); // (data & ~cmp) | (cmp & '=')
				
# if defined(__AVX512VBMI2__)
				if(use_isa >= ISA_LEVEL_VBMI2)
					dataA = _mm_mask_expand_epi8(_mm_set1_epi8('='), ~mask, dataA);
				else
# endif
				{
					dataA = _mm_mask_alignr_epi8(dataA, blendMask, dataA, dataA, 15); // there's no masked shift, so use ALIGNR instead
					dataA = _mm_ternarylogic_epi32(dataA, cmpA, _mm_set1_epi8('='), 0xb8);
				}
			} else
#endif
			{
				
#if !defined(__tune_btver1__)
				if(use_isa < ISA_LEVEL_AVX)
#endif
					maskBits = (mask != 0);
				if(_PREFER_BRANCHING) maskBits = 1;
				
#if defined(__LZCNT__) && defined(__tune_amdfam10__)
				// lzcnt is faster than bsf on AMD
				bitIndex = _lzcnt_u32(mask);
#else
				_BSR_VAR(bitIndex, mask);
				bitIndex |= maskBits-1; // if(mask == 0) bitIndex = -1;
#endif
				const __m128i* entries;
				
#if defined(__SSSE3__) && !defined(__tune_atom__) && !defined(__tune_silvermont__) && !defined(__tune_btver1__)
				if(use_isa >= ISA_LEVEL_SSSE3) {
# if defined(__LZCNT__) && defined(__tune_amdfam10__)
					entries = (const __m128i*)lookups.expand_shufmaskmix_lzc;
# else
					entries = (const __m128i*)lookups.expand_shufmaskmix_bsr + 4;
# endif
					entries += bitIndex*4;
					
					__m128i shufMaskA = _mm_load_si128(entries+0);
					__m128i mergeMaskB = _mm_load_si128(entries+1);
					__m128i dataBShifted = _mm_alignr_epi8(dataB, dataA, 15);
					dataB = _mm_andnot_si128(cmpB, dataB);
					
					dataA = _mm_shuffle_epi8(dataA, shufMaskA);
					
# if defined(__SSE4_1__) && !defined(__tune_silvermont__) && !defined(__tune_goldmont__) && !defined(__tune_goldmont_plus__)
					// unsure if worth on: Jaguar/Puma (3|2), Core2 (2|2)
					if(use_isa >= ISA_LEVEL_AVX) {
						dataB = _mm_blendv_epi8(dataBShifted, dataB, mergeMaskB);
					} else
# endif
					{
						dataB = _mm_or_si128(
							_mm_and_si128(mergeMaskB, dataB),
							_mm_andnot_si128(mergeMaskB, dataBShifted)
						);
					}
				} else
#endif
				{
					
#if defined(__LZCNT__) && defined(__tune_amdfam10__)
					entries = (const __m128i*)lookups.expand_maskmix_lzc;
#else
					entries = (const __m128i*)lookups.expand_maskmix_bsr + 4;
#endif
					entries += bitIndex*4;
					
					__m128i mergeMaskA = _mm_load_si128(entries+0);
					__m128i mergeMaskB = _mm_load_si128(entries+1);
					// TODO: consider deferring mask operation? (does require an extra ANDN but may help with L1 latency)
					__m128i dataAMasked = _mm_andnot_si128(mergeMaskA, dataA);
					__m128i dataBMasked = _mm_andnot_si128(mergeMaskB, dataB);
					__m128i dataAShifted = _mm_slli_si128(dataAMasked, 1);
					__m128i dataBShifted;
					
#if defined(__SSSE3__) && !defined(__tune_btver1__)
					if(use_isa >= ISA_LEVEL_SSSE3)
						dataBShifted = _mm_alignr_epi8(dataBMasked, dataAMasked, 15);
					else
#endif
						dataBShifted = _mm_or_si128(
							_mm_slli_si128(dataBMasked, 1),
							_mm_srli_si128(dataAMasked, 15)
						);
					
					// alternatively `_mm_xor_si128(dataAMasked, dataA)` if compiler wants to load mergeMask* again
					dataB = _mm_or_si128(
						_mm_and_si128(mergeMaskB, dataB), dataBShifted
					);
					dataA = _mm_or_si128(
						_mm_and_si128(mergeMaskA, dataA), dataAShifted
					);
				}
				// add escape chars
				dataA = _mm_add_epi8(dataA, _mm_load_si128(entries+2));
				dataB = _mm_add_epi8(dataB, _mm_load_si128(entries+3));
				
				// store final char
				p[XMM_SIZE*2] = es[i-1] + 42 + (64 & (mask>>(XMM_SIZE*2-1-6)));
			}
			
			// store main part
			STOREU_XMM(p, dataA);
			STOREU_XMM(p+XMM_SIZE, dataB);
			
			p += XMM_SIZE*2 + maskBits;
			col += XMM_SIZE*2 + maskBits;
			
			if(LIKELIHOOD(0.3, col >= 0)) {
#if defined(__AVX512VL__)
				if(use_isa >= ISA_LEVEL_AVX3)
					bitIndex = _lzcnt_u32(mask) +1;
				else
#endif
#if defined(__LZCNT__) && defined(__tune_amdfam10__)
				bitIndex = bitIndex +1;
#else
				bitIndex = 31-bitIndex +1;
#endif
				if(HEDLEY_UNLIKELY(col == bitIndex)) {
					// this is an escape character, so line will need to overflow
					p--;
				} else {
					i += (col > bitIndex);
				}
				p -= col;
				i -= col;
				
				_encode_eol_handle_pre:
				uint32_t eolChar = lookups.eolLastChar[es[i]];
				*(uint32_t*)p = eolChar;
				p += 3 + (eolChar>>27);
				col = lineSizeOffset;
				
				if(HEDLEY_UNLIKELY(i >= 0)) { // this isn't really a proper check - it's only needed to support short lines; basically, if the line is too short, `i` never gets checked, so we need one somewhere
					i++;
					break;
				}
				
				dataA = _mm_loadu_si128((__m128i *)(es + i + 1));
				dataB = _mm_loadu_si128((__m128i *)(es + i + 1) + 1);
				// search for special chars (EOL)
#if defined(__SSSE3__) && !defined(__tune_atom__) && !defined(__tune_silvermont__) && !defined(__tune_btver1__)
				if(use_isa >= ISA_LEVEL_SSSE3) {
					cmpA = _mm_cmpeq_epi8(
						_mm_shuffle_epi8(_mm_set_epi8(
							'\0'-42,-42,'\r'-42,'.'-42,'='-42,'\0'-42,'\t'-42,'\n'-42,-42,-42,'\r'-42,-42,'='-42,' '-42,-42,'\n'-42
						), _mm_adds_epi8(
							_mm_abs_epi8(dataA), _mm_cvtsi32_si128(88)
						)),
						dataA
					);
					maskA = _mm_movemask_epi8(cmpA);
					cmpB = _mm_cmpeq_epi8(
						_mm_shuffle_epi8(_mm_set_epi8(
							'\0'-42,-42,'\r'-42,'.'-42,'='-42,'\0'-42,'\t'-42,'\n'-42,-42,-42,'\r'-42,-42,'='-42,' '-42,-42,'\n'-42
						), _mm_abs_epi8(dataB)),
						dataB
					);
				} else
#endif
				{
					cmpA = _mm_or_si128(
						_mm_or_si128(
							_mm_cmpeq_epi8(dataA, _mm_set1_epi8(-42)),
							_mm_cmpeq_epi8(dataA, _mm_set1_epi8('\n'-42))
						),
						_mm_or_si128(
							_mm_cmpeq_epi8(dataA, _mm_set1_epi8('='-42)),
							_mm_cmpeq_epi8(dataA, _mm_set1_epi8('\r'-42))
						)
					);
					maskA = _mm_movemask_epi8(cmpA);
					maskA |= lookups.eolFirstMask[es[i+1]];
					cmpB = _mm_or_si128(
						_mm_or_si128(
							_mm_cmpeq_epi8(dataB, _mm_set1_epi8(-42)),
							_mm_cmpeq_epi8(dataB, _mm_set1_epi8('\n'-42))
						),
						_mm_or_si128(
							_mm_cmpeq_epi8(dataB, _mm_set1_epi8('='-42)),
							_mm_cmpeq_epi8(dataB, _mm_set1_epi8('\r'-42))
						)
					);
				}
				i += XMM_SIZE*2 + 1;
				maskB = _mm_movemask_epi8(cmpB);
				goto _encode_loop_branch;
			}
			
		}
	} while(i < 0);
	
	*colOffset = col + line_size -1;
	dest = p;
	len = -(i - INPUT_OFFSET);
}

