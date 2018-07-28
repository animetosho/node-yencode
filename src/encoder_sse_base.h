#include "common.h"

// slightly faster version which improves the worst case scenario significantly; since worst case doesn't happen often, overall speedup is relatively minor
// requires PSHUFB (SSSE3) instruction, but will use POPCNT (SSE4.2 (or AMD's ABM, but Phenom doesn't support SSSE3 so doesn't matter)) if available (these only seem to give minor speedups, so considered optional)
#include "encoder.h"

#ifdef __SSSE3__
ALIGN_32(static __m128i _shufLUT[258]); // +2 for underflow guard entry
static __m128i* shufLUT = _shufLUT+2;
ALIGN_32(static __m128i shufMixLUT[256]);

static uint16_t expandLUT[256];
#endif

static const unsigned char* escapeLUT;
static const uint16_t* escapedLUT;


static size_t do_encode_sse(int line_size, int* colOffset, const unsigned char* src, unsigned char* dest, size_t len) {
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
#ifdef __AVX512VL__
			__m128i cmp = _mm_ternarylogic_epi32(
				_mm_or_si128(
					_mm_cmpeq_epi8(data, _mm_setzero_si128()),
					_mm_cmpeq_epi8(data, _mm_set1_epi8('\n'))
				),
				_mm_cmpeq_epi8(data, _mm_set1_epi8('=')),
				_mm_cmpeq_epi8(data, _mm_set1_epi8('\r')),
				0xFE
			);
#else
			__m128i cmp = _mm_or_si128(
				_mm_or_si128(
					_mm_cmpeq_epi8(data, _mm_setzero_si128()),
					_mm_cmpeq_epi8(data, _mm_set1_epi8('\n'))
				),
				_mm_or_si128(
					_mm_cmpeq_epi8(data, _mm_set1_epi8('=')),
					_mm_cmpeq_epi8(data, _mm_set1_epi8('\r'))
				)
			);
#endif
			
			unsigned int mask = _mm_movemask_epi8(cmp);
			if (mask != 0) { // seems to always be faster than _mm_test_all_zeros, possibly because http://stackoverflow.com/questions/34155897/simd-sse-how-to-check-that-all-vector-elements-are-non-zero#comment-62475316
#ifdef __SSSE3__
				uint8_t m1 = mask & 0xFF;
				uint8_t m2 = mask >> 8;
				
# if defined(__AVX512VBMI2__) && defined(__AVX512VL__) && defined(__AVX512BW__) && 0
				// TODO: will need to see if this is actually faster; vpexpand* is rather slow on SKX, even for 128b ops, so this could be slower
				// on SKX, mask-shuffle is faster than expand, but requires loading shuffle masks, and ends up being slower than the SSSE3 method
				data = _mm_mask_add_epi8(data, mask, data, _mm_set1_epi8(64));
				__m128i data2 = _mm_mask_expand_epi8(_mm_set1_epi8('='), expandLUT[m2], _mm_srli_si128(data, 8));
				data = _mm_mask_expand_epi8(_mm_set1_epi8('='), expandLUT[m1], data);
# else
				
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
# endif
				// store out
# if defined(__POPCNT__) && (defined(__tune_znver1__) || defined(__tune_btver2__))
				unsigned char shufALen = _mm_popcnt_u32(m1) + 8;
				unsigned char shufBLen = _mm_popcnt_u32(m2) + 8;
# else
				unsigned char shufALen = BitsSetTable256[m1] + 8;
				unsigned char shufBLen = BitsSetTable256[m2] + 8;
# endif
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
#else
				unsigned char* sp = p;
				ALIGN_32(uint32_t mmTmp[4]);
				// special characters exist
				_mm_store_si128((__m128i*)mmTmp, data);
				#define DO_THING(n) \
					c = es[i-XMM_SIZE+n], escaped = escapeLUT[c]; \
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
					// TODO: consider revert optimisation from SSSE3 route
					// we overflowed - need to revert and use slower method :(
					col -= (int)(p - sp);
					p = sp;
					break;
				}
				#undef DO_THING_4
				#undef DO_THING
#endif
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

