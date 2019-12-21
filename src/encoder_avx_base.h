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

#pragma pack(16)
struct TShufMix {
	__m128i shuf, mix;
};
#pragma pack()

static struct {
	const int8_t ALIGN_TO(64, expand_mergemix[33*2*32]);
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
	if(len < YMM_SIZE*2+1 || line_size < 16) return;
	
	uint8_t *p = dest; // destination pointer
	long i = -(long)len; // input position
	long lineSizeOffset = -line_size +1;
	long col = *colOffset + lineSizeOffset;
	
	// offset position to enable simpler loop condition checking
	const int INPUT_OFFSET = YMM_SIZE + YMM_SIZE+1 -1; // extra chars for EOL handling, -1 to change <= to <
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
		__m256i data = _mm256_loadu_si256((__m256i *)(es + i));
		i += YMM_SIZE;
		// search for special chars
		__m256i cmp = _mm256_cmpeq_epi8(
			_mm256_shuffle_epi8(_mm256_set_epi8(
				'\0'-42,-42,'\r'-42,'.'-42,'='-42,'\0'-42,'\t'-42,'\n'-42,-42,-42,'\r'-42,-42,'='-42,' '-42,-42,'\n'-42,
				'\0'-42,-42,'\r'-42,'.'-42,'='-42,'\0'-42,'\t'-42,'\n'-42,-42,-42,'\r'-42,-42,'='-42,' '-42,-42,'\n'-42
			), _mm256_abs_epi8(data)),
			data
		);
		
#if defined(__AVX512VL__)
		if(use_isa >= ISA_LEVEL_AVX3) {
			data = _mm256_add_epi8(data, _mm256_set1_epi8(42));
			data = _mm256_ternarylogic_epi32(data, cmp, _mm256_set1_epi8(64), 0xf8); // data | (cmp & 64)
		}
#endif
		
		uint32_t mask = _mm256_movemask_epi8(cmp);
		unsigned int maskBits = popcnt32(mask);
		unsigned int outputBytes = maskBits+YMM_SIZE;
		// unlike the SSE (128-bit) encoder, the probability of at least one escaped character in a vector is much higher here, which causes the branch to be relatively unpredictable, resulting in poor performance
		// because of this, we tilt the probability towards the fast path by process single-character escape cases there; this results in a speedup, despite the fast path being slower
		// likelihood of >1 bit set: 1-((63/64)^32 + (63/64)^31 * (1/64) * 32C1)
		if (LIKELIHOOD(0.089, maskBits > 1)) {
			_encode_loop_branch_slow:
			unsigned int m1 = mask & 0xffff;
			unsigned int m2;
			__m256i data1, data2;
			__m256i shufMA, shufMB; // not set in VBMI2 path
			
#if defined(__AVX512VBMI2__) && defined(__AVX512VL__) && defined(__AVX512BW__)
			if(use_isa >= ISA_LEVEL_VBMI2) {
				m2 = mask >> 16;
				
				/* alternative no-LUT strategy
				uint64_t expandMask = _pdep_u64(mask, 0x5555555555555555); // expand bits
				expandMask = _pext_u64(~expandMask, expandMask|0xaaaaaaaaaaaaaaaa);
				*/
				
				data1 = _mm256_mask_expand_epi8(_mm256_set1_epi8('='), lookups.expand[m1], data);
				data2 = _mm256_mask_expand_epi8(_mm256_set1_epi8('='), lookups.expand[m2], _mm256_castsi128_si256(
					_mm256_extracti128_si256(data, 1)
				));
			} else
#endif
			{
				if(use_isa < ISA_LEVEL_AVX3)
					data = _mm256_add_epi8(data, _mm256_blendv_epi8(_mm256_set1_epi8(42), _mm256_set1_epi8(42+64), cmp));
				
				m2 = (mask >> 11) & 0x1fffe0;
				
				// duplicate halves
				data1 = _mm256_inserti128_si256(data, _mm256_castsi256_si128(data), 1);
				data2 = _mm256_permute4x64_epi64(data, 0xee);
				
				shufMA = _mm256_load_si256(lookups.shufExpand + m1);
				shufMB = _mm256_load_si256((__m256i*)((char*)lookups.shufExpand + m2));
				
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
					eqMask1 = lookups.expand[m1];
					eqMask2 = lookups.expand[m2];
				} else
#endif
				{
					eqMask1 = (uint32_t)_mm256_movemask_epi8(shufMA);
					eqMask2 = (uint32_t)_mm256_movemask_epi8(shufMB);
				}
				uint64_t eqMask = eqMask1 | (eqMask2 << shuf1Len);
				
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
			long bitIndex;
#if defined(__AVX512VL__) && defined(__AVX512BW__)
			if(use_isa >= ISA_LEVEL_AVX3) {
				_mm256_mask_storeu_epi8(p+1, 1<<31, data); // store last byte
# if defined(__AVX512VBMI2__)
				if(use_isa >= ISA_LEVEL_VBMI2) {
					data = _mm256_mask_expand_epi8(_mm256_set1_epi8('='), ~mask, data);
				} else
# endif
				{
					__m256i swapped = _mm256_permute4x64_epi64(data, _MM_SHUFFLE(1,0,3,2));
					data = _mm256_mask_alignr_epi8(data, ~(mask-1), data, swapped, 15);
					data = _mm256_ternarylogic_epi32(data, cmp, _mm256_set1_epi8('='), 0xb8); // (data & ~cmp) | (cmp & '=')
				}
			} else
#endif
			{
				bitIndex = _lzcnt_u32(mask);
				__m256i mergeMask = _mm256_load_si256((const __m256i*)lookups.expand_mergemix + bitIndex*2);
				// to deal with the pain of lane crossing, use shift + mask/blend
				__m256i dataShifted = _mm256_alignr_epi8(
					data,
					_mm256_inserti128_si256(data, _mm256_castsi256_si128(data), 1),
					15
				);
				data = _mm256_andnot_si256(cmp, data);
				data = _mm256_blendv_epi8(dataShifted, data, mergeMask);
				data = _mm256_add_epi8(data, _mm256_load_si256((const __m256i*)lookups.expand_mergemix + bitIndex*2 + 1));
				p[YMM_SIZE] = es[i-1] + 42 + (64 & (mask>>(YMM_SIZE-1-6)));
			}
			// store main + additional char
			_mm256_storeu_si256((__m256i*)p, data);
			p += outputBytes;
			col += outputBytes;
			
			if(LIKELIHOOD(0.3, col >= 0)) {
#if defined(__AVX512VL__) && defined(__AVX512BW__)
				if(use_isa >= ISA_LEVEL_AVX3)
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
				uint32_t eolChar = lookups.eolLastChar[es[i]];
				*(uint32_t*)p = eolChar;
				p += 3 + (eolChar>>27);
				col = lineSizeOffset;
				
				if(HEDLEY_UNLIKELY(i >= 0)) { // this isn't really a proper check - it's only needed to support short lines; basically, if the line is too short, `i` never gets checked, so we need one somewhere
					i++;
					break;
				}
				
				data = _mm256_loadu_si256((__m256i *)(es + i + 1));
				// search for special chars
				cmp = _mm256_cmpeq_epi8(
					_mm256_shuffle_epi8(_mm256_set_epi8(
						'\0'-42,-42,'\r'-42,'.'-42,'='-42,'\0'-42,'\t'-42,'\n'-42,-42,-42,'\r'-42,-42,'='-42,' '-42,-42,'\n'-42,
						'\0'-42,-42,'\r'-42,'.'-42,'='-42,'\0'-42,'\t'-42,'\n'-42,-42,-42,'\r'-42,-42,'='-42,' '-42,-42,'\n'-42
					), _mm256_adds_epi8(
						_mm256_abs_epi8(data), _mm256_castsi128_si256(_mm_cvtsi32_si128(88))
					)),
					data
				);
				i += YMM_SIZE + 1;
				
				
				// duplicate some code from above to reduce jumping a little
#if defined(__AVX512VL__)
				if(use_isa >= ISA_LEVEL_AVX3) {
					data = _mm256_add_epi8(data, _mm256_set1_epi8(42));
					data = _mm256_ternarylogic_epi32(data, cmp, _mm256_set1_epi8(64), 0xf8);
				}
#endif
				
				mask = _mm256_movemask_epi8(cmp);
				maskBits = popcnt32(mask);
				outputBytes = maskBits+YMM_SIZE;
				if (LIKELIHOOD(0.089, maskBits > 1))
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
