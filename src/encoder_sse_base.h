#include "common.h"

// slightly faster version which improves the worst case scenario significantly; since worst case doesn't happen often, overall speedup is relatively minor
// requires PSHUFB (SSSE3) instruction, but will use POPCNT (SSE4.2 (or AMD's ABM, but Phenom doesn't support SSSE3 so doesn't matter)) if available (these only seem to give minor speedups, so considered optional)
#include "encoder.h"
#include "encoder_common.h"

#pragma pack(16)
struct TShufMix {
	__m128i shuf, mix;
};
#pragma pack()
static struct TShufMix ALIGN_TO(32, shufMixLUT[256]);

static uint16_t expandLUT[256];

static void encoder_sse_lut() {
	for(int i=0; i<256; i++) {
		int k = i;
		uint8_t res[16];
		uint16_t expand = 0;
		int p = 0;
		for(int j=0; j<8; j++) {
			if(k & 1) {
				res[j+p] = 0xf0 + j;
				p++;
			}
			res[j+p] = j;
			expand |= 1<<(j+p);
			k >>= 1;
		}
		for(; p<8; p++)
			res[8+p] = 8+p +0x40; // +0x40 is an arbitrary value to make debugging slightly easier?  the top bit cannot be set
		
		__m128i shuf = _mm_loadu_si128((__m128i*)res);
		_mm_store_si128(&(shufMixLUT[i].shuf), shuf);
		expandLUT[i] = expand;
		
		// calculate add mask for mixing escape chars in
		__m128i maskEsc = _mm_cmpeq_epi8(_mm_and_si128(shuf, _mm_set1_epi8(-16)), _mm_set1_epi8(-16)); // -16 == 0xf0
		__m128i addMask = _mm_and_si128(_mm_slli_si128(maskEsc, 1), _mm_set1_epi8(64));
		addMask = _mm_or_si128(addMask, _mm_and_si128(maskEsc, _mm_set1_epi8('=')));
		
		_mm_store_si128(&(shufMixLUT[i].mix), addMask);
	}
}

// for SSE2 expanding
#define _X2(n,k) n>k?-1:0
#define _X(n) _X2(n,0), _X2(n,1), _X2(n,2), _X2(n,3), _X2(n,4), _X2(n,5), _X2(n,6), _X2(n,7), _X2(n,8), _X2(n,9), _X2(n,10), _X2(n,11), _X2(n,12), _X2(n,13), _X2(n,14), _X2(n,15)
#define _Y2(n, m) '='*(n==m) + 64*(n==m-1)
#define _Y(n) _Y2(n,0), _Y2(n,1), _Y2(n,2), _Y2(n,3), _Y2(n,4), _Y2(n,5), _Y2(n,6), _Y2(n,7), \
	_Y2(n,8), _Y2(n,9), _Y2(n,10), _Y2(n,11), _Y2(n,12), _Y2(n,13), _Y2(n,14), _Y2(n,15)
#define _XY(n) _X(n), _Y(n)

#if defined(__LZCNT__) && defined(__tune_amdfam10__)
static const int8_t ALIGN_TO(16, _expand_maskmix_lzc_table[32*32]) = {
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
};
static const __m128i* expand_maskmix_lzc_table = (const __m128i*)_expand_maskmix_lzc_table;
#else
static const int8_t ALIGN_TO(16, _expand_maskmix_table[16*32]) = {
	_XY( 0), _XY( 1), _XY( 2), _XY( 3), _XY( 4), _XY( 5), _XY( 6), _XY( 7),
	_XY( 8), _XY( 9), _XY(10), _XY(11), _XY(12), _XY(13), _XY(14), _XY(15)
};
static const __m128i* expand_maskmix_table = (const __m128i*)_expand_maskmix_table;
#endif
#undef _XY
#undef _Y
#undef _Y2
#undef _X
#undef _X2

// for LZCNT/BSF
#ifdef _MSC_VER
# include <intrin.h>
# include <ammintrin.h>
# define _BSR_VAR(var, src) var; _BitScanReverse(&var, src)
#else
# include <x86intrin.h>
# define _BSR_VAR(var, src) var = _bit_scan_reverse(src)
#endif

static HEDLEY_ALWAYS_INLINE __m128i sse2_expand_bytes(int mask, __m128i data) {
	while(mask) {
		// get highest bit
#if defined(__LZCNT__) && defined(__tune_amdfam10__)
		unsigned long bitIndex = _lzcnt_u32(mask);
		__m128i mergeMask = _mm_load_si128(expand_maskmix_lzc_table + bitIndex*2);
		mask ^= 0x80000000U>>bitIndex;
#else
		// TODO: consider LUT for when BSR is slow
		unsigned long _BSR_VAR(bitIndex, mask);
		__m128i mergeMask = _mm_load_si128(expand_maskmix_table + bitIndex*2);
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


static HEDLEY_ALWAYS_INLINE void encode_eol_handle_post(const uint8_t* HEDLEY_RESTRICT es, long& i, uint8_t*& p, int& col) {
	uint8_t c = es[i++];
	if (LIKELIHOOD(0.0273, escapedLUT[c]!=0)) {
		*(uint32_t*)p = UINT32_16_PACK(UINT16_PACK('\r', '\n'), (uint32_t)escapedLUT[c]);
		p += 4;
		col = 2;
	} else {
		*(uint32_t*)p = UINT32_PACK('\r', '\n', (uint32_t)(c+42), 0);
		p += 3;
		col = 1;
	}
}

static const uint32_t ALIGN_TO(32, _maskMix_eol_table[4*32]) = {
	0xff0000ff, 0x00000000, 0, 0,    0x2a0a0d2a, 0x00000000, 0, 0,
	0x0000ff00, 0x000000ff, 0, 0,    0x0a0d6a3d, 0x0000002a, 0, 0,
	0x000000ff, 0x000000ff, 0, 0,    0x3d0a0d2a, 0x0000006a, 0, 0,
	0x0000ff00, 0x0000ff00, 0, 0,    0x0a0d6a3d, 0x00006a3d, 0, 0
};
static struct TShufMix* maskMixEOL = (struct TShufMix*)_maskMix_eol_table;

template<enum YEncDecIsaLevel use_isa>
static HEDLEY_ALWAYS_INLINE void encode_eol_handle_pre(const uint8_t* HEDLEY_RESTRICT es, long& i, uint8_t*& p, int& col) {
	// load 2 bytes & broadcast
	__m128i lineChars = _mm_cvtsi32_si128(*(uint16_t*)(es+i)); // 01xxxxxx
#if defined(__SSSE3__) && !defined(__tune_atom__) && !defined(__tune_silvermont__) && !defined(__tune_btver1__)
	if(use_isa >= ISA_LEVEL_SSSE3)
		lineChars = _mm_shuffle_epi8(lineChars, _mm_set1_epi64x(0x0000010101010000ULL));
	else
#endif
	{
		lineChars = _mm_unpacklo_epi8(lineChars, lineChars); // 0011xxxx
		lineChars = _mm_shufflelo_epi16(lineChars, _MM_SHUFFLE(0,1,1,0)); // 00111100
		lineChars = _mm_unpacklo_epi64(lineChars, lineChars); // duplicate to upper half
	}
	// pattern is now 00111100 00111100
	unsigned testChars = _mm_movemask_epi8(_mm_cmpeq_epi8(
		lineChars,
		_mm_set_epi16(
			// 0 0     1 1     1 1     0 0     0 0     1 1     1 1     0 0
			//xxxx    xx .    \0\r    \0\r    \n =    \n =    \t\s    \t\s
			0xdfdf, 0xdf04, 0xd6e3, 0xd6e3, 0xe013, 0xe013, 0xdff6, 0xdff6
		)
	));
	if(testChars) {
		unsigned esc1stChar = (testChars & 0x03c3) != 0;
		unsigned esc2ndChar = (testChars & 0x1c3c) != 0;
		unsigned lut = esc1stChar + esc2ndChar*2;
		lineChars = _mm_and_si128(lineChars, _mm_load_si128(&(maskMixEOL[lut].shuf)));
		lineChars = _mm_add_epi8(lineChars, _mm_load_si128(&(maskMixEOL[lut].mix)));
		_mm_storel_epi64((__m128i*)p, lineChars);
		col = 1 + esc2ndChar;
		p += 4 + esc1stChar + esc2ndChar;
	} else {
		lineChars = _mm_and_si128(lineChars, _mm_load_si128(&(maskMixEOL[0].shuf)));
		lineChars = _mm_add_epi8(lineChars, _mm_load_si128(&(maskMixEOL[0].mix)));
		*(int*)p = _mm_cvtsi128_si32(lineChars);
		col = 1;
		p += 4;
	}
	i += 2;
}


template<enum YEncDecIsaLevel use_isa>
static HEDLEY_ALWAYS_INLINE void do_encode_sse(int line_size, int* colOffset, const uint8_t* HEDLEY_RESTRICT srcEnd, uint8_t* HEDLEY_RESTRICT& dest, size_t& len) {
	if(len < 2 || line_size < 4) return;
	
	uint8_t *p = dest; // destination pointer
	long i = -(long)len; // input position
	int col = *colOffset;
	
	// offset position to enable simpler loop condition checking
	const int INPUT_OFFSET = XMM_SIZE + 2 -1; // extra 2 chars for EOL handling, -1 to change <= to <
	i += INPUT_OFFSET;
	const uint8_t* es = srcEnd - INPUT_OFFSET;
	
	if (LIKELIHOOD(0.999, col == 0)) {
		uint8_t c = es[i++];
		if (LIKELIHOOD(0.0273, escapedLUT[c] != 0)) {
			*(uint16_t*)p = escapedLUT[c];
			p += 2;
			col = 2;
		} else {
			*(p++) = c + 42;
			col = 1;
		}
	}
	if(LIKELIHOOD(0.001, col >= line_size-1)) {
		if(col == line_size-1)
			encode_eol_handle_pre<use_isa>(es, i, p, col);
		else
			encode_eol_handle_post(es, i, p, col);
	}
	while(i < 0) {
		__m128i oData = _mm_loadu_si128((__m128i *)(es + i)); // probably not worth the effort to align
		__m128i data = _mm_add_epi8(oData, _mm_set1_epi8(42));
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
				), _mm_adds_epu8(
					data, _mm_set1_epi8(0x70)
				))
			);
		} else
#endif
		{
			cmp = _mm_or_si128(
				_mm_or_si128(
					_mm_cmpeq_epi8(data, _mm_setzero_si128()),
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
			__m128i shufMA, shufMB;
			__m128i data2;
			
#ifdef __SSSE3__
			if(use_isa >= ISA_LEVEL_SSSE3) {
# if defined(__AVX512VBMI2__) && defined(__AVX512VL__) && defined(__AVX512BW__) && 0
				if(use_isa >= ISA_LEVEL_VBMI2) {
					data = _mm_mask_add_epi8(data, mask, data, _mm_set1_epi8(64));
					
					// TODO: will need to see if this is actually faster; vpexpand* is rather slow on SKX, even for 128b ops, so this could be slower
					// on SKX, mask-shuffle is faster than expand, but requires loading shuffle masks, and ends up being slower than the SSSE3 method
					data2 = _mm_mask_expand_epi8(_mm_set1_epi8('='), expandLUT[m2], _mm_srli_si128(data, 8));
					data = _mm_mask_expand_epi8(_mm_set1_epi8('='), expandLUT[m1], data);
					
					/*
					// other idea, using expand/compact
					// expand compactMask
					uint32_t compactMask = _pdep_u32(mask, 0x55555555); // could also use pclmul
					compactMask |= 0xaaaaaaaa; // set all high bits to keep them
					
#  if 0
					// uses 256b instructions, but probably more efficient
					__m256i paddedData = _mm256_permute4x64_epi64(_mm256_castsi128_si256(data), 0x50);
					paddedData = _mm256_unpacklo_epi8(_mm256_set1_epi8('='), paddedData);
					_mm256_mask_compressstoreu_epi8(p, compactMask, paddedData);
					
					unsigned int bytes = popcnt32(mask) + 16;
					p += bytes;
					col += bytes;
					
					ovrflowAmt = col - (line_size-1);
					if(ovrflowAmt >= 0) {
#   if (defined(__tune_znver2__) || defined(__tune_znver1__) || defined(__tune_btver2__))
						shufALen = popcnt32(m1) + 8;
						shufBLen = popcnt32(m2) + 8;
#   else
						shufALen = BitsSetTable256plus8[m1];
						shufBLen = BitsSetTable256plus8[m2];
#   endif
					}
#  else
					__m128i data1 = _mm_unpacklo_epi8(_mm_set1_epi8('='), data);
					data2 = _mm_unpackhi_epi8(_mm_set1_epi8('='), data);
#   if (defined(__tune_znver2__) || defined(__tune_znver1__) || defined(__tune_btver2__))
					shufALen = popcnt32(m1) + 8;
					shufBLen = popcnt32(m2) + 8;
#   else
					shufALen = BitsSetTable256plus8[m1];
					shufBLen = BitsSetTable256plus8[m2];
#   endif
					_mm_mask_compressstoreu_epi8(p, compactMask & 0xffff, data1);
					p += shufALen;
					_mm_mask_compressstoreu_epi8(p, compactMask >> 16, data2);
					p += shufBLen;
					col += shufALen + shufBLen;
					
					ovrflowAmt = col - (line_size-1);
#  endif

					// TODO: improve overflow handling - do we need to calculate all this?
					if(ovrflowAmt >= 0) { // calculate parts if overflowed
						m1 = mask & 0xFF;
						m2 = mask >> 8;
					}
					*/
				} else
# endif
				{
					// perform lookup for shuffle mask
					shufMA = _mm_load_si128(&(shufMixLUT[m1].shuf));
					shufMB = _mm_load_si128(&(shufMixLUT[m2].shuf));
					
					// second mask processes on second half, so add to the offsets
					shufMB = _mm_or_si128(shufMB, _mm_set1_epi8(8));
					
					// expand halves
					data2 = _mm_shuffle_epi8(data, shufMB);
					data = _mm_shuffle_epi8(data, shufMA);
					
					// add in escaped chars
					__m128i shufMixMA = _mm_load_si128(&(shufMixLUT[m1].mix));
					__m128i shufMixMB = _mm_load_si128(&(shufMixLUT[m2].mix));
					data = _mm_add_epi8(data, shufMixMA);
					data2 = _mm_add_epi8(data2, shufMixMB);
				}
			} else
#endif
			{
				if(mask & (mask-1)) {
					data2 = sse2_expand_bytes(m2, _mm_srli_si128(data, 8));
					data = sse2_expand_bytes(m1, data);
					// add in escaped chars
					__m128i shufMixMA = _mm_load_si128(&(shufMixLUT[m1].mix));
					__m128i shufMixMB = _mm_load_si128(&(shufMixLUT[m2].mix));
					data = _mm_add_epi8(data, shufMixMA);
					data2 = _mm_add_epi8(data2, shufMixMB);
				} else {
					// shortcut for common case of only 1 bit set
#if defined(__LZCNT__) && defined(__tune_amdfam10__)
					// lzcnt is faster than bsf on AMD
					unsigned long bitIndex = _lzcnt_u32(mask);
					__m128i mergeMask = _mm_load_si128(expand_maskmix_lzc_table + bitIndex*2);
					__m128i mixVals = _mm_load_si128(expand_maskmix_lzc_table + bitIndex*2 + 1);
#else
					unsigned long _BSR_VAR(bitIndex, mask);
					__m128i mergeMask = _mm_load_si128(expand_maskmix_table + bitIndex*2);
					__m128i mixVals = _mm_load_si128(expand_maskmix_table + bitIndex*2 + 1);
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
					
					int ovrflowAmt = col - (line_size-1);
					if(LIKELIHOOD(0.15, ovrflowAmt >= 0)) {
#if defined(__LZCNT__) && defined(__tune_amdfam10__)
						bitIndex = bitIndex-16;
#else
						bitIndex = 15-bitIndex;
#endif
						if((unsigned int)ovrflowAmt-1 == bitIndex) {
							// this is an escape character, so line will need to overflow
							p -= ovrflowAmt - 1;
							i -= ovrflowAmt - 1;
							encode_eol_handle_post(es, i, p, col);
						} else {
							int overflowedPastEsc = (unsigned int)(ovrflowAmt-1) > (unsigned int)bitIndex;
							p -= ovrflowAmt;
							i -= ovrflowAmt - overflowedPastEsc;
							encode_eol_handle_pre<use_isa>(es, i, p, col);
						}
					}
					continue;
				}
				
			}
			
			// store out
			unsigned char shufALen, shufBLen;
# if defined(__POPCNT__) && (defined(__tune_znver2__) || defined(__tune_znver1__) || defined(__tune_btver2__))
			if(use_isa >= ISA_LEVEL_AVX) {
				shufALen = popcnt32(m1) + 8;
				shufBLen = popcnt32(m2) + 8;
			} else
# endif
			{
				shufALen = BitsSetTable256plus8[m1];
				shufBLen = BitsSetTable256plus8[m2];
			}
			STOREU_XMM(p, data);
			p += shufALen;
			STOREU_XMM(p, data2);
			p += shufBLen;
			col += shufALen + shufBLen;
			
			int ovrflowAmt = col - (line_size-1);
			if(LIKELIHOOD(0.15 /*guess, using 128b lines*/, ovrflowAmt >= 0)) {
# if defined(__POPCNT__)
				if(use_isa >= ISA_LEVEL_AVX) {
					// from experimentation, it doesn't seem like it's worth trying to branch here, i.e. it isn't worth trying to avoid a pmovmskb+shift+or by checking overflow amount
					uint32_t eqMask = (_mm_movemask_epi8(shufMB) << shufALen) | _mm_movemask_epi8(shufMA);
					eqMask >>= shufBLen+shufALen - ovrflowAmt -1;
					i -= ovrflowAmt - popcnt32(eqMask);
					p -= ovrflowAmt - (eqMask & 1);
					if(eqMask & 1)
						encode_eol_handle_post(es, i, p, col);
					else
						encode_eol_handle_pre<use_isa>(es, i, p, col);
				} else
# endif
				{
					// we overflowed - find correct position to revert back to
					p -= ovrflowAmt;
					if(ovrflowAmt == shufBLen) {
						i -= 8;
					} else if(ovrflowAmt != 0) {
						int isEsc;
						uint16_t tst;
						int midPointOffset = ovrflowAmt - shufBLen +1;
						if(ovrflowAmt > shufBLen) {
							// `shufALen - midPointOffset` expands to `shufALen + shufBLen - ovrflowAmt -1`
							// since `shufALen + shufBLen` > ovrflowAmt is implied (i.e. you can't overflow more than you've added)
							// ...the expression cannot underflow, and cannot exceed 14
							tst = *(uint16_t*)((char*)(&(shufMixLUT[m1].shuf)) + shufALen - midPointOffset);
							i -= 8;
						} else { // i.e. ovrflowAmt < shufBLen (== case handled above)
							// -14 <= midPointOffset <= 0, should be true here
							tst = *(uint16_t*)((char*)(&(shufMixLUT[m2].shuf)) - midPointOffset);
						}
						isEsc = (0xf0 == (tst&0xF0));
						p += isEsc;
						i -= 8 - ((tst>>8)&0xf) - isEsc;
						//col = line_size-1 + isEsc; // doesn't need to be set, since it's never read again
						if(isEsc) {
							encode_eol_handle_post(es, i, p, col);
							continue;
						}
					}
					encode_eol_handle_pre<use_isa>(es, i, p, col);
				}
			}
		} else {
			STOREU_XMM(p, data);
			p += XMM_SIZE;
			col += XMM_SIZE;
			if(LIKELIHOOD(0.15, col >= line_size-1)) {
				p -= col - (line_size-1);
				i -= col - (line_size-1);
				//col = line_size-1; // doesn't need to be set, since it's never read again
				encode_eol_handle_pre<use_isa>(es, i, p, col);
			}
		}
	}
	
	*colOffset = col;
	dest = p;
	len = -(i - INPUT_OFFSET);
}

