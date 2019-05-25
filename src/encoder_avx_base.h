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

ALIGN_32(static __m256i shufExpandLUT[65536]); // huge 2MB table
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
static size_t do_encode_avx2(int line_size, int* colOffset, const unsigned char* src, unsigned char* dest, size_t len) {
	unsigned char* es = (unsigned char*)src + len;
	unsigned char *p = dest; // destination pointer
	long i = -len; // input position
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
			if (LIKELIHOOD(0.3959, mask != 0)) {
				
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
					// uses 512b instructions, but probably more efficient
					__m512i paddedData = _mm512_permutexvar_epi64(_mm512_set_epi64(
						3,3, 2,2, 1,1, 0,0
					), _mm512_castsi256_si512(data));
					paddedData = _mm512_unpacklo_epi8(_mm512_set1_epi8('='), paddedData);
#  ifndef PLATFORM_AMD64
					__mmask64 compactMask = (((uint64_t)compactMask2) << 32) | compactMask1;
					int shufALen = _mm_popcnt_u32(mask & 0xffff) + 16; // needed for overflow handling
#  endif
					_mm512_mask_compressstoreu_epi8(p, compactMask, paddedData);
					
					int bytes = _mm_popcnt_u32(mask) + 32;
					p += bytes;
					
# else
					__m256i dataForUnpack = _mm256_permute4x64_epi64(data, 0xD8); // swap middle 64-bit qwords
					__m256i data1 = _mm256_unpacklo_epi8(_mm256_set1_epi8('='), dataForUnpack);
					__m256i data2 = _mm256_unpackhi_epi8(_mm256_set1_epi8('='), dataForUnpack);
					
					int shufALen = _mm_popcnt_u32(mask & 0xffff) + 16;
					int shufBLen = _mm_popcnt_u32(mask >> 16) + 16;
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
#ifdef PLATFORM_AMD64
						uint64_t eqMask = _pext_u64(0x5555555555555555, compactMask);
						eqMask >>= bytes - ovrflowAmt -1;
						i -= ovrflowAmt - _mm_popcnt_u64(eqMask);
#else
						uint64_t eqMask1 = _pext_u32(0x55555555, compactMask1);
						uint64_t eqMask2 = _pext_u32(0x55555555, compactMask2);
						uint64_t eqMask = (eqMask2 << shufALen) | eqMask1;
						eqMask >>= bytes - ovrflowAmt -1;
						i -= ovrflowAmt - _mm_popcnt_u32(eqMask & 0xffffffff) - _mm_popcnt_u32(eqMask >> 32);
#endif
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
					unsigned char shuf1Len = _mm_popcnt_u32(m1) + 16;
					unsigned char shuf2Len = _mm_popcnt_u32(m2) + 16;
					
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
						i -= ovrflowAmt - _mm_popcnt_u64(eqMask);
#else
						i -= ovrflowAmt - _mm_popcnt_u32(eqMask & 0xffffffff) - _mm_popcnt_u32(eqMask >> 32);
#endif
						p -= ovrflowAmt - (eqMask & 1);
						if(LIKELIHOOD(0.02, eqMask & 1))
							goto after_last_char_avx2;
						else
							goto last_char_avx2;
					}
				}
			} else {
				_mm256_storeu_si256((__m256i*)p, data);
				p += YMM_SIZE;
				col += YMM_SIZE;
				int ovrflowAmt = col - (line_size-1);
				if(LIKELIHOOD(0.3, ovrflowAmt > 0)) {
					p -= ovrflowAmt;
					i -= ovrflowAmt;
					//col = line_size-1;
					goto last_char_avx2;
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
