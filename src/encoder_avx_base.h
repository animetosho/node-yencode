#include "common.h"
#ifdef __AVX2__

#include "encoder.h"
//#include "encoder_common.h"
#include "encoder_sse_base.h"
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


template<enum YEncDecIsaLevel use_isa>
static size_t do_encode_avx2(int line_size, int* colOffset, const unsigned char* HEDLEY_RESTRICT src, unsigned char* HEDLEY_RESTRICT dest, size_t len) {
	unsigned char* es = (unsigned char*)src + len;
	unsigned char *p = dest; // destination pointer
	long i = -(long)len; // input position
	unsigned char c, escaped; // input character; escaped input character
	int col = *colOffset;
	
	if (LIKELIHOOD(0.999, col == 0)) {
		c = es[i++];
		if (LIKELIHOOD(0.0273, escapedLUT[c] != 0)) {
			*(uint16_t*)p = escapedLUT[c];
			p += 2;
			col = 2;
		} else {
			*(p++) = c + 42;
			col = 1;
		}
	}
	while(i < 0) {
		// main line
		while (i < -1-YMM_SIZE && col < line_size-1) {
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
			
			uint32_t mask = _mm256_movemask_epi8(cmp);
			// unlike the SSE (128-bit) encoder, the probability of at least one escaped character in a vector is much higher here, which causes the branch to be relatively unpredictable, resulting in poor performance
			// because of this, we tilt the probability towards the fast path by process single-character escape cases there; this results in a speedup, despite the fast path being slower
			int onlyOneOrNoneBitSet =
#if defined(__tune_znver2__) || defined(__tune_znver1__)
				_mm_popcnt_u32(mask) < 2;
#else
				(mask & (mask-1)) == 0;
#endif
			// likelihood of >1 bit set: 1-((63/64)^32 + (63/64)^31 * (1/64) * 32C1)
			if (LIKELIHOOD(0.089, !onlyOneOrNoneBitSet)) {
				
#if defined(__AVX512VBMI2__) && defined(__AVX512VL__) && defined(__AVX512BW__) && defined(__BMI2__) && 0
				if(use_isa >= ISA_LEVEL_VBMI2) {
					data = _mm256_mask_add_epi8(data, mask, data, _mm256_set1_epi8(64));
					
					// expand compactMask
# ifdef PLATFORM_AMD64
					uint64_t compactMask = _pdep_u64(mask, 0x5555555555555555); // could also use pclmul
					compactMask |= 0xaaaaaaaaaaaaaaaa; // set all high bits to keep them
# else
					uint32_t compactMask1 = _pdep_u32(mask, 0x55555555);
					uint32_t compactMask2 = _pdep_u32(mask>>16, 0x55555555);
					compactMask1 |= 0xaaaaaaaa;
					compactMask2 |= 0xaaaaaaaa;
# endif
					
# if 0
					// uses 512b instructions, which may be more efficient if CPU doesn't hard throttle on these (like SKX does, and also switches CPU to 512b port mode)
					__m512i paddedData = _mm512_permutexvar_epi64(_mm512_set_epi64(
						3,3, 2,2, 1,1, 0,0
					), _mm512_castsi256_si512(data));
					paddedData = _mm512_unpacklo_epi8(_mm512_set1_epi8('='), paddedData);
#  ifndef PLATFORM_AMD64
					__mmask64 compactMask = (((uint64_t)compactMask2) << 32) | compactMask1;
					unsigned int shufALen = popcnt32(mask & 0xffff) + 16; // needed for overflow handling
#  endif
					_mm512_mask_compressstoreu_epi8(p, compactMask, paddedData);
					
					unsigned int bytes = popcnt32(mask) + 32;
					p += bytes;
					
# else
					__m256i dataForUnpack = _mm256_permute4x64_epi64(data, 0xD8); // swap middle 64-bit qwords
					__m256i data1 = _mm256_unpacklo_epi8(_mm256_set1_epi8('='), dataForUnpack);
					__m256i data2 = _mm256_unpackhi_epi8(_mm256_set1_epi8('='), dataForUnpack);
					
					unsigned int shufALen = popcnt32(mask & 0xffff) + 16;
					unsigned int shufBLen = popcnt32(mask & 0xffff0000) + 16;
#  ifdef PLATFORM_AMD64
					uint32_t compactMask1 = compactMask & 0xffffffff;
					uint32_t compactMask2 = compactMask >> 32;
#  endif
					_mm256_mask_compressstoreu_epi8(p, compactMask1, data1);
					p += shufALen;
					_mm256_mask_compressstoreu_epi8(p, compactMask2, data2);
					p += shufBLen;
					int bytes = shufALen + shufBLen;
# endif
					
					col += bytes;
					
					int ovrflowAmt = col - (line_size-1);
					if(LIKELIHOOD(0.3, ovrflowAmt > 0)) {
# ifdef PLATFORM_AMD64
						uint64_t eqMask = _pext_u64(0x5555555555555555, compactMask);
						eqMask >>= bytes - ovrflowAmt -1;
						i -= ovrflowAmt - (unsigned int)_mm_popcnt_u64(eqMask);
# else
						uint64_t eqMask1 = _pext_u32(0x55555555, compactMask1);
						uint64_t eqMask2 = _pext_u32(0x55555555, compactMask2);
						uint64_t eqMask = (eqMask2 << shufALen) | eqMask1;
						eqMask >>= bytes - ovrflowAmt -1;
						i -= ovrflowAmt - popcnt32(eqMask & 0xffffffff) - popcnt32(eqMask >> 32);
# endif
						p -= ovrflowAmt - (eqMask & 1);
						if(LIKELIHOOD(0.02, eqMask & 1))
							goto after_last_char_avx2;
						else
							goto last_char_avx2;
					}
				} else
#endif
				{
					
					int m1 = mask & 0xffff;
					int m2 = (mask >> 11) & 0x1fffe0;
					unsigned char shuf1Len = popcnt32(m1) + 16;
					unsigned char shuf2Len = popcnt32(m2) + 16;
					
					data = _mm256_add_epi8(data, _mm256_and_si256(cmp, _mm256_set1_epi8(64)));
					
					// duplicate halves
					__m256i data1 = _mm256_inserti128_si256(data, _mm256_castsi256_si128(data), 1);
					__m256i data2 = _mm256_permute4x64_epi64(data, 0xee);
					
					__m256i shufMA = _mm256_load_si256(shufExpandLUT + m1);
					__m256i shufMB = _mm256_load_si256((__m256i*)((char*)shufExpandLUT + m2));
					// expand
					data1 = _mm256_shuffle_epi8(data1, shufMA);
					data2 = _mm256_shuffle_epi8(data2, shufMB);
					
					// generate = vectors
					__m256i shufMixMA = _mm256_subs_epu8(
						shufMA, _mm256_set1_epi8(0x70)
					);
					__m256i shufMixMB = _mm256_subs_epu8(
						shufMB, _mm256_set1_epi8(0x70)
					);
					
					// add in escaped chars
					data1 = _mm256_or_si256(data1, shufMixMA);
					data2 = _mm256_or_si256(data2, shufMixMB);
					
					_mm256_storeu_si256((__m256i*)p, data1);
					p += shuf1Len;
					_mm256_storeu_si256((__m256i*)p, data2);
					p += shuf2Len;
					col += shuf1Len + shuf2Len;
					
					int ovrflowAmt = col - (line_size-1);
					if(LIKELIHOOD(0.3, ovrflowAmt > 0)) {
						// we overflowed - find correct position to revert back to
						// this is perhaps sub-optimal on 32-bit, but who still uses that with AVX2?
						uint64_t eqMask1 = _mm256_movemask_epi8(shufMA);
						uint64_t eqMask2 = _mm256_movemask_epi8(shufMB);
						uint64_t eqMask = eqMask1 | (eqMask2 << shuf1Len);
						
						eqMask >>= shuf1Len + shuf2Len - ovrflowAmt -1;
#ifdef PLATFORM_AMD64
						i -= ovrflowAmt - (unsigned int)_mm_popcnt_u64(eqMask);
#else
						i -= ovrflowAmt - popcnt32(eqMask & 0xffffffff) - popcnt32(eqMask >> 32);
#endif
						p -= ovrflowAmt - (eqMask & 1);
						if(LIKELIHOOD(0.02, eqMask & 1))
							goto after_last_char_avx2;
						else
							goto last_char_avx2;
					}
				}
			} else {
				intptr_t bitIndex = _lzcnt_u32(mask);
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
					__m256i mergeMask = _mm256_load_si256(expand_mergemix_table + bitIndex*2);
					__m256i dataMasked = _mm256_andnot_si256(mergeMask, data);
					// to deal with the pain of lane crossing, use shift + mask/blend
					__m256i dataShifted = _mm256_alignr_epi8(
						dataMasked,
						//_mm256_permute2x128_si256(dataMasked, dataMasked, 8)
						_mm256_inserti128_si256(_mm256_setzero_si256(), _mm256_castsi256_si128(dataMasked), 1),
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
				unsigned int processed = popcnt32(mask);
				p += YMM_SIZE + processed;
				col += YMM_SIZE + processed;
				
				int ovrflowAmt = col - (line_size-1);
				if(LIKELIHOOD(0.3, ovrflowAmt > 0)) {
					if(ovrflowAmt-1 == bitIndex) {
						// this is an escape character, so line will need to overflow
						p -= ovrflowAmt - 1;
						i -= ovrflowAmt - 1;
						goto after_last_char_avx2;
					} else {
						int overflowedPastEsc = (unsigned int)(ovrflowAmt-1) > (unsigned int)bitIndex;
						p -= ovrflowAmt;
						i -= ovrflowAmt - overflowedPastEsc;
						goto last_char_avx2;
					}
				}
			}
		}
		// handle remaining chars
		while(col < line_size-1) {
			c = es[i++], escaped = escapeLUT[c];
			if (LIKELIHOOD(0.9844, escaped!=0)) {
				*(p++) = escaped;
				col++;
			}
			else {
				*(uint16_t*)p = escapedLUT[c];
				p += 2;
				col += 2;
			}
			if (LIKELIHOOD(0.001, i >= 0)) goto end;
		}
		
		// last line char
		if(LIKELIHOOD(0.95, col < line_size)) { // this can only be false if the last character was an escape sequence (or line_size is horribly small), in which case, we don't need to handle space/tab cases
			last_char_avx2:
			c = es[i++];
			if (LIKELIHOOD(0.0234, escapedLUT[c] && c != '.'-42)) {
				*(uint16_t*)p = escapedLUT[c];
				p += 2;
			} else {
				*(p++) = c + 42;
			}
		}
		
		after_last_char_avx2:
		if (LIKELIHOOD(0.001, i >= 0)) break;
		
		c = es[i++];
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
	
	_mm256_zeroupper();
	
	end:
	// special case: if the last character is a space/tab, it needs to be escaped as it's the final character on the line
	unsigned char lc = *(p-1);
	if(lc == '\t' || lc == ' ') {
		*(uint16_t*)(p-1) = UINT16_PACK('=', lc+64);
		p++;
		col++;
	}
	*colOffset = col;
	return p - dest;
}

#endif
