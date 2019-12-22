// can't seem to make this worth it
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

#if defined(__GNUC__) && __GNUC__ >= 7
# define KLOAD32(a, offs) _load_mask32((__mmask32*)(a) + (offs))
#else
# define KLOAD32(a, offs) (((uint32_t*)(a))[(offs)])
#endif

#pragma pack(16)
struct TShufMix {
	__m128i shuf, mix;
};
#pragma pack()

static struct {
	const int8_t ALIGN_TO(32, expand_mergemix[33*2*32]);
	__m256i ALIGN_TO(32, shufExpand[65536]); // huge 2MB table
	const uint32_t eolLastChar[256];
	uint32_t expand[65536]; // biggish 256KB table (but still smaller than the 2MB table)
} lookups = {
	/*expand_mergemix*/ {
		#define _X2(n,k) n>=k?-1:0
		#define _X(n) _X2(n,0), _X2(n,1), _X2(n,2), _X2(n,3), _X2(n,4), _X2(n,5), _X2(n,6), _X2(n,7), \
			_X2(n,8), _X2(n,9), _X2(n,10), _X2(n,11), _X2(n,12), _X2(n,13), _X2(n,14), _X2(n,15), \
			_X2(n,16), _X2(n,17), _X2(n,18), _X2(n,19), _X2(n,20), _X2(n,21), _X2(n,22), _X2(n,23), \
			_X2(n,24), _X2(n,25), _X2(n,26), _X2(n,27), _X2(n,28), _X2(n,29), _X2(n,30), _X2(n,31)
		#define _Y2(n, m) '='*(n==m) + 64*(n==m-1) + 42*(n!=m)
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
	},
	
	/*shufExpand*/ {},
	/*eolLastChar*/ {
		#define _B1(n) _B(n), _B(n+1), _B(n+2), _B(n+3)
		#define _B2(n) _B1(n), _B1(n+4), _B1(n+8), _B1(n+12)
		#define _B3(n) _B2(n), _B2(n+16), _B2(n+32), _B2(n+48)
		#define _BX _B3(0), _B3(64), _B3(128), _B3(192)
		#define _B(n) ((n == 214+'\t' || n == 214+' ' || n == 214+'\0' || n == 214+'\n' || n == 214+'\r' || n == '='-42) ? (((n+42+64)&0xff)<<8)+0x0a0d003d : ((n+42)&0xff)+0x0a0d00)
			_BX
		#undef _B
		#undef _B1
		#undef _B2
		#undef _B3
		#undef _BX
	},
	/*expand*/ {}
};


#if defined(__AVX512VBMI2__) && defined(__AVX512VL__)
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
		lookups.expand[i] = expand;
	}
}
#endif

static void encoder_avx2_lut() {
	for(int i=0; i<65536; i++) {
		int k = i;
		uint8_t* res = (uint8_t*)(lookups.shufExpand + i);
		int p = 0;
		for(int j=0; j<16; j++) {
			if(k & 1) {
				res[j+p] = 0xff;
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
HEDLEY_ALWAYS_INLINE void do_encode_avx2(int line_size, int* colOffset, const uint8_t* HEDLEY_RESTRICT srcEnd, uint8_t* HEDLEY_RESTRICT& dest, size_t& len) {
	if(len < YMM_SIZE*4+1 || line_size < 16) return;
	
	uint8_t *p = dest; // destination pointer
	long i = -(long)len; // input position
	long lineSizeOffset = -line_size +1;
	long col = *colOffset + lineSizeOffset;
	
	// offset position to enable simpler loop condition checking
	const int INPUT_OFFSET = YMM_SIZE*4 + 2 -1; // extra 2 chars for EOL handling, -1 to change <= to <
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
		__m256i dataA = _mm256_loadu_si256((__m256i *)(es + i));
		__m256i dataB = _mm256_loadu_si256((__m256i *)(es + i) + 1);
		i += YMM_SIZE*2;
		// search for special chars
		__m256i cmpA = _mm256_cmpeq_epi8(
			_mm256_shuffle_epi8(_mm256_set_epi8(
				'\0'-42,-42,'\r'-42,'.'-42,'='-42,'\0'-42,'\t'-42,'\n'-42,-42,-42,'\r'-42,-42,'='-42,' '-42,-42,'\n'-42,
				'\0'-42,-42,'\r'-42,'.'-42,'='-42,'\0'-42,'\t'-42,'\n'-42,-42,-42,'\r'-42,-42,'='-42,' '-42,-42,'\n'-42
			), _mm256_abs_epi8(dataA)),
			dataA
		);
		__m256i cmpB = _mm256_cmpeq_epi8(
			_mm256_shuffle_epi8(_mm256_set_epi8(
				'\0'-42,-42,'\r'-42,'.'-42,'='-42,'\0'-42,'\t'-42,'\n'-42,-42,-42,'\r'-42,-42,'='-42,' '-42,-42,'\n'-42,
				'\0'-42,-42,'\r'-42,'.'-42,'='-42,'\0'-42,'\t'-42,'\n'-42,-42,-42,'\r'-42,-42,'='-42,' '-42,-42,'\n'-42
			), _mm256_abs_epi8(dataB)),
			dataB
		);
		
#if defined(__AVX512VL__)
		if(use_isa >= ISA_LEVEL_AVX3) {
			dataA = _mm256_add_epi8(dataA, _mm256_set1_epi8(42));
			dataA = _mm256_ternarylogic_epi32(dataA, cmpA, _mm256_set1_epi8(64), 0xf8); // data | (cmp & 64)
			dataB = _mm256_add_epi8(dataB, _mm256_set1_epi8(42));
			dataB = _mm256_ternarylogic_epi32(dataB, cmpB, _mm256_set1_epi8(64), 0xf8); // data | (cmp & 64)
		}
#endif
		
		uint32_t maskA = (uint32_t)_mm256_movemask_epi8(cmpA);
		uint32_t maskB = (uint32_t)_mm256_movemask_epi8(cmpB);
		unsigned int maskBitsA = popcnt32(maskA);
		unsigned int maskBitsB = popcnt32(maskB);
		long bitIndexA, bitIndexB;
		if (LIKELIHOOD(0.170, (maskBitsA|maskBitsB) > 1)) {
			_encode_loop_branch_slow:
			unsigned int m1 = maskA & 0xffff;
			unsigned int m2, m3, m4;
			__m256i data1A, data2A;
			__m256i data1B, data2B;
			__m256i shuf1A, shuf1B; // not set in VBMI2 path
			__m256i shuf2A, shuf2B; // not set in VBMI2 path
			
#if defined(__AVX512VBMI2__) && defined(__AVX512VL__) && defined(__AVX512BW__)
			if(use_isa >= ISA_LEVEL_VBMI2) {
				m2 = maskA >> 16;
				m3 = maskB & 0xffff;
				m4 = maskB >> 16;
				
				/* alternative no-LUT strategy
				uint64_t expandMaskA = ~_pdep_u64(~maskA, 0x5555555555555555); // expand bits, with bits set
				expandMaskA = _pext_u64(expandMaskA^0x5555555555555555, expandMaskA);
				*/
				
				data1A = _mm256_mask_expand_epi8(_mm256_set1_epi8('='), KLOAD32(lookups.expand, m1), dataA);
				data2A = _mm256_mask_expand_epi8(_mm256_set1_epi8('='), KLOAD32(lookups.expand, m2), _mm256_castsi128_si256(
					_mm256_extracti128_si256(dataA, 1)
				));
				data1B = _mm256_mask_expand_epi8(_mm256_set1_epi8('='), KLOAD32(lookups.expand, m3), dataB);
				data2B = _mm256_mask_expand_epi8(_mm256_set1_epi8('='), KLOAD32(lookups.expand, m4), _mm256_castsi128_si256(
					_mm256_extracti128_si256(dataB, 1)
				));
			} else
#endif
			{
				if(use_isa < ISA_LEVEL_AVX3) {
					dataA = _mm256_add_epi8(dataA, _mm256_blendv_epi8(_mm256_set1_epi8(42), _mm256_set1_epi8(42+64), cmpA));
					dataB = _mm256_add_epi8(dataB, _mm256_blendv_epi8(_mm256_set1_epi8(42), _mm256_set1_epi8(42+64), cmpB));
				}
				
				m2 = (maskA >> 11) & 0x1fffe0;
				m3 = maskB & 0xffff;
				m4 = (maskB >> 11) & 0x1fffe0;
				
				// duplicate halves
				data1A = _mm256_inserti128_si256(dataA, _mm256_castsi256_si128(dataA), 1);
				data2A = _mm256_permute4x64_epi64(dataA, 0xee);
				data1B = _mm256_inserti128_si256(dataB, _mm256_castsi256_si128(dataB), 1);
				data2B = _mm256_permute4x64_epi64(dataB, 0xee);
				
				shuf1A = _mm256_load_si256(lookups.shufExpand + m1);
				shuf2A = _mm256_load_si256((__m256i*)((char*)lookups.shufExpand + m2));
				shuf1B = _mm256_load_si256(lookups.shufExpand + m3);
				shuf2B = _mm256_load_si256((__m256i*)((char*)lookups.shufExpand + m4));
				
				// expand
				data1A = _mm256_shuffle_epi8(data1A, shuf1A);
				data2A = _mm256_shuffle_epi8(data2A, shuf2A);
				data1B = _mm256_shuffle_epi8(data1B, shuf1B);
				data2B = _mm256_shuffle_epi8(data2B, shuf2B);
				// add in '='
				data1A = _mm256_blendv_epi8(data1A, _mm256_set1_epi8('='), shuf1A);
				data2A = _mm256_blendv_epi8(data2A, _mm256_set1_epi8('='), shuf2A);
				data1B = _mm256_blendv_epi8(data1B, _mm256_set1_epi8('='), shuf1B);
				data2B = _mm256_blendv_epi8(data2B, _mm256_set1_epi8('='), shuf2B);
			}
			
			unsigned char shuf1Len = popcnt32(m1) + 16;
			unsigned char shuf2Len = maskBitsA + 32;
			unsigned char shuf3Len = shuf2Len + popcnt32(m3) + 16;
			_mm256_storeu_si256((__m256i*)p, data1A);
			_mm256_storeu_si256((__m256i*)(p + shuf1Len), data2A);
			_mm256_storeu_si256((__m256i*)(p + shuf2Len), data1B);
			_mm256_storeu_si256((__m256i*)(p + shuf3Len), data2B);
			long outputBytes = YMM_SIZE*2 + maskBitsA + maskBitsB;
			p += outputBytes;
			col += outputBytes;
			
			if(col >= 0) {
				// we overflowed - find correct position to revert back to
				// this is perhaps sub-optimal on 32-bit, but who still uses that with AVX2?
				uint32_t eqMask1, eqMask2;
				uint32_t eqMask3, eqMask4;
				uint64_t eqMask;
				if(HEDLEY_UNLIKELY(col >= outputBytes-shuf2Len)) {
#if defined(__AVX512VBMI2__) && defined(__AVX512VL__) && defined(__AVX512BW__)
					if(use_isa >= ISA_LEVEL_VBMI2) {
						eqMask1 = lookups.expand[m1];
						eqMask2 = lookups.expand[m2];
					} else
#endif
					{
						eqMask1 = (uint32_t)_mm256_movemask_epi8(shuf1A);
						eqMask2 = (uint32_t)_mm256_movemask_epi8(shuf2A);
					}
					eqMask = eqMask1 | ((uint64_t)eqMask2 << shuf1Len);
					if(use_isa >= ISA_LEVEL_VBMI2)
						i -= YMM_SIZE;
					else
						i += outputBytes-shuf2Len - YMM_SIZE;
				} else {
#if defined(__AVX512VBMI2__) && defined(__AVX512VL__) && defined(__AVX512BW__)
					if(use_isa >= ISA_LEVEL_VBMI2) {
						eqMask3 = lookups.expand[m3];
						eqMask4 = lookups.expand[m4];
					} else
#endif
					{
						eqMask3 = (uint32_t)_mm256_movemask_epi8(shuf1B);
						eqMask4 = (uint32_t)_mm256_movemask_epi8(shuf2B);
					}
					eqMask = eqMask3 | ((uint64_t)eqMask4 << (shuf3Len-shuf2Len));
					outputBytes -= shuf2Len;
				}
				
#if defined(__GNUC__) && defined(PLATFORM_AMD64)
				if(use_isa >= ISA_LEVEL_VBMI2) {
					asm(
						"shrq $1, %[eqMask] \n"
						"shrq %%cl, %[eqMask] \n"
						"adcq %[col], %[p] \n"
						: [eqMask]"+r"(eqMask), [p]"+r"(p)
						: "c"(outputBytes - col -1), [col]"r"(~col)
					);
					i -= _mm_popcnt_u64(eqMask);
				} else
#endif
				{
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
				}
				goto _encode_eol_handle_pre;
			}
		} else {
			_encode_loop_branch_fast:
			maskBitsA += YMM_SIZE;
			maskBitsB += YMM_SIZE;
#if defined(__AVX512VL__) && defined(__AVX512BW__)
			if(use_isa >= ISA_LEVEL_AVX3) {
# if defined(__AVX512VBMI2__)
				if(use_isa >= ISA_LEVEL_VBMI2) {
					_mm256_mask_storeu_epi8(p+1, 1<<31, dataA);
					dataA = _mm256_mask_expand_epi8(_mm256_set1_epi8('='), ~maskA, dataA);
					_mm256_storeu_si256((__m256i*)p, dataA);
					p += maskBitsA;
					
					_mm256_mask_storeu_epi8(p+1, 1<<31, dataB);
					dataB = _mm256_mask_expand_epi8(_mm256_set1_epi8('='), ~maskB, dataB);
					_mm256_storeu_si256((__m256i*)p, dataB);
					p += maskBitsB;
				} else
# endif
				{
					_mm256_mask_storeu_epi8(p+1, 1<<31, dataA);
					dataA = _mm256_mask_alignr_epi8(dataA, ~(maskA-1), dataA, _mm256_permute4x64_epi64(dataA, _MM_SHUFFLE(1,0,3,2)), 15);
					dataA = _mm256_ternarylogic_epi32(dataA, cmpA, _mm256_set1_epi8('='), 0xb8); // (data & ~cmp) | (cmp & '=')
					_mm256_storeu_si256((__m256i*)p, dataA);
					p += maskBitsA;
					
					_mm256_mask_storeu_epi8(p+1, 1<<31, dataB);
					dataB = _mm256_mask_alignr_epi8(dataB, ~(maskB-1), dataB, _mm256_permute4x64_epi64(dataB, _MM_SHUFFLE(1,0,3,2)), 15);
					dataB = _mm256_ternarylogic_epi32(dataB, cmpB, _mm256_set1_epi8('='), 0xb8);
					_mm256_storeu_si256((__m256i*)p, dataB);
					p += maskBitsB;
				}
			} else
#endif
			{
				bitIndexA = _lzcnt_u32(maskA);
				bitIndexB = _lzcnt_u32(maskB);
				__m256i mergeMaskA = _mm256_load_si256((const __m256i*)lookups.expand_mergemix + bitIndexA*2);
				__m256i mergeMaskB = _mm256_load_si256((const __m256i*)lookups.expand_mergemix + bitIndexB*2);
				
				// to deal with the pain of lane crossing, use shift + mask/blend
				__m256i dataShifted = _mm256_alignr_epi8(
					dataA,
					_mm256_inserti128_si256(dataA, _mm256_castsi256_si128(dataA), 1),
					15
				);
				dataA = _mm256_andnot_si256(cmpA, dataA);
				dataA = _mm256_blendv_epi8(dataShifted, dataA, mergeMaskA);
				dataA = _mm256_add_epi8(dataA, _mm256_load_si256((const __m256i*)lookups.expand_mergemix + bitIndexA*2 + 1));
				_mm256_storeu_si256((__m256i*)p, dataA);
				p[YMM_SIZE] = es[i-1-YMM_SIZE] + 42 + (64 & (maskA>>(YMM_SIZE-1-6)));
				p += maskBitsA;
				
				dataShifted = _mm256_alignr_epi8(
					dataB,
					_mm256_inserti128_si256(dataB, _mm256_castsi256_si128(dataB), 1),
					15
				);
				dataB = _mm256_andnot_si256(cmpB, dataB);
				dataB = _mm256_blendv_epi8(dataShifted, dataB, mergeMaskB);
				dataB = _mm256_add_epi8(dataB, _mm256_load_si256((const __m256i*)lookups.expand_mergemix + bitIndexB*2 + 1));
				_mm256_storeu_si256((__m256i*)p, dataB);
				p[YMM_SIZE] = es[i-1] + 42 + (64 & (maskB>>(YMM_SIZE-1-6)));
				p += maskBitsB;
			}
			col += maskBitsA + maskBitsB;
			
			if(col >= 0) {
				if(HEDLEY_UNLIKELY(col > maskBitsB)) {
					if(use_isa >= ISA_LEVEL_AVX3)
						bitIndexA = _lzcnt_u32(maskA);
					bitIndexA += 1 + maskBitsB;
					
					i += maskBitsB - YMM_SIZE;
					if(HEDLEY_UNLIKELY(col == bitIndexA)) {
						// this is an escape character, so line will need to overflow
						p--;
					} else {
						i += (col > bitIndexA);
					}
				} else {
					if(use_isa >= ISA_LEVEL_AVX3)
						bitIndexB = _lzcnt_u32(maskB);
					bitIndexB++;
					
					if(HEDLEY_UNLIKELY(col == bitIndexB)) {
						p--;
					} else {
						i += (col > bitIndexB);
					}
				}
				i -= col;
				p -= col;
				
				_encode_eol_handle_pre:
				uint32_t eolChar = lookups.eolLastChar[es[i]];
				*(uint32_t*)p = eolChar;
				p += 3 + (eolChar>>27);
				col = lineSizeOffset;
				
				if(HEDLEY_UNLIKELY(i >= 0)) { // this isn't really a proper check - it's only needed to support short lines; basically, if the line is too short, `i` never gets checked, so we need one somewhere
					i++;
					break;
				}
				
				dataA = _mm256_loadu_si256((__m256i *)(es + i + 1));
				dataB = _mm256_loadu_si256((__m256i *)(es + i + 1) + 1);
				// search for special chars
				cmpA = _mm256_cmpeq_epi8(
					_mm256_shuffle_epi8(_mm256_set_epi8(
						'\0'-42,-42,'\r'-42,'.'-42,'='-42,'\0'-42,'\t'-42,'\n'-42,-42,-42,'\r'-42,-42,'='-42,' '-42,-42,'\n'-42,
						'\0'-42,-42,'\r'-42,'.'-42,'='-42,'\0'-42,'\t'-42,'\n'-42,-42,-42,'\r'-42,-42,'='-42,' '-42,-42,'\n'-42
					), _mm256_adds_epi8(
						_mm256_abs_epi8(dataA), _mm256_castsi128_si256(_mm_cvtsi32_si128(88))
					)),
					dataA
				);
				cmpB = _mm256_cmpeq_epi8(
					_mm256_shuffle_epi8(_mm256_set_epi8(
						'\0'-42,-42,'\r'-42,'.'-42,'='-42,'\0'-42,'\t'-42,'\n'-42,-42,-42,'\r'-42,-42,'='-42,' '-42,-42,'\n'-42,
						'\0'-42,-42,'\r'-42,'.'-42,'='-42,'\0'-42,'\t'-42,'\n'-42,-42,-42,'\r'-42,-42,'='-42,' '-42,-42,'\n'-42
					), _mm256_abs_epi8(dataB)),
					dataB
				);
				
				i += YMM_SIZE*2 + 1;
				
				
				// duplicate some code from above to reduce jumping a little
#if defined(__AVX512VL__)
				if(use_isa >= ISA_LEVEL_AVX3) {
					dataA = _mm256_add_epi8(dataA, _mm256_set1_epi8(42));
					dataA = _mm256_ternarylogic_epi32(dataA, cmpA, _mm256_set1_epi8(64), 0xf8); // data | (cmp & 64)
					dataB = _mm256_add_epi8(dataB, _mm256_set1_epi8(42));
					dataB = _mm256_ternarylogic_epi32(dataB, cmpB, _mm256_set1_epi8(64), 0xf8); // data | (cmp & 64)
				}
#endif
				
				maskA = (uint32_t)_mm256_movemask_epi8(cmpA);
				maskB = (uint32_t)_mm256_movemask_epi8(cmpB);
				maskBitsA = popcnt32(maskA);
				maskBitsB = popcnt32(maskB);
				if (LIKELIHOOD(0.170, (maskBitsA|maskBitsB) > 1))
					goto _encode_loop_branch_slow;
				goto _encode_loop_branch_fast;
			}
		}
	} while(i < 0);
	
	_mm256_zeroupper();
	
	*colOffset = col + line_size -1;
	dest = p;
	len = -(i - INPUT_OFFSET);
}

#endif
