#include "common.h"
#ifdef __AVX2__

#include "encoder.h"
#include "encoder_common.h"
#define YMM_SIZE 32

#if defined(__x86_64__) || \
    defined(__amd64__ ) || \
    defined(__LP64    ) || \
    defined(_M_X64    ) || \
    defined(_M_AMD64  ) || \
    defined(_WIN64    )
# define PLATFORM_AMD64 1
#endif


static const int8_t ALIGN_TO(64, _expand_mergemix_table[33*32*2]) = {
#define _X2(n,k) n>k?-1:0
#define _X(n) _X2(n,0), _X2(n,1), _X2(n,2), _X2(n,3), _X2(n,4), _X2(n,5), _X2(n,6), _X2(n,7), \
	_X2(n,8), _X2(n,9), _X2(n,10), _X2(n,11), _X2(n,12), _X2(n,13), _X2(n,14), _X2(n,15), \
	_X2(n,16), _X2(n,17), _X2(n,18), _X2(n,19), _X2(n,20), _X2(n,21), _X2(n,22), _X2(n,23), \
	_X2(n,24), _X2(n,25), _X2(n,26), _X2(n,27), _X2(n,28), _X2(n,29), _X2(n,30), _X2(n,31)
#define _Y2(n, m) '='*(n==m) + 64*(n==m-1)
#define _Y(n) _Y2(n,0), _Y2(n,1), _Y2(n,2), _Y2(n,3), _Y2(n,4), _Y2(n,5), _Y2(n,6), _Y2(n,7), \
	_Y2(n,8), _Y2(n,9), _Y2(n,10), _Y2(n,11), _Y2(n,12), _Y2(n,13), _Y2(n,14), _Y2(n,15), \
	_Y2(n,16), _Y2(n,17), _Y2(n,18), _Y2(n,19), _Y2(n,20), _Y2(n,21), _Y2(n,22), _Y2(n,23), \
	_Y2(n,24), _Y2(n,25), _Y2(n,26), _Y2(n,27), _Y2(n,28), _Y2(n,29), _Y2(n,30), _Y2(n,31)
#define _XY(n) _X(n), _Y(n)
	_XY(31), _XY(30), _XY(29), _XY(28), _XY(27), _XY(26), _XY(25), _XY(24),
	_XY(23), _XY(22), _XY(21), _XY(20), _XY(19), _XY(18), _XY(17), _XY(16),
	_XY(15), _XY(14), _XY(13), _XY(12), _XY(11), _XY(10), _XY( 9), _XY( 8),
	_XY( 7), _XY( 6), _XY( 5), _XY( 4), _XY( 3), _XY( 2), _XY( 1), _XY( 0),
	_XY(32)
#undef _XY
#undef _Y
#undef _Y2
#undef _X
#undef _X2
};
static const __m256i* expand_mergemix_table = (const __m256i*)_expand_mergemix_table;


#if defined(__AVX512VBMI2__) && defined(__AVX512VL__) && defined(__AVX512BW__)
static uint32_t expandLUT[65536]; // biggish 256KB table (but still smaller than the 2MB table)
static void encoder_avx_vbmi2_lut() {
	for(int i=0; i<65536; i++) {
		int k = i;
		uint32_t expand = 0;
		int p = 0;
		for(int j=0; j<16; j++) {
			if(k & 1) {
				p++;
			}
			expand |= 1<<(j+p);
			k >>= 1;
		}
		expandLUT[i] = expand;
	}
}
#endif

static __m256i ALIGN_TO(32, shufExpandLUT[65536]); // huge 2MB table
static void encoder_avx2_lut() {
	for(int i=0; i<65536; i++) {
		int k = i;
		uint8_t* res = (uint8_t*)(shufExpandLUT + i);
		int p = 0;
		for(int j=0; j<16; j++) {
			if(k & 1) {
				res[j+p] = 0x70+'=';
				p++;
			}
			res[j+p] = j;
			k >>= 1;
		}
		for(; p<16; p++)
			res[16+p] = 0x40; // arbitrary value (top bit cannot be set)
	}
}


#pragma pack(16)
struct TShufMix {
	__m128i shuf, mix;
};
#pragma pack()

static const uint32_t ALIGN_TO(32, _maskMix_eol_table[4*32]) = {
	// first row is an AND mask, rest is PSHUFB table
	0xff0000ff, 0x00000000, 0, 0,    0x2a0a0d2a, 0x00000000, 0, 0,
	
	0xffff00ff, 0xffffff01, 0, 0,    0x0a0d6a3d, 0x0000002a, 0, 0,
	0xffffff00, 0xffffff01, 0, 0,    0x3d0a0d2a, 0x0000006a, 0, 0,
	0xffff00ff, 0xffff01ff, 0, 0,    0x0a0d6a3d, 0x00006a3d, 0, 0
};
static struct TShufMix* maskMixEOL = (struct TShufMix*)_maskMix_eol_table;

static HEDLEY_ALWAYS_INLINE void encode_eol_handle_pre(const uint8_t* HEDLEY_RESTRICT es, long& i, uint8_t*& p, long& col, long lineSizeOffset) {
	__m128i lineChars = _mm_set1_epi16(*(uint16_t*)(es+i)); // unfortunately, _mm_broadcastw_epi16 requires __m128i argument
	unsigned testChars = _mm_movemask_epi8(_mm_cmpeq_epi8(
		lineChars,
		_mm_set_epi16(
			// xxxx      .xx     \0\0     \r\r     \n\n      = =     \t\t     \s\s
			-0x2021,  0x04df, -0x292A, -0x1C1D, -0x1F20,  0x1313, -0x2021, -0x090A
		)
	));
	if(HEDLEY_UNLIKELY(testChars)) {
		unsigned esc1stChar = (testChars & 0x5555) != 0;
		unsigned esc2ndChar = (testChars & 0xaaaa) != 0;
		unsigned lut = esc1stChar + esc2ndChar*2;
		lineChars = _mm_shuffle_epi8(lineChars, _mm_load_si128(&(maskMixEOL[lut].shuf)));
		lineChars = _mm_add_epi8(lineChars, _mm_load_si128(&(maskMixEOL[lut].mix)));
		_mm_storel_epi64((__m128i*)p, lineChars);
		col = lineSizeOffset + esc2ndChar;
		p += 4 + esc1stChar + esc2ndChar;
	} else {
		lineChars = _mm_and_si128(lineChars, _mm_cvtsi32_si128(0xff0000ff));
		lineChars = _mm_add_epi8(lineChars, _mm_cvtsi32_si128(0x2a0a0d2a));
		*(int*)p = _mm_cvtsi128_si32(lineChars);
		col = lineSizeOffset;
		p += 4;
	}
	i += 2;
}

template<enum YEncDecIsaLevel use_isa>
static HEDLEY_ALWAYS_INLINE void do_encode_avx2(int line_size, int* colOffset, const uint8_t* HEDLEY_RESTRICT srcEnd, uint8_t* HEDLEY_RESTRICT& dest, size_t& len) {
	if(len < 2 || line_size < 4) return;
	
	uint8_t *p = dest; // destination pointer
	long i = -(long)len; // input position
	long lineSizeOffset = -line_size +2; // line size excluding first/last char
	long col = *colOffset + lineSizeOffset -1;
	
	// offset position to enable simpler loop condition checking
	const int INPUT_OFFSET = YMM_SIZE + 2 -1; // extra 2 chars for EOL handling, -1 to change <= to <
	i += INPUT_OFFSET;
	const uint8_t* es = srcEnd - INPUT_OFFSET;
	
	if (LIKELIHOOD(0.999, col == lineSizeOffset-1)) {
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
			encode_eol_handle_pre(es, i, p, col, lineSizeOffset);
		else {
			uint8_t c = es[i++];
			if (LIKELIHOOD(0.0273, escapedLUT[c]!=0)) {
				*(uint32_t*)p = UINT32_16_PACK(UINT16_PACK('\r', '\n'), (uint32_t)escapedLUT[c]);
				p += 4;
				col = 1+lineSizeOffset;
			} else {
				*(uint32_t*)p = UINT32_PACK('\r', '\n', (uint32_t)(c+42), 0);
				p += 3;
				col = lineSizeOffset;
			}
		}
	}
	while(i < 0) {
		__m256i oData = _mm256_loadu_si256((__m256i *)(es + i));
		__m256i data = _mm256_add_epi8(oData, _mm256_set1_epi8(42));
		i += YMM_SIZE;
		// search for special chars
		__m256i cmp = _mm256_or_si256(
			_mm256_cmpeq_epi8(oData, _mm256_set1_epi8('='-42)),
			_mm256_shuffle_epi8(_mm256_set_epi8(
				//  \r     \n                   \0
				0,0,-1,0,0,-1,0,0,0,0,0,0,0,0,0,-1,
				0,0,-1,0,0,-1,0,0,0,0,0,0,0,0,0,-1
			), _mm256_adds_epu8(
				data, _mm256_set1_epi8(0x70)
			))
		);
		/* this AVX512 idea has one less instruction, but is slower on Skylake-X, probably due to masked compares being very slow? maybe it'll be faster on some later processor?
		__m256i cmp = _mm256_mask_shuffle_epi8(
			_mm256_cmpeq_epi8(oData, _mm256_set1_epi8('='-42)),
			_mm256_cmplt_epu8_mask(data, _mm256_set1_epi8(16)),
			_mm256_set_epi8(
				//  \r     \n                   \0
				0,0,-1,0,0,-1,0,0,0,0,0,0,0,0,0,-1,
				0,0,-1,0,0,-1,0,0,0,0,0,0,0,0,0,-1
			),
			data
		);
		*/
		
		uint32_t mask = _mm256_movemask_epi8(cmp);
		unsigned int maskBits = popcnt32(mask);
		unsigned int outputBytes = maskBits+YMM_SIZE;
		// unlike the SSE (128-bit) encoder, the probability of at least one escaped character in a vector is much higher here, which causes the branch to be relatively unpredictable, resulting in poor performance
		// because of this, we tilt the probability towards the fast path by process single-character escape cases there; this results in a speedup, despite the fast path being slower
		// likelihood of >1 bit set: 1-((63/64)^32 + (63/64)^31 * (1/64) * 32C1)
		if (LIKELIHOOD(0.089, maskBits > 1)) {
			unsigned int m1 = mask & 0xffff;
			unsigned int m2;
			__m256i data1, data2;
			__m256i shufMA, shufMB; // not set in VBMI2 path
			
			data = _mm256_add_epi8(data, _mm256_and_si256(cmp, _mm256_set1_epi8(64)));
#if defined(__AVX512VBMI2__) && defined(__AVX512VL__) && defined(__AVX512BW__)
			if(use_isa >= ISA_LEVEL_VBMI2) {
				m2 = mask >> 16;
				
				/* alternative no-LUT strategy
				uint64_t expandMask = _pdep_u64(mask, 0x5555555555555555); // expand bits
				expandMask = _pext_u64(~expandMask, expandMask|0xaaaaaaaaaaaaaaaa);
				*/
				
				data1 = _mm256_mask_expand_epi8(_mm256_set1_epi8('='), expandLUT[m1], data);
				data2 = _mm256_mask_expand_epi8(_mm256_set1_epi8('='), expandLUT[m2], _mm256_castsi128_si256(
					_mm256_extracti128_si256(data, 1)
				));
			} else
#endif
			{
				m2 = (mask >> 11) & 0x1fffe0;
				
				// duplicate halves
				data1 = _mm256_inserti128_si256(data, _mm256_castsi256_si128(data), 1);
				data2 = _mm256_permute4x64_epi64(data, 0xee);
				
				shufMA = _mm256_load_si256(shufExpandLUT + m1);
				shufMB = _mm256_load_si256((__m256i*)((char*)shufExpandLUT + m2));
				
				// expand
				data1 = _mm256_shuffle_epi8(data1, shufMA);
				data2 = _mm256_shuffle_epi8(data2, shufMB);
				// add in '='
				data1 = _mm256_blendv_epi8(data1, _mm256_set1_epi8('='), shufMA);
				data2 = _mm256_blendv_epi8(data2, _mm256_set1_epi8('='), shufMB);
			}
			
			unsigned char shuf1Len = popcnt32(m1) + 16;
			_mm256_storeu_si256((__m256i*)p, data1);
			_mm256_storeu_si256((__m256i*)(p + shuf1Len), data2);
			p += outputBytes;
			col += outputBytes;
			
			if(LIKELIHOOD(0.3, col >= 0)) {
				// we overflowed - find correct position to revert back to
				// this is perhaps sub-optimal on 32-bit, but who still uses that with AVX2?
				uint64_t eqMask1, eqMask2;
#if defined(__AVX512VBMI2__) && defined(__AVX512VL__) && defined(__AVX512BW__)
				if(use_isa >= ISA_LEVEL_VBMI2) {
					eqMask1 = expandLUT[m1];
					eqMask2 = expandLUT[m2];
				} else
#endif
				{
					eqMask1 = (uint32_t)_mm256_movemask_epi8(shufMA);
					eqMask2 = (uint32_t)_mm256_movemask_epi8(shufMB);
				}
				uint64_t eqMask = eqMask1 | (eqMask2 << shuf1Len);
				
				eqMask >>= outputBytes - col -1;
				unsigned int bitCount;
#ifdef PLATFORM_AMD64
				bitCount = (unsigned int)_mm_popcnt_u64(eqMask);
#else
				bitCount = popcnt32(eqMask & 0xffffffff) + popcnt32(eqMask >> 32);
#endif
#if defined(__AVX512VBMI2__) && defined(__AVX512VL__) && defined(__AVX512BW__)
				if(use_isa >= ISA_LEVEL_VBMI2) {
					i -= bitCount;
					p -= col;
					if(LIKELIHOOD(0.98, (eqMask & 1) != 1))
						p--;
					else
						i++;
				} else
#endif
				{
					i += bitCount;
					long revert = col + (eqMask & 1);
					p -= revert;
					i -= revert;
				}
				goto _encode_eol_handle_pre;
			}
		} else {
			long bitIndex;
#if defined(__AVX512VBMI2__) && defined(__AVX512VL__) && defined(__AVX512BW__)
			if(use_isa >= ISA_LEVEL_VBMI2) {
				data = _mm256_mask_expand_epi8(
					_mm256_set1_epi8('='),
# if defined(__GNUC__) && __GNUC__ >= 8
					// GCC 8/9/10(dev) fails to optimize this, so use intrinsic explicitly; Clang 6+ has no issue, but Clang 6/7 doesn't have the intrinsic; MSVC 2019 also fails and lacks the intrinsic
					_knot_mask32(mask),
# else
					~mask,
# endif
					_mm256_mask_add_epi8(data, mask, data, _mm256_set1_epi8(64))
				);
			} else
#endif
			{
				bitIndex = _lzcnt_u32(mask);
				__m256i mergeMask = _mm256_load_si256(expand_mergemix_table + bitIndex*2);
				__m256i dataMasked = _mm256_andnot_si256(mergeMask, data);
				// to deal with the pain of lane crossing, use shift + mask/blend
				__m256i dataShifted = _mm256_alignr_epi8(
					dataMasked,
#if defined(__tune_znver1__) || defined(__tune_bdver4__)
					_mm256_inserti128_si256(_mm256_setzero_si256(), _mm256_castsi256_si128(dataMasked), 1),
#else
					_mm256_permute2x128_si256(dataMasked, dataMasked, 8),
#endif
					15
				);
#if defined(__AVX512VL__) && defined(__AVX512BW__)
				if(use_isa >= ISA_LEVEL_AVX3)
					data = _mm256_ternarylogic_epi32(dataShifted, data, mergeMask, 0xf8); // (data & mergeMask) | dataShifted
				else
#endif
					data = _mm256_blendv_epi8(dataShifted, data, mergeMask);
				
				data = _mm256_add_epi8(data, _mm256_load_si256(expand_mergemix_table + bitIndex*2 + 1));
			}
			// store main + additional char
			_mm256_storeu_si256((__m256i*)p, data);
			p[YMM_SIZE] = es[i-1] + 42 + (64 & (mask>>(YMM_SIZE-1-6)));
			p += outputBytes;
			col += outputBytes;
			
			if(LIKELIHOOD(0.3, col >= 0)) {
#if defined(__AVX512VBMI2__) && defined(__AVX512VL__) && defined(__AVX512BW__)
				if(use_isa >= ISA_LEVEL_VBMI2)
					bitIndex = _lzcnt_u32(mask);
#endif
				
				if(HEDLEY_UNLIKELY(col-1 == bitIndex)) {
					// this is an escape character, so line will need to overflow
					p--;
				} else {
					i += (col-1 > bitIndex);
				}
				i -= col;
				p -= col;
				
				_encode_eol_handle_pre:
				encode_eol_handle_pre(es, i, p, col, lineSizeOffset);
			}
		}
	}
	
	_mm256_zeroupper();
	
	*colOffset = col - lineSizeOffset +1;
	dest = p;
	len = -(i - INPUT_OFFSET);
}

#endif
