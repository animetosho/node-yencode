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
	
	const int8_t ALIGN_TO(16, perm_expand[33*32]);
	
#if defined(__LZCNT__) && defined(__tune_amdfam10__)
	const int8_t ALIGN_TO(16, expand_maskmix_lzc[33*2*32]);
#else
	const int8_t ALIGN_TO(16, expand_maskmix_bsr[33*2*32]);
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
	
	
	#define _X(n) \
		_X2(n,0), _X2(n,1), _X2(n,2), _X2(n,3), _X2(n,4), _X2(n,5), _X2(n,6), _X2(n,7), \
		_X2(n,8), _X2(n,9), _X2(n,10), _X2(n,11), _X2(n,12), _X2(n,13), _X2(n,14), _X2(n,15), \
		_X2(n,16), _X2(n,17), _X2(n,18), _X2(n,19), _X2(n,20), _X2(n,21), _X2(n,22), _X2(n,23), \
		_X2(n,24), _X2(n,25), _X2(n,26), _X2(n,27), _X2(n,28), _X2(n,29), _X2(n,30), _X2(n,31)
	
	/*perm_expand*/ {
		#define _X2(n,k) (n==k) ? '=' : k-(n<k)
		_X(31), _X(30), _X(29), _X(28), _X(27), _X(26), _X(25), _X(24),
		_X(23), _X(22), _X(21), _X(20), _X(19), _X(18), _X(17), _X(16),
		_X(15), _X(14), _X(13), _X(12), _X(11), _X(10), _X( 9), _X( 8),
		_X( 7), _X( 6), _X( 5), _X( 4), _X( 3), _X( 2), _X( 1), _X( 0),
		_X(32)
		#undef _X2
	},
	
	// for SSE2 expanding
	#define _X2(n,k) n>k?-1:0
	#define _Y2(n, m) n==m ? '=' : 42+64*(n==m-1)
	#define _Y(n) _Y2(n,0), _Y2(n,1), _Y2(n,2), _Y2(n,3), _Y2(n,4), _Y2(n,5), _Y2(n,6), _Y2(n,7), \
		_Y2(n,8), _Y2(n,9), _Y2(n,10), _Y2(n,11), _Y2(n,12), _Y2(n,13), _Y2(n,14), _Y2(n,15), \
		_Y2(n,16), _Y2(n,17), _Y2(n,18), _Y2(n,19), _Y2(n,20), _Y2(n,21), _Y2(n,22), _Y2(n,23), \
		_Y2(n,24), _Y2(n,25), _Y2(n,26), _Y2(n,27), _Y2(n,28), _Y2(n,29), _Y2(n,30), _Y2(n,31)
	#define _XY(n) _X(n), _Y(n)
	
	#if defined(__LZCNT__) && defined(__tune_amdfam10__)
	/*expand_maskmix_lzc*/ {
		_XY(31), _XY(30), _XY(29), _XY(28), _XY(27), _XY(26), _XY(25), _XY(24),
		_XY(23), _XY(22), _XY(21), _XY(20), _XY(19), _XY(18), _XY(17), _XY(16),
		_XY(15), _XY(14), _XY(13), _XY(12), _XY(11), _XY(10), _XY( 9), _XY( 8),
		_XY( 7), _XY( 6), _XY( 5), _XY( 4), _XY( 3), _XY( 2), _XY( 1), _XY( 0),
		_XY(32)
	},
	#else
	/*expand_maskmix_bsr*/ {
		_XY(32),
		_XY( 0), _XY( 1), _XY( 2), _XY( 3), _XY( 4), _XY( 5), _XY( 6), _XY( 7),
		_XY( 8), _XY( 9), _XY(10), _XY(11), _XY(12), _XY(13), _XY(14), _XY(15),
		_XY(16), _XY(17), _XY(18), _XY(19), _XY(20), _XY(21), _XY(22), _XY(23),
		_XY(24), _XY(25), _XY(26), _XY(27), _XY(28), _XY(29), _XY(30), _XY(31)
	}
	#endif
	#undef _XY
	#undef _Y
	#undef _Y2
	#undef _X
	#undef _X2
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
					'='-42,'\0'-42,-128,-128,'\r'-42,' '-42,'\0'-42,'\n'-42,'\0'-42,'\r'-42,'.'-42,'\r'-42,'\n'-42,'\t'-42,'\n'-42,'\t'-42
				), _mm_adds_epi8(dataA, _mm_set1_epi8(120))),
				dataA
			);
			cmpB = _mm_cmpeq_epi8(
				_mm_shuffle_epi8(_mm_set_epi8(
					'='-42,'\0'-42,-128,-128,'\r'-42,' '-42,'\0'-42,'\n'-42,'\0'-42,'\r'-42,'.'-42,'\r'-42,'\n'-42,'\t'-42,'\n'-42,'\t'-42
				), _mm_adds_epi8(dataB, _mm_set1_epi8(120))),
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
		
		// likelihood of non-0 = 1-(63/64)^(sizeof(__m128i)*2)
		if (LIKELIHOOD(0.089, manyBitsSet)) {
			uint8_t m1 = maskA & 0xFF;
			uint8_t m2 = maskA >> 8;
			uint8_t m3 = maskB & 0xFF;
			uint8_t m4 = maskB >> 8;
			unsigned int shuf1Len, shuf2Len, shuf3Len, shufTotalLen;
			__m128i shuf1A, shuf1B, shuf2A, shuf2B; // only used for SSSE3 path
			__m128i data1A, data1B, data2A, data2B;
			
#if defined(__AVX512VBMI2__) && defined(__AVX512VL__) && defined(__AVX512BW__) && defined(__POPCNT__)
			if(use_isa >= ISA_LEVEL_VBMI2) {
				dataA = _mm_sub_epi8(dataA, _mm_ternarylogic_epi32(cmpA, _mm_set1_epi8(-42), _mm_set1_epi8(-42-64), 0xac));
				dataB = _mm_sub_epi8(dataB, _mm_ternarylogic_epi32(cmpB, _mm_set1_epi8(-42), _mm_set1_epi8(-42-64), 0xac));
				
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
				shuf2A = _mm_or_si128(shuf2A, _mm_set1_epi8(120));
				shuf2B = _mm_or_si128(shuf2B, _mm_set1_epi8(120));
				
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
				// TODO: optimize for 32-bit?
				uint64_t eqMask;
				// from experimentation, it doesn't seem like it's worth trying to branch here, i.e. it isn't worth trying to avoid a pmovmskb+shift+or by checking overflow amount
				if(use_isa >= ISA_LEVEL_VBMI2 || use_isa < ISA_LEVEL_SSSE3) {
					eqMask =
					  ((uint64_t)lookups.expandMask[m4] << shuf3Len)
					  | ((uint64_t)lookups.expandMask[m3] << shuf2Len)
					  | ((uint64_t)lookups.expandMask[m2] << shuf1Len)
					  | (uint64_t)lookups.expandMask[m1];
					
#if defined(__GNUC__) && defined(PLATFORM_AMD64)
					// be careful to avoid partial flag stalls on Intel P6 CPUs (SHR+ADC will likely stall)
# if !(defined(__tune_amdfam10__) || defined(__tune_k8__))
					if(use_isa >= ISA_LEVEL_VBMI2)
# endif
					{
						asm(
							"shrq $1, %[eqMask] \n"
							"shrq %%cl, %[eqMask] \n"
							"adcq %[col], %[p] \n"
							: [eqMask]"+r"(eqMask), [p]"+r"(p)
							: "c"(shufTotalLen - col - 1), [col]"r"(~col)
						);
					}
# if !(defined(__tune_amdfam10__) || defined(__tune_k8__))
					else
# else
					if(0)
# endif
#endif
					{
						eqMask >>= shufTotalLen - col -1;
						p -= col;
						if(LIKELIHOOD(0.98, (eqMask & 1) != 1))
							p--;
						else
							i++;
					}
				} else {
					eqMask =
					  ((uint64_t)_mm_movemask_epi8(shuf2B) << shuf3Len)
					  | ((uint64_t)_mm_movemask_epi8(shuf1B) << shuf2Len)
					  | ((uint64_t)_mm_movemask_epi8(shuf2A) << shuf1Len)
					  | (uint64_t)_mm_movemask_epi8(shuf1A);
					eqMask >>= shufTotalLen - col -1;
				}
				
				unsigned int bitCount;
#if defined(__POPCNT__)
				if(use_isa >= ISA_LEVEL_AVX) {
#ifdef PLATFORM_AMD64
					bitCount = (unsigned)_mm_popcnt_u64(eqMask);
#else
					bitCount = popcnt32(eqMask & 0xffffffff);
					bitCount += popcnt32(eqMask >> 32);
#endif
				} else
#endif
				{
					// TODO: consider using pshufb to do popcnt
					unsigned char cnt = lookups.BitsSetTable256plus8[eqMask & 0xff];
					cnt += lookups.BitsSetTable256plus8[(eqMask>>8) & 0xff];
					cnt += lookups.BitsSetTable256plus8[(eqMask>>16) & 0xff];
					cnt += lookups.BitsSetTable256plus8[(eqMask>>24) & 0xff];
					if(HEDLEY_UNLIKELY(eqMask >> 32)) {
						cnt += lookups.BitsSetTable256plus8[(eqMask>>32) & 0xff];
						cnt += lookups.BitsSetTable256plus8[(eqMask>>40) & 0xff];
						cnt += lookups.BitsSetTable256plus8[(eqMask>>48) & 0xff];
						cnt += lookups.BitsSetTable256plus8[(eqMask>>56) & 0xff];
						cnt -= 32;
					}
					bitCount = cnt-32;
				}
				if(use_isa >= ISA_LEVEL_VBMI2 || use_isa < ISA_LEVEL_SSSE3) {
					i -= bitCount;
				} else {
					i += bitCount;
					long revert = col + (eqMask & 1);
					p -= revert;
					i -= revert;
				}
				goto _encode_eol_handle_pre;
			}
		} else {
			if(_PREFER_BRANCHING && !mask) {
				// TODO: Atom _really_ prefers the add to be done before the `if`
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
#if defined(__AVX512VBMI__) && defined(__AVX512VBMI2__) && defined(__AVX512VL__) && defined(__AVX512BW__)
			if(use_isa >= ISA_LEVEL_VBMI2) {
				bitIndex = _lzcnt_u32(mask);
				dataA = _mm_sub_epi8(dataA, _mm_ternarylogic_epi32(cmpA, _mm_set1_epi8(-42), _mm_set1_epi8(-42-64), 0xac));
				dataB = _mm_sub_epi8(dataB, _mm_ternarylogic_epi32(cmpB, _mm_set1_epi8(-42), _mm_set1_epi8(-42-64), 0xac));
				dataB = _mm_mask2_permutex2var_epi8(
					dataA,
					_mm_load_si128((__m128i*)lookups.perm_expand + 1 + bitIndex*2),
					~maskB,
					dataB
				);
				dataA = _mm_mask_expand_epi8(_mm_set1_epi8('='), ~mask, dataA);
				/*
				dataA = _mm_mask_shuffle_epi8(
					_mm_set1_epi8('='),
					KNOT16(mask),
					dataA,
					_mm_maskz_expand_epi8( // TODO: see if loading is worthwhile
						KNOT16(mask), _mm_set_epi32(0x0f0e0d0c, 0x0b0a0908, 0x07060504, 0x03020100)
					)
				);
				*/
			} else
#endif
			{
				
#if !defined(__tune_btver1__)
				if(use_isa < ISA_LEVEL_AVX)
#endif
					maskBits = (mask != 0);
				if(_PREFER_BRANCHING) maskBits = 1;
				
				// shortcut for common case of only 1 bit set
#if defined(__LZCNT__) && defined(__tune_amdfam10__)
				// lzcnt is faster than bsf on AMD
				bitIndex = _lzcnt_u32(mask);
				const __m128i* entries = (const __m128i*)lookups.expand_maskmix_lzc;
				entries += bitIndex*4;
#else
				_BSR_VAR(bitIndex, mask);
				bitIndex |= maskBits-1; // if(mask == 0) bitIndex = -1;
				const __m128i* entries = (const __m128i*)lookups.expand_maskmix_bsr + 4;
				entries += bitIndex*4;
#endif
				
				// TODO: can first vector use shuffle instead of mask-blend?
				__m128i mergeMaskA = _mm_load_si128(entries+0);
				__m128i mergeMaskB = _mm_load_si128(entries+1);
				__m128i mixValsA = _mm_load_si128(entries+2);
				__m128i mixValsB = _mm_load_si128(entries+3);
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
				
#ifdef __AVX512VL__
				if(use_isa >= ISA_LEVEL_AVX3) {
					dataA = _mm_ternarylogic_epi32(dataAShifted, dataA, mergeMaskA, 0xf8);
					dataB = _mm_ternarylogic_epi32(dataBShifted, dataB, mergeMaskB, 0xf8);
					// TODO: consider blending in the '=' character too (don't need to pre-mask the shifted vec)
				} else
#endif
#ifdef __SSE4_1__
				if(use_isa >= ISA_LEVEL_AVX) {
					// BLENDV is horrible on Atom, so requiring AVX is likely worth it (exception: gimped Intel SKUs)
					dataA = _mm_blendv_epi8(dataAShifted, dataA, mergeMaskA);
					dataB = _mm_blendv_epi8(dataBShifted, dataB, mergeMaskB);
				} else
#endif
				{
					// alternatively `_mm_xor_si128(dataAMasked, dataA)` if compiler wants to load mergeMask* again
					dataB = _mm_or_si128(
						_mm_and_si128(mergeMaskB, dataB), dataBShifted
					);
					dataA = _mm_or_si128(
						_mm_and_si128(mergeMaskA, dataA), dataAShifted
					);
				}
				// add escape chars
				dataA = _mm_add_epi8(dataA, mixValsA);
				dataB = _mm_add_epi8(dataB, mixValsB);
			}
			
			// store main part
			STOREU_XMM(p, dataA);
			STOREU_XMM(p+XMM_SIZE, dataB);
			// store final char
			p[XMM_SIZE*2] = es[i-1] + 42 + (64 & (mask>>(XMM_SIZE*2-1-6)));
			
			p += XMM_SIZE*2 + maskBits;
			col += XMM_SIZE*2 + maskBits;
			
			if(LIKELIHOOD(0.3, col >= 0)) {
#if defined(__AVX512VBMI__) && defined(__AVX512VBMI2__) && defined(__AVX512VL__) && defined(__AVX512BW__)
				if(use_isa >= ISA_LEVEL_VBMI2)
					bitIndex = bitIndex +1;
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
							'='-42,'\0'-42,-128,-128,'\r'-42,' '-42,'\0'-42,'\n'-42,'\0'-42,'\r'-42,'.'-42,'\r'-42,'\n'-42,'\t'-42,'\n'-42,'\t'-42
						), _mm_adds_epi8(
							_mm_adds_epu8(dataA, _mm_set_epi8(
								0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,22
							)),
							_mm_set_epi8(
								120,120,120,120,120,120,120,120,120,120,120,120,120,120,120,91
							)
						)),
						dataA
					);
					maskA = _mm_movemask_epi8(cmpA);
					cmpB = _mm_cmpeq_epi8(
						_mm_shuffle_epi8(_mm_set_epi8(
							'='-42,'\0'-42,-128,-128,'\r'-42,' '-42,'\0'-42,'\n'-42,'\0'-42,'\r'-42,'.'-42,'\r'-42,'\n'-42,'\t'-42,'\n'-42,'\t'-42
						), _mm_adds_epi8(dataB, _mm_set1_epi8(120))),
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

