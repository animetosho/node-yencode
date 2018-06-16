#include "common.h"


static unsigned char escapeLUT[256]; // whether or not the character is critical
static uint16_t escapedLUT[256]; // escaped sequences for characters that need escaping


#ifdef __ARM_NEON
ALIGN_32(uint8x16_t _shufLUT[258]); // +2 for underflow guard entry
uint8x16_t* shufLUT = _shufLUT+2;
ALIGN_32(uint8x16_t shufMixLUT[256]);

static size_t do_encode_neon(int line_size, int* colOffset, const unsigned char* src, unsigned char* dest, size_t len) {
	unsigned char* es = (unsigned char*)src + len;
	unsigned char *p = dest; // destination pointer
	long i = -len; // input position
	unsigned char c, escaped; // input character; escaped input character
	int col = *colOffset;
	
	if (col == 0) {
		c = es[i++];
		if (escapedLUT[c]) {
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
		while (i < -1-(long)sizeof(uint8x16_t) && col < line_size-1) {
			uint8x16_t data = vaddq_u8(
				vld1q_u8(es + i),
				vdupq_n_u8(42)
			);
			i += sizeof(uint8x16_t);
			// search for special chars
			uint8x16_t cmp = vorrq_u8(
				vorrq_u8(
					vceqq_u8(data, vdupq_n_u8(0)),
					vceqq_u8(data, vdupq_n_u8('\n'))
				),
				vorrq_u8(
					vceqq_u8(data, vdupq_n_u8('\r')),
					vceqq_u8(data, vdupq_n_u8('='))
				)
			);
			
			uint16_t mask = neon_movemask(cmp);
			if (mask != 0) {
				uint8_t m1 = mask & 0xFF;
				uint8_t m2 = mask >> 8;
				
				// perform lookup for shuffle mask
				uint8x16_t shufMA = vld1q_u8((uint8_t*)(shufLUT + m1));
				uint8x16_t shufMB = vld1q_u8((uint8_t*)(shufLUT + m2));
				
				
				// expand halves
#ifdef __aarch64__
				// second mask processes on second half, so add to the offsets
				shufMB = vaddq_u8(shufMB, vdupq_n_u8(8));
				
				uint8x16_t data2 = vqtbl1q_u8(data, shufMB);
				data = vqtbl1q_u8(data, shufMA);
#else
				uint8x16_t data2 = vcombine_u8(vtbl1_u8(vget_high_u8(data), vget_low_u8(shufMB)),
				                               vtbl1_u8(vget_high_u8(data), vget_high_u8(shufMB)));
				data = vcombine_u8(vtbl1_u8(vget_low_u8(data), vget_low_u8(shufMA)),
				                   vtbl1_u8(vget_low_u8(data), vget_high_u8(shufMA)));
#endif
				
				// add in escaped chars
				uint8x16_t shufMixMA = vld1q_u8((uint8_t*)(shufMixLUT + m1));
				uint8x16_t shufMixMB = vld1q_u8((uint8_t*)(shufMixLUT + m2));
				data = vaddq_u8(data, shufMixMA);
				data2 = vaddq_u8(data2, shufMixMB);
				// store out
				unsigned char shufALen = BitsSetTable256[m1] + 8;
				unsigned char shufBLen = BitsSetTable256[m2] + 8;
				vst1q_u8(p, data);
				p += shufALen;
				vst1q_u8(p, data2);
				p += shufBLen;
				col += shufALen + shufBLen;
				
				int ovrflowAmt = col - (line_size-1);
				if(ovrflowAmt > 0) {
					// we overflowed - find correct position to revert back to
					p -= ovrflowAmt;
					if(ovrflowAmt == shufBLen) {
						i -= 8;
						goto last_char_fast;
					} else {
						int isEsc;
						uint16_t tst;
						int offs = shufBLen - ovrflowAmt -1;
						if(ovrflowAmt > shufBLen) {
							tst = *(uint16_t*)((char*)(shufLUT+m1) + shufALen+offs);
							i -= 8;
						} else {
							tst = *(uint16_t*)((char*)(shufLUT+m2) + offs);
						}
						isEsc = (0xf0 == (tst&0xF0));
						p += isEsc;
						i -= 8 - ((tst>>8)&0xf) - isEsc;
						//col = line_size-1 + isEsc; // doesn't need to be set, since it's never read again
						if(isEsc)
							goto after_last_char_fast;
						else
							goto last_char_fast;
					}
				}
			} else {
				vst1q_u8(p, data);
				p += sizeof(uint8x16_t);
				col += sizeof(uint8x16_t);
				if(col > line_size-1) {
					p -= col - (line_size-1);
					i -= col - (line_size-1);
					//col = line_size-1; // doesn't need to be set, since it's never read again
					goto last_char_fast;
				}
			}
		}
		// handle remaining chars
		while(col < line_size-1) {
			c = es[i++], escaped = escapeLUT[c];
			if (escaped) {
				*(p++) = escaped;
				col++;
			}
			else {
				*(uint16_t*)p = escapedLUT[c];
				p += 2;
				col += 2;
			}
			if (i >= 0) goto end;
		}
		
		// last line char
		if(col < line_size) { // this can only be false if the last character was an escape sequence (or line_size is horribly small), in which case, we don't need to handle space/tab cases
			last_char_fast:
			c = es[i++];
			if (escapedLUT[c] && c != '.'-42) {
				*(uint16_t*)p = escapedLUT[c];
				p += 2;
			} else {
				*(p++) = c + 42;
			}
		}
		
		after_last_char_fast:
		if (i >= 0) break;
		
		c = es[i++];
		if (escapedLUT[c]) {
			*(uint32_t*)p = UINT32_16_PACK(UINT16_PACK('\r', '\n'), (uint32_t)escapedLUT[c]);
			p += 4;
			col = 2;
		} else {
			*(uint32_t*)p = UINT32_PACK('\r', '\n', (uint32_t)(c+42), 0);
			p += 3;
			col = 1;
		}
	}
	
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

#endif /* defined(__ARM_NEON) */

// runs at around 380MB/s on 2.4GHz Silvermont (worst: 125MB/s, best: 440MB/s)
static size_t do_encode_generic(int line_size, int* colOffset, const unsigned char* src, unsigned char* dest, size_t len) {
	unsigned char* es = (unsigned char*)src + len;
	unsigned char *p = dest; // destination pointer
	long i = -len; // input position
	unsigned char c, escaped; // input character; escaped input character
	int col = *colOffset;
	
	if (col == 0) {
		c = es[i++];
		if (escapedLUT[c]) {
			memcpy(p, &escapedLUT[c], sizeof(uint16_t));
			p += 2;
			col = 2;
		} else {
			*(p++) = c + 42;
			col = 1;
		}
	}
	while(i < 0) {
		unsigned char* sp = NULL;
		// main line
		#ifdef __SSE2__
		while (i < -1-XMM_SIZE && col < line_size-1) {
			__m128i data = _mm_add_epi8(
				_mm_loadu_si128((__m128i *)(es + i)), // probably not worth the effort to align
				_mm_set1_epi8(42)
			);
			// search for special chars
			// TODO: for some reason, GCC feels obliged to spill `data` onto the stack, then _load_ from it!
			__m128i cmp = _mm_or_si128(
				_mm_or_si128(
					_mm_cmpeq_epi8(data, _mm_setzero_si128()),
					_mm_cmpeq_epi8(data, _mm_set1_epi8('\n'))
				),
				_mm_or_si128(
					_mm_cmpeq_epi8(data, _mm_set1_epi8('\r')),
					_mm_cmpeq_epi8(data, _mm_set1_epi8('='))
				)
			);
			
			unsigned int mask = _mm_movemask_epi8(cmp);
			if (mask != 0) {
				sp = p;
				ALIGN_32(uint32_t mmTmp[4]);
				// special characters exist
				_mm_store_si128((__m128i*)mmTmp, data);
				#define DO_THING(n) \
					c = es[i+n], escaped = escapeLUT[c]; \
					if (escaped) \
						*(p+n) = escaped; \
					else { \
						memcpy(p+n, &escapedLUT[c], sizeof(uint16_t)); \
						p++; \
					}
				#define DO_THING_4(n) \
					if(mask & (0xF << n)) { \
						DO_THING(n); \
						DO_THING(n+1); \
						DO_THING(n+2); \
						DO_THING(n+3); \
					} else { \
						memcpy(p+n, &mmTmp[n>>2], sizeof(uint32_t)); \
					}
				DO_THING_4(0);
				DO_THING_4(4);
				DO_THING_4(8);
				DO_THING_4(12);
				p += XMM_SIZE;
				col += (int)(p - sp);
				
				if(col > line_size-1) {
					// TODO: consider revert optimisation from do_encode_fast
					// we overflowed - need to revert and use slower method :(
					col -= (int)(p - sp);
					p = sp;
					break;
				}
			} else {
				STOREU_XMM(p, data);
				p += XMM_SIZE;
				col += XMM_SIZE;
				if(col > line_size-1) {
					p -= col - (line_size-1);
					i += XMM_SIZE - (col - (line_size-1));
					//col = line_size-1; // never read again, doesn't need to be set
					goto last_char;
				}
			}
			
			i += XMM_SIZE;
		}
		#else
		while (i < -1-8 && line_size-col-1 > 8) {
			// 8 cycle unrolled version
			sp = p;
			#define DO_THING(n) \
				c = es[i+n], escaped = escapeLUT[c]; \
				if (escaped) \
					*(p++) = escaped; \
				else { \
					memcpy(p, &escapedLUT[c], sizeof(uint16_t)); \
					p += 2; \
				}
			DO_THING(0);
			DO_THING(1);
			DO_THING(2);
			DO_THING(3);
			DO_THING(4);
			DO_THING(5);
			DO_THING(6);
			DO_THING(7);
			
			i += 8;
			col += (int)(p - sp);
		}
		if(sp && col >= line_size-1) {
			// TODO: consider revert optimisation from do_encode_fast
			// we overflowed - need to revert and use slower method :(
			col -= (int)(p - sp);
			p = sp;
			i -= 8;
		}
		#endif
		// handle remaining chars
		while(col < line_size-1) {
			c = es[i++], escaped = escapeLUT[c];
			if (escaped) {
				*(p++) = escaped;
				col++;
			}
			else {
				memcpy(p, &escapedLUT[c], sizeof(uint16_t));
				p += 2;
				col += 2;
			}
			/* experimental branchless version 
			*p = '=';
			c = (es[i++] + 42) & 0xFF;
			int cond = (c=='\0' || c=='=' || c=='\r' || c=='\n');
			*(p+cond) = c + (cond << 6);
			p += 1+cond;
			col += 1+cond;
			*/
			if (i >= 0) goto end;
		}
		
		// last line char
		if(col < line_size) { // this can only be false if the last character was an escape sequence (or line_size is horribly small), in which case, we don't need to handle space/tab cases
			last_char:
			c = es[i++];
			if (escapedLUT[c] && c != '.'-42) {
				memcpy(p, &escapedLUT[c], sizeof(uint16_t));
				p += 2;
			} else {
				*(p++) = c + 42;
			}
		}
		
		if (i >= 0) break;
		
		c = es[i++];
		if (escapedLUT[c]) {
			uint32_t w = UINT32_16_PACK(UINT16_PACK('\r', '\n'), (uint32_t)escapedLUT[c]);
			memcpy(p, &w, sizeof(w));
			p += 4;
			col = 2;
		} else {
			// another option may be to just write the EOL and let the first char be handled by the faster methods above, but it appears that writing the extra byte here is generally faster...
			uint32_t w = UINT32_PACK('\r', '\n', (uint32_t)(c+42), 0);
			memcpy(p, &w, sizeof(w));
			p += 3;
			col = 1;
		}
	}
	
	end:
	// special case: if the last character is a space/tab, it needs to be escaped as it's the final character on the line
	unsigned char lc = *(p-1);
	if(lc == '\t' || lc == ' ') {
		*(p-1) = '=';
		*p = lc+64;
		p++;
		col++;
	}
	*colOffset = col;
	return p - dest;
}


// slightly faster version which improves the worst case scenario significantly; since worst case doesn't happen often, overall speedup is relatively minor
// requires PSHUFB (SSSE3) instruction, but will use POPCNT (SSE4.2 (or AMD's ABM, but Phenom doesn't support SSSE3 so doesn't matter)) if available (these only seem to give minor speedups, so considered optional)
#ifdef __SSSE3__
ALIGN_32(__m128i _shufLUT[258]); // +2 for underflow guard entry
__m128i* shufLUT = _shufLUT+2;
ALIGN_32(__m128i shufMixLUT[256]);
static size_t do_encode_ssse3(int line_size, int* colOffset, const unsigned char* src, unsigned char* dest, size_t len) {
	unsigned char* es = (unsigned char*)src + len;
	unsigned char *p = dest; // destination pointer
	long i = -len; // input position
	unsigned char c, escaped; // input character; escaped input character
	int col = *colOffset;
	
	if (col == 0) {
		c = es[i++];
		if (escapedLUT[c]) {
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
		while (i < -1-XMM_SIZE && col < line_size-1) {
			__m128i data = _mm_add_epi8(
				_mm_loadu_si128((__m128i *)(es + i)), // probably not worth the effort to align
				_mm_set1_epi8(42)
			);
			i += XMM_SIZE;
			// search for special chars
			__m128i cmp = _mm_or_si128(
				_mm_or_si128(
					_mm_cmpeq_epi8(data, _mm_setzero_si128()),
					_mm_cmpeq_epi8(data, _mm_set1_epi8('\n'))
				),
				_mm_or_si128(
					_mm_cmpeq_epi8(data, _mm_set1_epi8('\r')),
					_mm_cmpeq_epi8(data, _mm_set1_epi8('='))
				)
			);
			
			unsigned int mask = _mm_movemask_epi8(cmp);
			if (mask != 0) { // seems to always be faster than _mm_test_all_zeros, possibly because http://stackoverflow.com/questions/34155897/simd-sse-how-to-check-that-all-vector-elements-are-non-zero#comment-62475316
				uint8_t m1 = mask & 0xFF;
				uint8_t m2 = mask >> 8;
				
#if defined(__AVX512VBMI2__) && defined(__BMI2__) && 0
				// TODO: consider expanding into 256b vector
				__m128i data2 = _mm_mask_expand_epi8(_mm_set1_epi8('='), ~m2, _mm_srli_si128(data, 8));
				data = _mm_mask_expand_epi8(_mm_set1_epi8('='), ~m1, data);
				data = _mm_mask_add_epi8(data, _pdep_u32(m1, ~m1), data, _mm_set1_epi8(64));
				data2 = _mm_mask_add_epi8(data2, _pdep_u32(m2, ~m2), data, _mm_set1_epi8(64));
#else
				// perform lookup for shuffle mask
				__m128i shufMA = _mm_load_si128(shufLUT + m1);
				__m128i shufMB = _mm_load_si128(shufLUT + m2);
				
				// second mask processes on second half, so add to the offsets
				// this seems to be faster than right-shifting data by 8 bytes on Intel chips, maybe due to psrldq always running on port5? may be different on AMD
				shufMB = _mm_add_epi8(shufMB, _mm_set1_epi8(8));
				
				// expand halves
				//shuf = _mm_or_si128(_mm_cmpgt_epi8(shuf, _mm_set1_epi8(15)), shuf);
				__m128i data2 = _mm_shuffle_epi8(data, shufMB);
				data = _mm_shuffle_epi8(data, shufMA);
				
				// add in escaped chars
				__m128i shufMixMA = _mm_load_si128(shufMixLUT + m1);
				__m128i shufMixMB = _mm_load_si128(shufMixLUT + m2);
				data = _mm_add_epi8(data, shufMixMA);
				data2 = _mm_add_epi8(data2, shufMixMB);
#endif
				// store out
#if defined(__POPCNT__) && (defined(__tune_znver1__) || defined(__tune_btver2__))
				unsigned char shufALen = _mm_popcnt_u32(m1) + 8;
				unsigned char shufBLen = _mm_popcnt_u32(m2) + 8;
#else
				unsigned char shufALen = BitsSetTable256[m1] + 8;
				unsigned char shufBLen = BitsSetTable256[m2] + 8;
#endif
				STOREU_XMM(p, data);
				p += shufALen;
				STOREU_XMM(p, data2);
				p += shufBLen;
				col += shufALen + shufBLen;
				
				int ovrflowAmt = col - (line_size-1);
				if(ovrflowAmt > 0) {
					// we overflowed - find correct position to revert back to
					p -= ovrflowAmt;
					if(ovrflowAmt == shufBLen) {
						i -= 8;
						goto last_char_fast;
					} else {
						int isEsc;
						uint16_t tst;
						int offs = shufBLen - ovrflowAmt -1;
						if(ovrflowAmt > shufBLen) {
							tst = *(uint16_t*)((char*)(shufLUT+m1) + shufALen+offs);
							i -= 8;
						} else {
							tst = *(uint16_t*)((char*)(shufLUT+m2) + offs);
						}
						isEsc = (0xf0 == (tst&0xF0));
						p += isEsc;
						i -= 8 - ((tst>>8)&0xf) - isEsc;
						//col = line_size-1 + isEsc; // doesn't need to be set, since it's never read again
						if(isEsc)
							goto after_last_char_fast;
						else
							goto last_char_fast;
					}
				}
			} else {
				STOREU_XMM(p, data);
				p += XMM_SIZE;
				col += XMM_SIZE;
				if(col > line_size-1) {
					p -= col - (line_size-1);
					i -= col - (line_size-1);
					//col = line_size-1; // doesn't need to be set, since it's never read again
					goto last_char_fast;
				}
			}
		}
		// handle remaining chars
		while(col < line_size-1) {
			c = es[i++], escaped = escapeLUT[c];
			if (escaped) {
				*(p++) = escaped;
				col++;
			}
			else {
				*(uint16_t*)p = escapedLUT[c];
				p += 2;
				col += 2;
			}
			if (i >= 0) goto end;
		}
		
		// last line char
		if(col < line_size) { // this can only be false if the last character was an escape sequence (or line_size is horribly small), in which case, we don't need to handle space/tab cases
			last_char_fast:
			c = es[i++];
			if (escapedLUT[c] && c != '.'-42) {
				*(uint16_t*)p = escapedLUT[c];
				p += 2;
			} else {
				*(p++) = c + 42;
			}
		}
		
		after_last_char_fast:
		if (i >= 0) break;
		
		c = es[i++];
		if (escapedLUT[c]) {
			*(uint32_t*)p = UINT32_16_PACK(UINT16_PACK('\r', '\n'), (uint32_t)escapedLUT[c]);
			p += 4;
			col = 2;
		} else {
			*(uint32_t*)p = UINT32_PACK('\r', '\n', (uint32_t)(c+42), 0);
			p += 3;
			col = 1;
		}
	}
	
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

/*
// experimental AVX2 version
// seems to be slower than SSSE3 variant, so not used at the moment; with experimental optimisations, is faster on Haswell, but only mildly so
#ifdef __AVX2__
#define YMM_SIZE 32
static size_t do_encode_avx2(int line_size, int* colOffset, const unsigned char* src, unsigned char* dest, size_t len) {
	unsigned char* es = (unsigned char*)src + len;
	unsigned char *p = dest; // destination pointer
	long i = -len; // input position
	unsigned char c, escaped; // input character; escaped input character
	int col = *colOffset;
	
	if (col == 0) {
		c = es[i++];
		if (escapedLUT[c]) {
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
			__m256i data = _mm256_add_epi8(
				_mm256_loadu_si256((__m256i *)(es + i)),
				_mm256_set1_epi8(42)
			);
			i += YMM_SIZE;
			// search for special chars
			__m256i cmp = _mm256_or_si256(
				_mm256_or_si256(
					_mm256_cmpeq_epi8(data, _mm256_setzero_si256()),
					_mm256_cmpeq_epi8(data, _mm256_set1_epi8('\n'))
				),
				_mm256_or_si256(
					_mm256_cmpeq_epi8(data, _mm256_set1_epi8('\r')),
					_mm256_cmpeq_epi8(data, _mm256_set1_epi8('='))
				)
			);
			
			unsigned int mask = _mm256_movemask_epi8(cmp);
			if (mask != 0) {
				uint8_t m1 = mask & 0xFF;
				uint8_t m2 = (mask >> 8) & 0xFF;
				uint8_t m3 = (mask >> 16) & 0xFF;
				uint8_t m4 = mask >> 24;
				
				// perform lookup for shuffle mask
				// note that we interlave 1/3, 2/4 to make processing easier
				// TODO: any way to ensure that these loads use AVX?
				__m256i shufMA = _mm256_inserti128_si256(
					_mm256_castsi128_si256(shufLUT[m1]),
					shufLUT[m3],
					1
				);
				__m256i shufMB = _mm256_inserti128_si256(
					_mm256_castsi128_si256(shufLUT[m2]),
					shufLUT[m4],
					1
				);
				
				// offset second mask
				shufMB = _mm256_add_epi8(shufMB, _mm256_set1_epi8(8));
				
				// expand halves
				__m256i data1 = _mm256_shuffle_epi8(data, shufMA);
				__m256i data2 = _mm256_shuffle_epi8(data, shufMB);
				
				// add in escaped chars
				__m256i shufMixMA = _mm256_inserti128_si256(
					_mm256_castsi128_si256(shufMixLUT[m1]),
					shufMixLUT[m3],
					1
				);
				__m256i shufMixMB = _mm256_inserti128_si256(
					_mm256_castsi128_si256(shufMixLUT[m2]),
					shufMixLUT[m4],
					1
				);
				data = _mm256_add_epi8(data, shufMixMA);
				data2 = _mm256_add_epi8(data2, shufMixMB);
				// store out
				unsigned char shuf1Len = _mm_popcnt_u32(m1) + 8;
				unsigned char shuf2Len = _mm_popcnt_u32(m2) + 8;
				unsigned char shuf3Len = _mm_popcnt_u32(m3) + 8;
				unsigned char shuf4Len = _mm_popcnt_u32(m4) + 8;
				// TODO: do these stores always use AVX?
				// TODO: this can overflow since we only give +32 chars for over-allocation
				_mm_storeu_si128((__m128i*)p, _mm256_castsi256_si128(data1));
				p += shuf1Len;
				_mm_storeu_si128((__m128i*)p, _mm256_castsi256_si128(data2));
				p += shuf2Len;
				_mm_storeu_si128((__m128i*)p, _mm256_extracti128_si256(data1, 1));
				p += shuf3Len;
				_mm_storeu_si128((__m128i*)p, _mm256_extracti128_si256(data2, 1));
				p += shuf4Len;
				col += shuf1Len + shuf2Len + shuf3Len + shuf4Len;
				
				if(col > line_size-1) {
					// we overflowed - may need to revert and use slower method :(
					// TODO: optimize this
					col -= shuf1Len + shuf2Len + shuf3Len + shuf4Len;
					p -= shuf1Len + shuf2Len + shuf3Len + shuf4Len;
					i -= YMM_SIZE;
					break;
				}
			} else {
				_mm256_storeu_si256((__m256i*)p, data);
				p += YMM_SIZE;
				col += YMM_SIZE;
				if(col > line_size-1) {
					p -= col - (line_size-1);
					i -= col - (line_size-1);
					col = line_size-1;
					goto last_char_avx2;
				}
			}
		}
		// handle remaining chars
		while(col < line_size-1) {
			c = es[i++], escaped = escapeLUT[c];
			if (escaped) {
				*(p++) = escaped;
				col++;
			}
			else {
				*(uint16_t*)p = escapedLUT[c];
				p += 2;
				col += 2;
			}
			if (i >= 0) goto end;
		}
		
		// last line char
		if(col < line_size) { // this can only be false if the last character was an escape sequence (or line_size is horribly small), in which case, we don't need to handle space/tab cases
			last_char_avx2:
			c = es[i++];
			if (escapedLUT[c] && c != '.'-42) {
				*(uint16_t*)p = escapedLUT[c];
				p += 2;
			} else {
				*(p++) = c + 42;
			}
		}
		
		if (i >= 0) break;
		
		c = es[i++];
		if (escapedLUT[c]) {
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
*/



ALIGN_32(static const uint8_t _pshufb_shift_table[272]) = {
	0x80,0x81,0x82,0x83,0x84,0x85,0x86,0x87,0x88,0x89,0x8a,0x8b,0x8c,0x8d,0x8e,0x8f,
	0x0f,0x80,0x81,0x82,0x83,0x84,0x85,0x86,0x87,0x88,0x89,0x8a,0x8b,0x8c,0x8d,0x8e,
	0x0e,0x0f,0x80,0x81,0x82,0x83,0x84,0x85,0x86,0x87,0x88,0x89,0x8a,0x8b,0x8c,0x8d,
	0x0d,0x0e,0x0f,0x80,0x81,0x82,0x83,0x84,0x85,0x86,0x87,0x88,0x89,0x8a,0x8b,0x8c,
	0x0c,0x0d,0x0e,0x0f,0x80,0x81,0x82,0x83,0x84,0x85,0x86,0x87,0x88,0x89,0x8a,0x8b,
	0x0b,0x0c,0x0d,0x0e,0x0f,0x80,0x81,0x82,0x83,0x84,0x85,0x86,0x87,0x88,0x89,0x8a,
	0x0a,0x0b,0x0c,0x0d,0x0e,0x0f,0x80,0x81,0x82,0x83,0x84,0x85,0x86,0x87,0x88,0x89,
	0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f,0x80,0x81,0x82,0x83,0x84,0x85,0x86,0x87,0x88,
	0x08,0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f,0x80,0x81,0x82,0x83,0x84,0x85,0x86,0x87,
	0x07,0x08,0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f,0x80,0x81,0x82,0x83,0x84,0x85,0x86,
	0x06,0x07,0x08,0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f,0x80,0x81,0x82,0x83,0x84,0x85,
	0x05,0x06,0x07,0x08,0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f,0x80,0x81,0x82,0x83,0x84,
	0x04,0x05,0x06,0x07,0x08,0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f,0x80,0x81,0x82,0x83,
	0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f,0x80,0x81,0x82,
	0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f,0x80,0x81,
	0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f,0x80,
	0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f
};
static const __m128i* pshufb_shift_table = (const __m128i*)_pshufb_shift_table;

// assumes line_size is reasonably large (probably >32)
size_t do_encode_fast2(int line_size, int* colOffset, const unsigned char* src, unsigned char* dest, size_t len) {
	// TODO: not ideal; leave here so that tests pass
	if(line_size < 32) return do_encode_ssse3(line_size, colOffset, src, dest, len);
	
	unsigned char *p = dest; // destination pointer
	unsigned long i = 0; // input position
	unsigned char c; // input character; escaped input character
	int col = *colOffset;
	
	__m128i escFirstChar = _mm_set_epi8(0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,64);
	
	// firstly, align reader
	for (; (uintptr_t)(src+i) & 0xF; i++) {
		if(i >= len) goto encode_fast2_end;
		c = (src[i] + 42) & 0xFF;
		switch(c) {
			case '.':
				if(col > 0) break;
			case '\t': case ' ':
				if(col > 0 && col < line_size-1) break;
			case '\0': case '\r': case '\n': case '=':
				*(p++) = '=';
				c += 64;
				col++;
		}
		*(p++) = c;
		col++;
		if(col >= line_size && i+1 < len) {
			*(uint16_t*)p = UINT16_PACK('\r', '\n');
			p += 2;
			col = 0;
		}
	}
	
	if(len-i-1 > XMM_SIZE) {
		__m128i input = _mm_load_si128((__m128i *)(src + i));
		
		if (col == 0) {
			// first char in line
			c = src[i];
			if (escapedLUT[c]) {
				*p++ = '=';
				col = 1;
				
				input = _mm_add_epi8(input, escFirstChar);
			}
		}
		do {
			__m128i data = _mm_add_epi8(input, _mm_set1_epi8(42));
			i += XMM_SIZE;
			// search for special chars
			__m128i cmp = _mm_or_si128(
				_mm_or_si128(
					_mm_cmpeq_epi8(data, _mm_setzero_si128()),
					_mm_cmpeq_epi8(data, _mm_set1_epi8('\n'))
				),
				_mm_or_si128(
					_mm_cmpeq_epi8(data, _mm_set1_epi8('\r')),
					_mm_cmpeq_epi8(data, _mm_set1_epi8('='))
				)
			);
			
			unsigned int mask = _mm_movemask_epi8(cmp);
			if (mask != 0) { // seems to always be faster than _mm_test_all_zeros, possibly because http://stackoverflow.com/questions/34155897/simd-sse-how-to-check-that-all-vector-elements-are-non-zero#comment-62475316
				uint8_t m1 = mask & 0xFF;
				uint8_t m2 = mask >> 8;
				
				// perform lookup for shuffle mask
				__m128i shufMA = _mm_load_si128(shufLUT + m1);
				__m128i shufMB = _mm_load_si128(shufLUT + m2);
				
				// second mask processes on second half, so add to the offsets
				// this seems to be faster than right-shifting data by 8 bytes on Intel chips, maybe due to psrldq always running on port5? may be different on AMD
				shufMB = _mm_add_epi8(shufMB, _mm_set1_epi8(8));
				
				// expand halves
				__m128i data2 = _mm_shuffle_epi8(data, shufMB);
				data = _mm_shuffle_epi8(data, shufMA);
				
				// add in escaped chars
				__m128i shufMixMA = _mm_load_si128(shufMixLUT + m1);
				__m128i shufMixMB = _mm_load_si128(shufMixLUT + m2);
				data = _mm_add_epi8(data, shufMixMA);
				data2 = _mm_add_epi8(data2, shufMixMB);
				// store out
#if defined(__POPCNT__) && (defined(__tune_znver1__) || defined(__tune_btver2__))
				unsigned char shufALen = _mm_popcnt_u32(m1) + 8;
				unsigned char shufBLen = _mm_popcnt_u32(m2) + 8;
#else
				unsigned char shufALen = BitsSetTable256[m1] + 8;
				unsigned char shufBLen = BitsSetTable256[m2] + 8;
#endif
				STOREU_XMM(p, data);
				p += shufALen;
				STOREU_XMM(p, data2);
				p += shufBLen;
				col += shufALen + shufBLen;
				
				int ovrflowAmt = col - (line_size-1);
				if(ovrflowAmt > 0) {
					// we overflowed - find correct position to revert back to
					p -= ovrflowAmt;
					if(ovrflowAmt == shufBLen) {
						c = src[i-8];
						// TODO: consider doing direct comparisons instead of lookup
						if (escapedLUT[c] && c != '.'-42) {
							// if data2's version is escaped, we shift out by 2, otherwise only by 1
							if(m2 & 1) {
								data2 = _mm_srli_si128(data2, 1);
								ovrflowAmt--;
							} else
								*(uint16_t*)p = escapedLUT[c];
							p += 2;
						} else {
							p++;
							// shift data2 by one (actually will be combined below)
						}
						ovrflowAmt--;
						
						c = src[i-7];
						if (escapedLUT[c] && !(m2 & 2)) { // if the character was originally escaped, we can just fallback to storing it straight out
							col = ovrflowAmt+1;
							data2 = _mm_srli_si128(data2, 1+1);
							
							*(uint32_t*)p = UINT32_16_PACK(UINT16_PACK('\r', '\n'), (uint32_t)escapedLUT[c]);
							p += 4;
							ovrflowAmt--;
						} else {
							*(uint16_t*)p = UINT16_PACK('\r', '\n');
							col = ovrflowAmt;
							data2 = _mm_srli_si128(data2, 1);
							p += 2;
						}
						STOREU_XMM(p, data2);
						p += ovrflowAmt;
					} else {
						int isEsc, lastIsEsc;
						uint16_t tst;
						int offs = shufBLen - ovrflowAmt -1;
						unsigned long tmpInPos = i;
						if(ovrflowAmt > shufBLen) {
							// ! Note that it's possible for shufALen+offs == -1 to be true !!
							// although the way the lookup tables are constructed (with the additional guard entry), this isn't a problem, but it's not ideal; TODO: consider proper fix
							tst = *(uint16_t*)((char*)(shufLUT+m1) + shufALen+offs);
							tmpInPos -= 8;
						} else {
							tst = *(uint16_t*)((char*)(shufLUT+m2) + offs);
						}
						isEsc = (0xf0 == (tst&0xF0));
						tmpInPos -= 8 - ((tst>>8)&0xf) - isEsc;
						
						lastIsEsc = 0;
						if(!isEsc) {
							//lastIsEsc = (mask & (1 << (16-(i-tmpInPos)))) ? 1:0; // TODO: use this?
							c = src[tmpInPos++];
							// TODO: consider doing direct comparisons instead of lookup
							if (escapedLUT[c] && c != '.'-42) {
								*(uint16_t*)p = escapedLUT[c];
								p++;
								
								lastIsEsc = escapeLUT[c] ? 0:1;
							}
						}
						p++;
						
						
						//offs = offs + 1 - (1+lastIsEsc);
						if(ovrflowAmt > shufBLen) {
							ovrflowAmt -= 1+lastIsEsc;
							__m128i shiftThing = _mm_load_si128(&pshufb_shift_table[16 - (shufALen+shufBLen - ovrflowAmt)]);
							data = _mm_shuffle_epi8(data, shiftThing);
							shufALen = ovrflowAmt-shufBLen;
						} else {
							// TODO: theoretically, everything in the 2nd half can be optimized better, but requires a lot more code paths :|
							ovrflowAmt -= 1+lastIsEsc;
							__m128i shiftThing = _mm_load_si128(&pshufb_shift_table[16 - (shufBLen - ovrflowAmt)]);
							data2 = _mm_shuffle_epi8(data2, shiftThing);
							shufBLen = ovrflowAmt;
						}
						
						if(tmpInPos >= len) goto encode_fast2_end; // TODO: remove conditional by pre-checking this
						
						c = src[tmpInPos];
						if(ovrflowAmt > 0) {
							isEsc = mask & (1 << (16-(i-tmpInPos)));
							if(ovrflowAmt > shufBLen) {
								if (escapedLUT[c] && !isEsc) {
									*(uint32_t*)p = UINT32_16_PACK(UINT16_PACK('\r', '\n'), (uint32_t)escapedLUT[c]);
									col = 2/*escaped char*/+shufALen-1/*previously not escaped char*/;
									data = _mm_srli_si128(data, 1);
									p += 4;
									shufALen--;
								} else {
									*(uint16_t*)p = UINT16_PACK('\r', '\n');
									col = shufALen;
									p += 2;
								}
								
								STOREU_XMM(p, data);
								p += shufALen;
							} else {
								if (escapedLUT[c] && !isEsc) {
									*(uint32_t*)p = UINT32_16_PACK(UINT16_PACK('\r', '\n'), (uint32_t)escapedLUT[c]);
									col = 2;
									p += 4;
									shufBLen--;
									data2 = _mm_srli_si128(data2, 1);
								} else {
									*(uint16_t*)p = UINT16_PACK('\r', '\n');
									col = 0;
									p += 2;
								}
							}
							STOREU_XMM(p, data2);
							p += shufBLen;
							col += shufBLen;
						} else {
							if (escapedLUT[c]) {
								// check if we have enough bytes to read
								if(len-i-1 <= XMM_SIZE) {
									*(uint16_t*)p = UINT16_PACK('\r', '\n');
									col = 0;
									p += 2;
									break;
								}
								
								// we've now got a rather problematic case to handle... :|
								*(uint32_t*)p = UINT32_PACK('\r', '\n', '=', 0);
								p += 3;
								col = 1;
								
								// ewww....
								input = _mm_load_si128((__m128i *)(src + i));
								// hack XMM input to fool regular code into writing the correct character
								input = _mm_add_epi8(input, escFirstChar);
								continue;
							} else {
								*(uint16_t*)p = UINT16_PACK('\r', '\n');
								col = 0;
								p += 2;
							}
						}
					}
				}
			} else {
				STOREU_XMM(p, data);
				p += XMM_SIZE;
				col += XMM_SIZE;
				int ovrflowAmt = col - (line_size-1);
				if(ovrflowAmt > 0) {
					// optimisation: check last char here
					c = src[i - ovrflowAmt];
					ovrflowAmt--;
					// TODO: consider doing direct comparisons instead of lookup
					if (escapedLUT[c] && c != '.'-42) {
						p -= ovrflowAmt-1;
						*(uint16_t*)(p-2) = escapedLUT[c];
					} else {
						p -= ovrflowAmt;
					}
					
					if(i-ovrflowAmt >= len) goto encode_fast2_end; // TODO: remove conditional by pre-checking this
					
					c = src[i - ovrflowAmt];
					if(ovrflowAmt != 0) {
						int dataLen;
						if (escapedLUT[c]) {
							*(uint32_t*)p = UINT32_16_PACK(UINT16_PACK('\r', '\n'), (uint32_t)escapedLUT[c]);
							col = 2+ovrflowAmt-1;
							dataLen = ovrflowAmt-1;
							data = _mm_srli_si128(data, 1);
							p += 4;
						} else {
							*(uint16_t*)p = UINT16_PACK('\r', '\n');
							col = ovrflowAmt;
							dataLen = ovrflowAmt;
							p += 2;
						}
						
						// shuffle remaining elements across
						__m128i shiftThing = _mm_load_si128(&pshufb_shift_table[ovrflowAmt]);
						data = _mm_shuffle_epi8(data, shiftThing);
						// store out data
						STOREU_XMM(p, data);
						p += dataLen;
					} else {
						if (escapedLUT[c]) { // will also handle case which would be handled fine normally, but since we're checking it...
							// ugly hacky code
							// check if we have enough bytes to read
							if(len-i-1 <= XMM_SIZE) {
								*(uint16_t*)p = UINT16_PACK('\r', '\n');
								col = 0;
								p += 2;
								break;
							}
							
							*(uint32_t*)p = UINT32_PACK('\r', '\n', '=', 0);
							col = 1;
							p += 3;
							
							// ewww....
							input = _mm_load_si128((__m128i *)(src + i));
							// hack XMM input to fool regular code into writing the correct character
							input = _mm_add_epi8(input, escFirstChar);
							continue;
							
						} else {
							*(uint16_t*)p = UINT16_PACK('\r', '\n');
							col = 0;
							p += 2;
						}
					}
				}
			}
			input = _mm_load_si128((__m128i *)(src + i));
		} while(len-i-1 > XMM_SIZE);
	}
	
	if(col == 0) {
		c = src[i++];
		if (escapedLUT[c]) {
			*(uint16_t*)p = escapedLUT[c];
			p += 2;
			col = 2;
		} else {
			*(p++) = c + 42;
			col = 1;
		}
	}
	while(i < len) {
		while(col < line_size-1) {
			c = src[i++];
			if (escapeLUT[c]) {
				*(p++) = escapeLUT[c];
				col++;
			}
			else {
				*(uint16_t*)p = escapedLUT[c];
				p += 2;
				col += 2;
			}
			if (i >= len) goto encode_fast2_end;
		}
		
		// last line char
		// TODO: consider combining with above
		if(col < line_size) { // this can only be false if the last character was an escape sequence (or line_size is horribly small), in which case, we don't need to handle space/tab cases
			c = src[i++];
			if (escapedLUT[c] && c != '.'-42) {
				*(uint16_t*)p = escapedLUT[c];
				p += 2;
			} else {
				*(p++) = c + 42;
			}
		}
		
		if (i >= len) break;
		
		c = src[i++];
		if (escapedLUT[c]) {
			*(uint32_t*)p = UINT32_16_PACK(UINT16_PACK('\r', '\n'), (uint32_t)escapedLUT[c]);
			p += 4;
			col = 2;
		} else {
			*(uint32_t*)p = UINT32_PACK('\r', '\n', (uint32_t)(c+42), 0);
			p += 3;
			col = 1;
		}
	}
	
	
	encode_fast2_end:
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


/*
// simple naive implementation - most yEnc encoders I've seen do something like the following
// runs at around 145MB/s on 2.4GHz Silvermont (worst: 135MB/s, best: 158MB/s)
static size_t do_encode_scalar(int line_size, int* colOffset, const unsigned char* src, unsigned char* dest, size_t len) {
	unsigned char *p = dest;
	int col = *colOffset;
	
	for (unsigned long i = 0; i < len; i++) {
		unsigned char c = (src[i] + 42) & 0xFF;
		switch(c) {
			case '.':
				if(col > 0) break;
			case '\t': case ' ':
				if(col > 0 && col < line_size-1) break;
			case '\0': case '\r': case '\n': case '=':
				*(p++) = '=';
				c += 64;
				col++;
		}
		*(p++) = c;
		col++;
		if(col >= line_size && i+1 < len) {
			*(uint16_t*)p = UINT16_PACK('\r', '\n');
			p += 2;
			col = 0;
		}
	}
	
	// special case: if the last character is a space/tab, it needs to be escaped as it's the final character on the line
	unsigned char lc = *(p-1);
	if(lc == '\t' || lc == ' ') {
		*(uint16_t*)(p-1) = UINT16_PACK('=', lc+64);
		p++;
	}
	*colOffset = col;
	return p - dest;
}
*/

size_t (*_do_encode)(int, int*, const unsigned char*, unsigned char*, size_t) = &do_encode_generic;


void encoder_init() {
	for (int i=0; i<256; i++) {
		escapeLUT[i] = (i+42) & 0xFF;
		escapedLUT[i] = 0;
	}
	escapeLUT[214 + '\0'] = 0;
	escapeLUT[214 + '\r'] = 0;
	escapeLUT[214 + '\n'] = 0;
	escapeLUT['=' - 42	] = 0;
	
	escapedLUT[214 + '\0'] = UINT16_PACK('=', '\0'+64);
	escapedLUT[214 + '\r'] = UINT16_PACK('=', '\r'+64);
	escapedLUT[214 + '\n'] = UINT16_PACK('=', '\n'+64);
	escapedLUT['=' - 42	] =  UINT16_PACK('=', '='+64);
	escapedLUT[214 + '\t'] = UINT16_PACK('=', '\t'+64);
	escapedLUT[214 + ' ' ] = UINT16_PACK('=', ' '+64);
	escapedLUT['.' - 42	] =  UINT16_PACK('=', '.'+64);
	
#ifdef __SSSE3__
	if((cpu_flags() & CPU_SHUFFLE_FLAGS) == CPU_SHUFFLE_FLAGS) {
		_do_encode = &do_encode_ssse3;
		// generate shuf LUT
		for(int i=0; i<256; i++) {
			int k = i;
			uint8_t res[16];
			int p = 0;
			for(int j=0; j<8; j++) {
				if(k & 1) {
					res[j+p] = 0xf0 + j;
					p++;
				}
				res[j+p] = j;
				k >>= 1;
			}
			for(; p<8; p++)
				res[8+p] = 8+p +0x80; // +0x80 causes PSHUFB to 0 discarded entries; has no effect other than to ease debugging
			
			__m128i shuf = _mm_loadu_si128((__m128i*)res);
			_mm_store_si128(shufLUT + i, shuf);
			
			// calculate add mask for mixing escape chars in
			__m128i maskEsc = _mm_cmpeq_epi8(_mm_and_si128(shuf, _mm_set1_epi8(0xf0)), _mm_set1_epi8(0xf0));
			__m128i addMask = _mm_and_si128(_mm_slli_si128(maskEsc, 1), _mm_set1_epi8(64));
			addMask = _mm_or_si128(addMask, _mm_and_si128(maskEsc, _mm_set1_epi8('=')));
			
			_mm_store_si128(shufMixLUT + i, addMask);
		}
		// underflow guard entries; this may occur when checking for escaped characters, when the shufLUT[0] and shufLUT[-1] are used for testing
		_mm_store_si128(_shufLUT +0, _mm_set1_epi8(0xFF));
		_mm_store_si128(_shufLUT +1, _mm_set1_epi8(0xFF));
	}
#endif
#ifdef __ARM_NEON
	if(cpu_supports_neon()) {
		_do_encode = &do_encode_neon;
		// generate shuf LUT
		for(int i=0; i<256; i++) {
			int k = i;
			uint8_t res[16];
			int p = 0;
			for(int j=0; j<8; j++) {
				if(k & 1) {
					res[j+p] = 0xf0 + j;
					p++;
				}
				res[j+p] = j;
				k >>= 1;
			}
			for(; p<8; p++)
				res[8+p] = 8+p +0x80; // +0x80 => 0 discarded entries; has no effect other than to ease debugging
			
			uint8x16_t shuf = vld1q_u8(res);
			vst1q_u8((uint8_t*)(shufLUT + i), shuf);
			
			// calculate add mask for mixing escape chars in
			uint8x16_t maskEsc = vceqq_u8(vandq_u8(shuf, vdupq_n_u8(0xf0)), vdupq_n_u8(0xf0));
			uint8x16_t addMask = vandq_u8(vextq_u8(vdupq_n_u8(0), maskEsc, 15), vdupq_n_u8(64));
			addMask = vorrq_u8(addMask, vandq_u8(maskEsc, vdupq_n_u8('=')));
			
			vst1q_u8((uint8_t*)(shufMixLUT + i), addMask);
		}
		// underflow guard entries; this may occur when checking for escaped characters, when the shufLUT[0] and shufLUT[-1] are used for testing
		vst1q_u8((uint8_t*)(_shufLUT +0), vdupq_n_u8(0xFF));
		vst1q_u8((uint8_t*)(_shufLUT +1), vdupq_n_u8(0xFF));
	}
#endif

}