#include "common.h"

#include "encoder.h"
#include "encoder_common.h"



#pragma pack(16)
struct TShufMix {
	__m128i shuf, mix;
};
#pragma pack()

static struct {
	struct TShufMix ALIGN_TO(32, shufMix[256]);
	struct TShufMix ALIGN_TO(32, nlShufMix[256]);
	__m128i ALIGN_TO(16, mix[256]);
	__m128i ALIGN_TO(16, nlMix[256]);
	const unsigned char BitsSetTable256plus8[256];
	const uint16_t eolCharMask[512];
	uint16_t expandMask[256];
	
#if defined(__LZCNT__) && defined(__tune_amdfam10__)
	const int8_t ALIGN_TO(16, expand_maskmix_lzc[32*2*16]);
#else
	const int8_t ALIGN_TO(16, expand_maskmix_bsr[16*2*16]);
#endif
	
	const uint32_t ALIGN_TO(32, maskMixEOL[4*8]);
} lookups = {
	// shuf/mix lookups
	{}, {}, {}, {},
	/*BitsSetTable256plus8*/ {
		#   define B2(n) n+8,     n+9,     n+9,     n+10
		#   define B4(n) B2(n), B2(n+1), B2(n+1), B2(n+2)
		#   define B6(n) B4(n), B4(n+1), B4(n+1), B4(n+2)
		    B6(0), B6(1), B6(1), B6(2)
		#undef B2
		#undef B4
		#undef B6
	},
	/*eolCharMask*/ {
		#define _B1(n) _B(n), _B(n+1), _B(n+2), _B(n+3)
		#define _B2(n) _B1(n), _B1(n+4), _B1(n+8), _B1(n+12)
		#define _B3(n) _B2(n), _B2(n+16), _B2(n+32), _B2(n+48)
		#define _BX _B3(0), _B3(64), _B3(128), _B3(192)
		#define _B(n) ((n == 214+'\t' || n == 214+' ') ? 1 : 0)
			_BX,
		#undef _B
		#define _B(n) ((n == 214+'\t' || n == 214+' ' || n == '.'-42) ? 2 : 0)
			_BX
		#undef _B
		#undef _B1
		#undef _B2
		#undef _B3
		#undef _BX
	},
	/*expandMask*/ {},
	
	
	// for SSE2 expanding
	#define _X2(n,k) n>k?-1:0
	#define _X(n) _X2(n,0), _X2(n,1), _X2(n,2), _X2(n,3), _X2(n,4), _X2(n,5), _X2(n,6), _X2(n,7), _X2(n,8), _X2(n,9), _X2(n,10), _X2(n,11), _X2(n,12), _X2(n,13), _X2(n,14), _X2(n,15)
	#define _Y2(n, m) n==m ? '=' : 42+64*(n==m-1)
	#define _Y(n) _Y2(n,0), _Y2(n,1), _Y2(n,2), _Y2(n,3), _Y2(n,4), _Y2(n,5), _Y2(n,6), _Y2(n,7), \
		_Y2(n,8), _Y2(n,9), _Y2(n,10), _Y2(n,11), _Y2(n,12), _Y2(n,13), _Y2(n,14), _Y2(n,15)
	#define _XY(n) _X(n), _Y(n)
	
	#if defined(__LZCNT__) && defined(__tune_amdfam10__)
	/*expand_maskmix_lzc*/ {
		// 512 bytes of padding; this allows us to save doing a subtraction in-loop
		0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
		
		_XY(15), _XY(14), _XY(13), _XY(12), _XY(11), _XY(10), _XY( 9), _XY( 8),
		_XY( 7), _XY( 6), _XY( 5), _XY( 4), _XY( 3), _XY( 2), _XY( 1), _XY( 0)
	},
	#else
	/*expand_maskmix_bsr*/ {
		_XY( 0), _XY( 1), _XY( 2), _XY( 3), _XY( 4), _XY( 5), _XY( 6), _XY( 7),
		_XY( 8), _XY( 9), _XY(10), _XY(11), _XY(12), _XY(13), _XY(14), _XY(15)
	},
	#endif
	#undef _XY
	#undef _Y
	#undef _Y2
	#undef _X
	#undef _X2
	
	/*maskMixEOL*/ {
		0xff0000ff, 0x00000000, 0, 0,    0x2a0a0d2a, 0x00000000, 0, 0,
		0x0000ff00, 0x000000ff, 0, 0,    0x0a0d6a3d, 0x0000002a, 0, 0,
		0x000000ff, 0x000000ff, 0, 0,    0x3d0a0d2a, 0x0000006a, 0, 0,
		0x0000ff00, 0x0000ff00, 0, 0,    0x0a0d6a3d, 0x00006a3d, 0, 0
	}
};


static void encoder_sse_lut() {
	for(int i=0; i<256; i++) {
		int k = i;
		uint8_t* res = (uint8_t*)(&(lookups.shufMix[i].shuf));
		uint8_t* nlShuf = (uint8_t*)(&(lookups.nlShufMix[i].shuf));
		uint8_t* nlMix  = (uint8_t*)(&(lookups.nlShufMix[i].mix));
		uint16_t expand = 0;
		int p = 0, pNl = 0;
		for(int j=0; j<8; j++) {
			if(k & 1) {
				res[j+p] = 0xf0 + j;
				p++;
				if(j+pNl < 16) {
					nlShuf[j+pNl] = 0xf0 + j;
					nlMix[j+pNl] = '=';
					pNl++;
					if(j+pNl < 16) nlMix[j+pNl] = 64;
				}
			} else {
				if(j+pNl < 16) nlMix[j+pNl] = 0;
			}
			expand |= 1<<(j+p);
			res[j+p] = j;
			if(j+pNl < 16) nlShuf[j+pNl] = j;
			k >>= 1;
			
			if(j == 0) {
				// insert newline
				nlShuf[pNl+1] = 0xff;
				nlShuf[pNl+2] = 0xff;
				nlMix[pNl+1] = '\r';
				nlMix[pNl+2] = '\n';
				pNl += 2;
			}
		}
		for(; p<8; p++)
			res[8+p] = 8+p +0x40; // +0x40 is an arbitrary value to make debugging slightly easier?  the top bit cannot be set
		
		lookups.expandMask[i] = expand;
		
		// calculate add mask for mixing escape chars in
		__m128i shuf = _mm_load_si128((__m128i*)res);
		__m128i maskEsc = _mm_cmpeq_epi8(_mm_and_si128(shuf, _mm_set1_epi8(-16)), _mm_set1_epi8(-16)); // -16 == 0xf0
		__m128i addMask = _mm_and_si128(_mm_slli_si128(maskEsc, 1), _mm_set1_epi8(64));
		addMask = _mm_or_si128(addMask, _mm_and_si128(maskEsc, _mm_set1_epi8('=')));
		
		_mm_store_si128(&(lookups.shufMix[i].mix), addMask);
		_mm_store_si128(lookups.mix + i, _mm_add_epi8(
			addMask,
			_mm_andnot_si128(maskEsc, _mm_set1_epi8(42))
		));
		
		__m128i mix = _mm_load_si128((__m128i*)nlMix);
		__m128i plainChars = _mm_cmpeq_epi8(_mm_and_si128(mix, _mm_set1_epi8(255-64)), _mm_setzero_si128());
		_mm_store_si128(lookups.nlMix + i, _mm_add_epi8(
			mix,
			_mm_and_si128(plainChars, _mm_set1_epi8(42))
		));
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
		__m128i mergeMask = _mm_load_si128((const __m128i*)lookups.expand_maskmix_lzc + bitIndex*2);
		mask ^= 0x80000000U>>bitIndex;
#else
		// TODO: consider LUT for when BSR is slow
		unsigned long _BSR_VAR(bitIndex, mask);
		__m128i mergeMask = _mm_load_si128((const __m128i*)lookups.expand_maskmix_bsr + bitIndex*2);
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
static HEDLEY_ALWAYS_INLINE void encode_eol_handle_pre(const uint8_t* HEDLEY_RESTRICT es, long& i, uint8_t*& p, long& col, long lineSizeOffset) {
	__m128i oData = _mm_loadu_si128((__m128i *)(es + i));
	__m128i data = oData;
	if(use_isa >= ISA_LEVEL_SSSE3)
		data = _mm_add_epi8(oData, _mm_set1_epi8(42));
	__m128i cmp;
	unsigned int mask;
	
	// search for special chars
#if defined(__SSSE3__) && !defined(__tune_atom__) && !defined(__tune_silvermont__) && !defined(__tune_btver1__)
	// use shuffle to replace 3x cmpeq + ors; ideally, avoid on CPUs with slow shuffle
	if(use_isa >= ISA_LEVEL_SSSE3) {
#ifdef __AVX512VL__
		if(use_isa >= ISA_LEVEL_AVX3) {
			/* // alternative idea, using shuffle+pack, seems to be slower
			cmp = _mm_shuffle_epi8(_mm_set_epi8(
				//  \r     \n\t                 \0
				0,0,-1,0,0,-1,0,0,0,0,0,0,0,0,0,-1
			), _mm_min_epu8(
				data, _mm_set1_epi8(15)
			));
			cmp = _mm_ternarylogic_epi32(
				cmp,
				_mm_cmpeq_epi8(oData, _mm_set1_epi8('='-42)),
				_mm_cvtusepi32_epi8(_mm_cmpeq_epi8(
					_mm_shuffle_epi8(oData, _mm_set_epi32(
						-1,-1,0xff010101, 0xffff0000
					)),
					_mm_set_epi32(
						//                         .\t\s       \t\s
						0x13131313,0x13131313,0x1304DFF6,0x1313DFF6
					)
				)),
				0xFE
			);
			*/
			
			cmp = _mm_shuffle_epi8(_mm_set_epi32(
				//    \r       \n\t                    ..\0
				0x1313d613,0x13d6e313,0x13131313,0x131304d6
			), _mm_min_epu8(
				data, _mm_set1_epi8(15)
			));
			cmp = _mm_or_si128(
				_mm_cmpgt_epi8(_mm_set_epi32(0xe0e0e0e0,0xe0e0e0e0,0xe0e0e0e0,0xe0e0f6f6), cmp),
				_mm_ternarylogic_epi32(
					_mm_cmpeq_epi8(oData, _mm_set_epi32(0x1313d613,0x13d6e313,0x13131313,0x131304d6)), // .
					_mm_cmpeq_epi8(oData, _mm_set_epi32(0xe0e0e0e0,0xe0e0e0e0,0xe0e0e0e0,0xe0e0f6f6)), // \s
					_mm_cmpeq_epi8(oData, _mm_set1_epi8('='-42)),
					0xFE
				)
			);
			mask = _mm_movemask_epi8(cmp);
		} else
#endif
		{
			cmp = _mm_shuffle_epi8(_mm_set_epi8(
				//  \r     \n\t                 \0
				0,0,-1,0,0,-1,0,0,0,0,0,0,0,0,0,-1
			), _mm_min_epu8(
				data, _mm_set1_epi8(15)
			));
			cmp = _mm_or_si128(cmp, _mm_cmpeq_epi8(oData, _mm_set1_epi8('='-42)));
			mask = _mm_movemask_epi8(cmp);
			uint16_t lineChars = *(uint16_t*)(es + i);
			mask |= lookups.eolCharMask[lineChars & 0xff];
			mask |= lookups.eolCharMask[256 + ((lineChars>>8) & 0xff)];
		}
	} else
#endif
	{
		cmp = _mm_or_si128(
			_mm_or_si128(
				_mm_cmpeq_epi8(oData, _mm_set1_epi8(-42)),
				_mm_cmpeq_epi8(oData, _mm_set1_epi8('\n'-42))
			),
			_mm_or_si128(
				_mm_cmpeq_epi8(oData, _mm_set1_epi8('='-42)),
				_mm_cmpeq_epi8(oData, _mm_set1_epi8('\r'-42))
			)
		);
		mask = _mm_movemask_epi8(cmp);
		
		uint16_t lineChars = *(uint16_t*)(es + i);
		mask |= lookups.eolCharMask[lineChars & 0xff];
		mask |= lookups.eolCharMask[256 + ((lineChars>>8) & 0xff)];
	}
	
	if (LIKELIHOOD(0.2227, mask != 0)) {
		uint8_t m1 = mask & 0xFF;
		uint8_t m2 = mask >> 8;
		__m128i data1;
		
		unsigned int shufALen;
#if defined(__POPCNT__) && (defined(__tune_znver2__) || defined(__tune_znver1__) || defined(__tune_btver2__))
		if(use_isa >= ISA_LEVEL_AVX)
			shufALen = popcnt32(m1) + 10;
		else
#endif
			shufALen = lookups.BitsSetTable256plus8[m1] + 2;
		
		// TODO: is VBMI2 worthwhile here?
#ifdef __SSSE3__
		if(use_isa >= ISA_LEVEL_SSSE3) {
			__m128i shufMA = _mm_load_si128(&(lookups.nlShufMix[m1].shuf));
			data1 = _mm_shuffle_epi8(data, shufMA);
			// add in escaped chars
			__m128i shufMixMA = _mm_load_si128(&(lookups.nlShufMix[m1].mix));
			data1 = _mm_add_epi8(data1, shufMixMA);
		} else
#endif
		{
			if(LIKELIHOOD(0.001, shufALen > XMM_SIZE)) {
				// overflow case, only bother with first 2 chars
				data1 = _mm_unpacklo_epi8(oData, oData); // 0011xxxx
				data1 = _mm_shufflelo_epi16(data1, _MM_SHUFFLE(0,1,1,0)); // 00111100
				
				unsigned lut = (m1 & 3)*2;
				data1 = _mm_and_si128(data1, _mm_load_si128((const __m128i*)lookups.maskMixEOL + lut));
				data1 = _mm_add_epi8(data1, _mm_load_si128((const __m128i*)lookups.maskMixEOL + lut+1));
			} else {
				// regular expansion, except first byte
				data1 = sse2_expand_bytes(m1 & 0xfe, data);
				// expand for newline
				if(LIKELIHOOD(0.02, m1 & 1)) {
					data1 = _mm_slli_si128(data1, 3);
					data1 = _mm_shufflelo_epi16(data1, _MM_SHUFFLE(3,2,1,1));
					data1 = _mm_and_si128(data1, _mm_set_epi32(-1, -1, -1, 0x0000ffff));
				} else {
					data1 = _mm_slli_si128(data1, 2);
					data1 = _mm_shufflelo_epi16(data1, _MM_SHUFFLE(3,2,1,1));
					data1 = _mm_and_si128(data1, _mm_set_epi32(-1, -1, -1, 0xff0000ff));
				}
				
				__m128i shufMixMA = _mm_load_si128(lookups.nlMix + m1);
				data1 = _mm_add_epi8(data1, shufMixMA);
			}
		}
		
		if(LIKELIHOOD(0.001, shufALen > XMM_SIZE)) {
			// unlikely special case, which would cause vectors to be overflowed
			// we'll just handle this by only dealing with the first 2 characters, and let main loop handle the rest
			// at least one of the first 2 chars is guaranteed to need escaping
			_mm_storel_epi64((__m128i*)p, data1);
			col = lineSizeOffset + ((m1 & 2)>>1) - 16+2;
			p += 4 + (m1 & 1) + ((m1 & 2)>>1);
			i += 2;
			return;
		}
		
		__m128i data2;
		unsigned int shufTotalLen;
#if defined(__POPCNT__) && (defined(__tune_znver2__) || defined(__tune_znver1__) || defined(__tune_btver2__))
		if(use_isa >= ISA_LEVEL_AVX)
			shufTotalLen = popcnt32(mask) + 16;
		else
#endif
			shufTotalLen = shufALen + lookups.BitsSetTable256plus8[m2];
		
#ifdef __SSSE3__
		if(use_isa >= ISA_LEVEL_SSSE3) {
			__m128i shufMB = _mm_load_si128(&(lookups.shufMix[m2].shuf));
			shufMB = _mm_or_si128(shufMB, _mm_set1_epi8(8));
			data2 = _mm_shuffle_epi8(data, shufMB);
			__m128i shufMixMB = _mm_load_si128(&(lookups.shufMix[m2].mix));
			data2 = _mm_add_epi8(data2, shufMixMB);
		} else
#endif
		{
			data2 = sse2_expand_bytes(m2, _mm_srli_si128(data, 8));
			__m128i shufMixMB = _mm_load_si128(lookups.mix + m2);
			data2 = _mm_add_epi8(data2, shufMixMB);
		}
		
		STOREU_XMM(p, data1);
		STOREU_XMM(p+shufALen, data2);
		p += shufTotalLen;
		col = (long)(shufTotalLen - (m1&1)) + lineSizeOffset-16 -2;
	} else {
		__m128i data1;
#ifdef __SSSE3__
		if(use_isa >= ISA_LEVEL_SSSE3) {
			data1 = _mm_shuffle_epi8(data, _mm_set_epi8(
				13,12,11,10,9,8,7,6,5,4,3,2,1,-1,-1,0
			));
		} else
#endif
		{
			data = _mm_sub_epi8(oData, _mm_set1_epi8(-42));
			data1 = _mm_slli_si128(data, 2);
			data1 = _mm_shufflelo_epi16(data1, _MM_SHUFFLE(3,2,1,1));
			data1 = _mm_and_si128(data1, _mm_set_epi32(-1, -1, -1, 0xff0000ff));
		}
		data1 = _mm_or_si128(data1, _mm_cvtsi32_si128(0x0a0d00));
		STOREU_XMM(p, data1);
		*(uint16_t*)(p+XMM_SIZE) = _mm_extract_epi16(data, 7);
		p += XMM_SIZE+2;
		col = lineSizeOffset;
		
	}
	
	i += XMM_SIZE;
	// TODO: check col >= 0 if we want to support short lines
}


template<enum YEncDecIsaLevel use_isa>
static HEDLEY_ALWAYS_INLINE void do_encode_sse(int line_size, int* colOffset, const uint8_t* HEDLEY_RESTRICT srcEnd, uint8_t* HEDLEY_RESTRICT& dest, size_t& len) {
	if(len < XMM_SIZE*2 || line_size < XMM_SIZE*2) return;
	
	uint8_t *p = dest; // destination pointer
	long i = -(long)len; // input position
	long lineSizeOffset = -line_size +16; // line size plus vector length
	//long col = *colOffset - line_size +1; // for some reason, this causes GCC-8 to spill an extra register, causing the main loop to run ~5% slower, so use the alternative version below
	long col = *colOffset + lineSizeOffset - 15;
	
	// offset position to enable simpler loop condition checking
	const int INPUT_OFFSET = XMM_SIZE + XMM_SIZE -1; // EOL handling performs a full 128b read, -1 to change <= to <
	i += INPUT_OFFSET;
	const uint8_t* es = srcEnd - INPUT_OFFSET;
	
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
	if(LIKELIHOOD(0.001, col >= 0)) {
		if(col == 0)
			encode_eol_handle_pre<use_isa>(es, i, p, col, lineSizeOffset);
		else {
			uint8_t c = es[i++];
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
	while(i < 0) {
		__m128i oData = _mm_loadu_si128((__m128i *)(es + i)); // probably not worth the effort to align
		__m128i data = oData;
		if(use_isa >= ISA_LEVEL_SSSE3)
			data = _mm_add_epi8(oData, _mm_set1_epi8(42));
		
		i += XMM_SIZE;
		// search for special chars
		__m128i cmp;
#if defined(__SSSE3__) && !defined(__tune_atom__) && !defined(__tune_silvermont__) && !defined(__tune_btver1__)
		// use shuffle to replace 3x cmpeq + ors; ideally, avoid on CPUs with slow shuffle
		if(use_isa >= ISA_LEVEL_SSSE3) {
			cmp = _mm_or_si128(
				_mm_cmpeq_epi8(oData, _mm_set1_epi8('='-42)),
				_mm_shuffle_epi8(_mm_set_epi8(
					//  \r     \n                   \0
					0,0,-1,0,0,-1,0,0,0,0,0,0,0,0,0,-1
				), _mm_min_epu8(
					data, _mm_set1_epi8(15)
				))
			);
		} else
#endif
		{
			cmp = _mm_or_si128(
				_mm_or_si128(
					_mm_cmpeq_epi8(oData, _mm_set1_epi8(-42)),
					_mm_cmpeq_epi8(oData, _mm_set1_epi8('\n'-42))
				),
				_mm_or_si128(
					_mm_cmpeq_epi8(oData, _mm_set1_epi8('='-42)),
					_mm_cmpeq_epi8(oData, _mm_set1_epi8('\r'-42))
				)
			);
		}
		
		unsigned int mask = _mm_movemask_epi8(cmp);
		// likelihood of non-0 = 1-(63/64)^(sizeof(__m128i))
		if (LIKELIHOOD(0.2227, mask != 0)) { // seems to always be faster than _mm_test_all_zeros, possibly because http://stackoverflow.com/questions/34155897/simd-sse-how-to-check-that-all-vector-elements-are-non-zero#comment-62475316
			uint8_t m1 = mask & 0xFF;
			uint8_t m2 = mask >> 8;
			__m128i shufMA, shufMB; // only used for SSSE3 path
			__m128i data2;
			
#if defined(__AVX512VBMI2__) && defined(__AVX512VL__) && defined(__AVX512BW__) && defined(__POPCNT__)
			if(use_isa >= ISA_LEVEL_VBMI2) {
				data = _mm_mask_add_epi8(data, mask, data, _mm_set1_epi8(64));
				
				data2 = _mm_mask_expand_epi8(_mm_set1_epi8('='), lookups.expandMask[m2], _mm_srli_si128(data, 8));
				data = _mm_mask_expand_epi8(_mm_set1_epi8('='), lookups.expandMask[m1], data);
			} else
#endif
#ifdef __SSSE3__
			if(use_isa >= ISA_LEVEL_SSSE3) {
				// perform lookup for shuffle mask
				shufMA = _mm_load_si128(&(lookups.shufMix[m1].shuf));
				shufMB = _mm_load_si128(&(lookups.shufMix[m2].shuf));
				
				// second mask processes on second half, so add to the offsets
				shufMB = _mm_or_si128(shufMB, _mm_set1_epi8(8));
				
				// expand halves
				data2 = _mm_shuffle_epi8(data, shufMB);
				data = _mm_shuffle_epi8(data, shufMA);
				
				// add in escaped chars
				__m128i shufMixMA = _mm_load_si128(&(lookups.shufMix[m1].mix));
				__m128i shufMixMB = _mm_load_si128(&(lookups.shufMix[m2].mix));
				data = _mm_add_epi8(data, shufMixMA);
				data2 = _mm_add_epi8(data2, shufMixMB);
			} else
#endif
			{
				if(HEDLEY_UNLIKELY(mask & (mask-1))) {
					data2 = sse2_expand_bytes(m2, _mm_srli_si128(data, 8));
					data = sse2_expand_bytes(m1, data);
					// add in escaped chars
					__m128i shufMixMA = _mm_load_si128(lookups.mix + m1);
					__m128i shufMixMB = _mm_load_si128(lookups.mix + m2);
					data = _mm_add_epi8(data, shufMixMA);
					data2 = _mm_add_epi8(data2, shufMixMB);
				} else {
					// shortcut for common case of only 1 bit set
#if defined(__LZCNT__) && defined(__tune_amdfam10__)
					// lzcnt is faster than bsf on AMD
					long bitIndex = _lzcnt_u32(mask);
					__m128i mergeMask = _mm_load_si128((const __m128i*)lookups.expand_maskmix_lzc + bitIndex*2);
					__m128i mixVals = _mm_load_si128((const __m128i*)lookups.expand_maskmix_lzc + bitIndex*2 + 1);
#else
					long _BSR_VAR(bitIndex, mask);
					__m128i mergeMask = _mm_load_si128((const __m128i*)lookups.expand_maskmix_bsr + bitIndex*2);
					__m128i mixVals = _mm_load_si128((const __m128i*)lookups.expand_maskmix_bsr + bitIndex*2 + 1);
#endif
					data = _mm_or_si128(
						_mm_and_si128(mergeMask, data),
						_mm_slli_si128(_mm_andnot_si128(mergeMask, data), 1)
					);
					// add escape chars
					data = _mm_add_epi8(data, mixVals);
					
					// store main part
					STOREU_XMM(p, data);
					// store final char
					p[XMM_SIZE] = es[i-1] + 42 + (64 & (mask>>(XMM_SIZE-1-6)));
					
					p += XMM_SIZE + 1;
					col += XMM_SIZE + 1;
					
					if(LIKELIHOOD(0.15, col >= 0)) {
#if defined(__LZCNT__) && defined(__tune_amdfam10__)
						bitIndex = bitIndex-16 +1;
#else
						bitIndex = 15-bitIndex +1;
#endif
						if(HEDLEY_UNLIKELY(col == bitIndex)) {
							// this is an escape character, so line will need to overflow
							p--;
						} else {
							i += (col > bitIndex);
						}
						p -= col;
						i -= col;
						goto _encode_eol_handle_pre;
					}
					continue;
				}
				
			}
			
			// store out
			unsigned int shufALen, shufTotalLen;
#if defined(__POPCNT__) && (defined(__tune_znver2__) || defined(__tune_znver1__) || defined(__tune_btver2__))
			if(use_isa >= ISA_LEVEL_AVX) {
				shufALen = popcnt32(m1) + 8;
				shufTotalLen = popcnt32(mask) + 16;
			} else
#endif
			{
				shufALen = lookups.BitsSetTable256plus8[m1];
				shufTotalLen = shufALen + lookups.BitsSetTable256plus8[m2];
			}
			STOREU_XMM(p, data);
			STOREU_XMM(p+shufALen, data2);
			p += shufTotalLen;
			col += shufTotalLen;
			
			if(LIKELIHOOD(0.15 /*guess, using 128b lines*/, col >= 0)) {
				uint32_t eqMask;
				// from experimentation, it doesn't seem like it's worth trying to branch here, i.e. it isn't worth trying to avoid a pmovmskb+shift+or by checking overflow amount
				if(use_isa >= ISA_LEVEL_VBMI2 || use_isa < ISA_LEVEL_SSSE3) {
					eqMask = (lookups.expandMask[m2] << shufALen) | lookups.expandMask[m1];
				} else {
					eqMask = ((unsigned)_mm_movemask_epi8(shufMB) << shufALen) | (unsigned)_mm_movemask_epi8(shufMA);
				}
				eqMask >>= shufTotalLen - col -1;
				
				unsigned int bitCount;
#if defined(__POPCNT__)
				if(use_isa >= ISA_LEVEL_AVX)
					bitCount = popcnt32(eqMask);
				else
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
					p -= col;
					if(LIKELIHOOD(0.98, (eqMask & 1) != 1))
						p--;
					else
						i++;
				} else {
					i += bitCount;
					long revert = col + (eqMask & 1);
					p -= revert;
					i -= revert;
				}
				goto _encode_eol_handle_pre;
			}
		} else {
			if(use_isa < ISA_LEVEL_SSSE3)
				data = _mm_sub_epi8(oData, _mm_set1_epi8(-42));
			STOREU_XMM(p, data);
			p += XMM_SIZE;
			col += XMM_SIZE;
			if(LIKELIHOOD(0.15, col >= 0)) {
				p -= col;
				i -= col;
				
				_encode_eol_handle_pre:
				encode_eol_handle_pre<use_isa>(es, i, p, col, lineSizeOffset);
			}
		}
	}
	
	*colOffset = col + line_size -1;
	dest = p;
	len = -(i - INPUT_OFFSET);
}

