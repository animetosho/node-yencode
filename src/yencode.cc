
#include <node.h>
#include <node_buffer.h>
#include <node_version.h>
#include <v8.h>
#include <stdlib.h>

using namespace v8;

// MSVC compatibility
#if (defined(_M_IX86_FP) && _M_IX86_FP == 2) || defined(_M_X64)
	#define __SSE2__ 1
	#define __SSSE3__ 1
	//#define __SSE4_1__ 1
	#if defined(_MSC_VER) && _MSC_VER >= 1600
		#define X86_PCLMULQDQ_CRC 1
	#endif
#endif
#ifdef _MSC_VER
#define __BYTE_ORDER__ 1234
#define __ORDER_BIG_ENDIAN__ 4321
#include <intrin.h>
#endif

#if defined(__x86_64__) || defined(__i386__)
#if !defined(X86_PCLMULQDQ_CRC) && defined(__PCLMUL__) && defined(__SSSE3__) && defined(__SSE4_1__)
	#define X86_PCLMULQDQ_CRC 1
#endif
#endif

static unsigned char escapeLUT[256]; // whether or not the character is critical
static uint16_t escapedLUT[256]; // escaped sequences for characters that need escaping
// combine two 8-bit ints into a 16-bit one
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#define UINT16_PACK(a, b) (((a) << 8) | (b))
#define UINT32_PACK(a, b, c, d) (((a) << 24) | ((b) << 16) | ((c) << 8) | (d))
#define UINT32_16_PACK(a, b) (((a) << 16) | (b))
#else
#define UINT16_PACK(a, b) ((a) | ((b) << 8))
#define UINT32_PACK(a, b, c, d) ((a) | ((b) << 8) | ((c) << 16) | ((d) << 24))
#define UINT32_16_PACK(a, b) ((a) | ((b) << 16))
#endif

#ifdef __SSE2__
#include <emmintrin.h>
#define XMM_SIZE 16 /*== (signed int)sizeof(__m128i)*/

#ifdef _MSC_VER
#define ALIGN_32(v) __declspec(align(32)) v
#else
#define ALIGN_32(v) v __attribute__((aligned(32)))
#endif

#ifdef __SSSE3__
#include <tmmintrin.h>
#endif
#ifdef __SSE4_1__
#include <smmintrin.h>
#endif
#ifdef __POPCNT__
#include <nmmintrin.h>
#endif
/*
#ifdef __AVX2__
#include <immintrin.h>
#endif
*/

#if defined(__tune_core2__) || defined(__tune_atom__)
/* on older Intel CPUs, plus first gen Atom, it is faster to store XMM registers in half */
# define STOREU_XMM(dest, xmm) \
  _mm_storel_epi64((__m128i*)(dest), xmm); \
  _mm_storeh_pi(((__m64*)(dest) +1), _mm_castsi128_ps(xmm))
#else
# define STOREU_XMM(dest, xmm) \
  _mm_storeu_si128((__m128i*)(dest), xmm)
#endif

#endif

// runs at around 380MB/s on 2.4GHz Silvermont (worst: 125MB/s, best: 440MB/s)
static size_t do_encode_slow(int line_size, int* colOffset, const unsigned char* src, unsigned char* dest, size_t len) {
	unsigned char *p = dest; // destination pointer
	unsigned long i = 0; // input position
	unsigned char c, escaped; // input character; escaped input character
	int col = *colOffset;
	
	if (col == 0) {
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
		unsigned char* sp = NULL;
		// main line
		#ifdef __SSE2__
		while (len-i-1 > XMM_SIZE && col < line_size-1) {
			__m128i data = _mm_add_epi8(
				_mm_loadu_si128((__m128i *)(src + i)), // probably not worth the effort to align
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
					c = src[i+n], escaped = escapeLUT[c]; \
					if (escaped) \
						*(p+n) = escaped; \
					else { \
						*(uint16_t*)(p+n) = escapedLUT[c]; \
						p++; \
					}
				#define DO_THING_4(n) \
					if(mask & (0xF << n)) { \
						DO_THING(n); \
						DO_THING(n+1); \
						DO_THING(n+2); \
						DO_THING(n+3); \
					} else { \
						*(uint32_t*)(p+n) = mmTmp[n>>2]; \
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
		while (len-i-1 > 8 && line_size-col-1 > 8) {
			// 8 cycle unrolled version
			sp = p;
			#define DO_THING(n) \
				c = src[i+n], escaped = escapeLUT[c]; \
				if (escaped) \
					*(p++) = escaped; \
				else { \
					*(uint16_t*)p = escapedLUT[c]; \
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
			c = src[i++], escaped = escapeLUT[c];
			if (escaped) {
				*(p++) = escaped;
				col++;
			}
			else {
				*(uint16_t*)p = escapedLUT[c];
				p += 2;
				col += 2;
			}
			/* experimental branchless version 
			*p = '=';
			c = (src[i++] + 42) & 0xFF;
			int cond = (c=='\0' || c=='=' || c=='\r' || c=='\n');
			*(p+cond) = c + (cond << 6);
			p += 1+cond;
			col += 1+cond;
			*/
			if (i >= len) goto end;
		}
		
		// last line char
		if(col < line_size) { // this can only be false if the last character was an escape sequence (or line_size is horribly small), in which case, we don't need to handle space/tab cases
			last_char:
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
			// another option may be to just write the EOL and let the first char be handled by the faster methods above, but it appears that writing the extra byte here is generally faster...
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


// slightly faster version which improves the worst case scenario significantly; since worst case doesn't happen often, overall speedup is relatively minor
// requires PSHUFB (SSSE3) instruction, but will use POPCNT (SSE4.2 (or AMD's ABM, but Phenom doesn't support SSSE3 so doesn't matter)) if available (these only seem to give minor speedups, so considered optional)
#ifdef __SSSE3__
size_t (*_do_encode)(int, int*, const unsigned char*, unsigned char*, size_t) = &do_encode_slow;
#define do_encode (*_do_encode)
ALIGN_32(__m128i _shufLUT[258]); // +2 for underflow guard entry
__m128i* shufLUT = _shufLUT+2;
ALIGN_32(__m128i shufMixLUT[256]);
#ifndef __POPCNT__
// table from http://graphics.stanford.edu/~seander/bithacks.html#CountBitsSetTable
static const unsigned char BitsSetTable256[256] = 
{
#   define B2(n) n,     n+1,     n+1,     n+2
#   define B4(n) B2(n), B2(n+1), B2(n+1), B2(n+2)
#   define B6(n) B4(n), B4(n+1), B4(n+1), B4(n+2)
    B6(0), B6(1), B6(1), B6(2)
#undef B2
#undef B4
#undef B6
};
#endif
static size_t do_encode_fast(int line_size, int* colOffset, const unsigned char* src, unsigned char* dest, size_t len) {
	unsigned char *p = dest; // destination pointer
	unsigned long i = 0; // input position
	unsigned char c, escaped; // input character; escaped input character
	int col = *colOffset;
	
	if (col == 0) {
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
		// main line
		while (len-i-1 > XMM_SIZE && col < line_size-1) {
			__m128i data = _mm_add_epi8(
				_mm_loadu_si128((__m128i *)(src + i)), // probably not worth the effort to align
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
				// store out
#ifdef __POPCNT__
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
			c = src[i++], escaped = escapeLUT[c];
			if (escaped) {
				*(p++) = escaped;
				col++;
			}
			else {
				*(uint16_t*)p = escapedLUT[c];
				p += 2;
				col += 2;
			}
			if (i >= len) goto end;
		}
		
		// last line char
		if(col < line_size) { // this can only be false if the last character was an escape sequence (or line_size is horribly small), in which case, we don't need to handle space/tab cases
			last_char_fast:
			c = src[i++];
			if (escapedLUT[c] && c != '.'-42) {
				*(uint16_t*)p = escapedLUT[c];
				p += 2;
			} else {
				*(p++) = c + 42;
			}
		}
		
		after_last_char_fast:
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
	unsigned char *p = dest; // destination pointer
	unsigned long i = 0; // input position
	unsigned char c, escaped; // input character; escaped input character
	int col = *colOffset;
	
	if (col == 0) {
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
		// main line
		while (len-i-1 > YMM_SIZE && col < line_size-1) {
			__m256i data = _mm256_add_epi8(
				_mm256_loadu_si256((__m256i *)(src + i)),
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
			c = src[i++], escaped = escapeLUT[c];
			if (escaped) {
				*(p++) = escaped;
				col++;
			}
			else {
				*(uint16_t*)p = escapedLUT[c];
				p += 2;
				col += 2;
			}
			if (i >= len) goto end;
		}
		
		// last line char
		if(col < line_size) { // this can only be false if the last character was an escape sequence (or line_size is horribly small), in which case, we don't need to handle space/tab cases
			last_char_avx2:
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
static size_t do_encode_fast2(int line_size, int* colOffset, const unsigned char* src, unsigned char* dest, size_t len) {
	// TODO: not ideal; leave here so that tests pass
	if(line_size < 32) return do_encode_fast(line_size, colOffset, src, dest, len);
	
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
#ifdef __POPCNT__
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

#else
#define do_encode do_encode_slow
#endif


/*
// simple naive implementation - most yEnc encoders I've seen do something like the following
// runs at around 145MB/s on 2.4GHz Silvermont (worst: 135MB/s, best: 158MB/s)
static inline unsigned long do_encode(int line_size, int col, const unsigned char* src, unsigned char* dest, size_t len) {
	unsigned char *p = dest;
	
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
	return p - dest;
}
*/


// TODO: need to support max output length somehow
#define do_decode do_decode_scalar

// state var: refers to the previous state - only used for incremental processing
//   0: previous characters are `\r\n` OR there is no previous character
//   1: previous character is `=`
//   2: previous character is `\r`
//   3: previous character is none of the above
static size_t do_decode_scalar_raw(const unsigned char* src, unsigned char* dest, size_t len, char* state) {
	unsigned char *p = dest; // destination pointer
	unsigned long i = 0; // input position
	unsigned char c; // input character
	
	if(len < 1) return 0;
	
	if(state) switch(*state) {
		case 1:
			c = src[i];
			*p++ = c - 42 - 64;
			i++;
			if(c == '\r' && i < len) {
				*state = 2;
				// fall through to case 2
			} else {
				*state = 3;
				break;
			}
		case 2:
			if(src[i] != '\n') break;
			i++;
			*state = 0; // now `\r\n`
			if(len <= i) return 0;
		case 0:
			// skip past first dot
			if(src[i] == '.') i++;
	} else // treat as *state == 0
		if(src[i] == '.') i++;
	
	for(; i + 2 < len; i++) {
		c = src[i];
		switch(c) {
			case '\r':
				// skip past \r\n. sequences
				//i += (*(uint16_t*)(src + i + 1) == UINT16_PACK('\n', '.')) << 1;
				if(*(uint16_t*)(src + i + 1) == UINT16_PACK('\n', '.'))
					i += 2;
			case '\n':
				continue;
			case '=':
				c = src[i+1];
				*p++ = c - 42 - 64;
				i += (c != '\r'); // if we have a \r, reprocess character to deal with \r\n. case
				continue;
			default:
				*p++ = c - 42;
		}
	}
	
	if(state) *state = 3;
	
	if(i+1 < len) { // 2nd last char
		c = src[i];
		switch(c) {
			case '\r':
				if(state && src[i+1] == '\n') {
					*state = 0;
					return p - dest;
				}
			case '\n':
				break;
			case '=':
				c = src[i+1];
				*p++ = c - 42 - 64;
				i += (c != '\r');
				break;
			default:
				*p++ = c - 42;
		}
		i++;
	}
	
	// do final char; we process this separately to prevent an overflow if the final char is '='
	if(i < len) {
		c = src[i];
		if(c != '\n' && c != '\r' && c != '=') {
			*p++ = c - 42;
		} else if(state) {
			if(c == '=') *state = 1;
			else if(c == '\r') *state = 2;
			else *state = 3;
		}
	}
	
	return p - dest;
}
static size_t do_decode_scalar(const unsigned char* src, unsigned char* dest, size_t len, char* state, bool isRaw) {
	if(isRaw) return do_decode_scalar_raw(src, dest, len, state);
	unsigned char *p = dest; // destination pointer
	unsigned long i = 0; // input position
	unsigned char c; // input character
	
	if(len < 1) return 0;
	
	if(state && *state == 1) {
		*p++ = src[i] - 42 - 64;
		i++;
		*state = 3;
	}
	
	/*for(i = 0; i < len - 1; i++) {
		c = src[i];
		if(c == '\n' || c == '\r') continue;
		unsigned char isEquals = (c == '=');
		i += isEquals;
		*p++ = src[i] - (42 + (isEquals << 6));
	}*/
	for(; i+1 < len; i++) {
		c = src[i];
		switch(c) {
			case '\n': case '\r': continue;
			case '=':
				i++;
				c = src[i] - 64;
		}
		*p++ = c - 42;
	}
	if(state) *state = 3;
	// do final char; we process this separately to prevent an overflow if the final char is '='
	if(i < len) {
		c = src[i];
		if(c != '\n' && c != '\r' && c != '=') {
			*p++ = c - 42;
		} else
			if(state) *state = (c == '=' ? 1 : 3);
	}
	
	return p - dest;
}
#ifdef __SSE2__
uint8_t eqFixLUT[256];
ALIGN_32(__m64 eqSubLUT[256]);
#ifdef __SSSE3__
ALIGN_32(__m64 unshufLUT[256]);
ALIGN_32(static const uint8_t _pshufb_combine_table[272]) = {
	0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f,
	0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x08,0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f,0x80,
	0x00,0x01,0x02,0x03,0x04,0x05,0x08,0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f,0x80,0x80,
	0x00,0x01,0x02,0x03,0x04,0x08,0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f,0x80,0x80,0x80,
	0x00,0x01,0x02,0x03,0x08,0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f,0x80,0x80,0x80,0x80,
	0x00,0x01,0x02,0x08,0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f,0x80,0x80,0x80,0x80,0x80,
	0x00,0x01,0x08,0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f,0x80,0x80,0x80,0x80,0x80,0x80,
	0x00,0x08,0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f,0x80,0x80,0x80,0x80,0x80,0x80,0x80,
	0x08,0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,
};
static const __m128i* pshufb_combine_table = (const __m128i*)_pshufb_combine_table;
#endif
static size_t do_decode_sse(const unsigned char* src, unsigned char* dest, size_t len, char* state, bool isRaw) {
	if(len <= sizeof(__m128i)*2) return do_decode_scalar(src, dest, len, state, isRaw);
	
	unsigned char *p = dest; // destination pointer
	unsigned long i = 0; // input position
	unsigned char escFirst = 0; // input character; first char needs escaping
	unsigned int nextMask = 0;
	char tState = 0;
	char* pState = state ? state : &tState;
	if((uintptr_t)src & ((sizeof(__m128i)-1))) {
		// find source memory alignment
		unsigned char* aSrc = (unsigned char*)(((uintptr_t)src + (sizeof(__m128i)-1)) & ~(sizeof(__m128i)-1));
		
		i = aSrc - src;
		p += do_decode_scalar(src, dest, i, pState, isRaw);
	}
	
	// handle finicky case of \r\n. straddled across initial boundary
	if(*pState == 0 && i+1 < len && src[i] == '.')
		nextMask = 1;
	else if(*pState == 2 && i+2 < len && *(uint16_t*)(src + i) == UINT16_PACK('\n','.'))
		nextMask = 2;
	escFirst = *pState == 1;
	
	// our algorithm may perform an aligned load on the next part, of which we consider 2 bytes (for \r\n. sequence checking)
	for(; i + (sizeof(__m128i)+1) < len; i += sizeof(__m128i)) {
		__m128i data = _mm_load_si128((__m128i *)(src + i));
		
		// search for special chars
		__m128i cmpEq = _mm_cmpeq_epi8(data, _mm_set1_epi8('=')),
		cmp = _mm_or_si128(
			_mm_or_si128(
				_mm_cmpeq_epi8(data, _mm_set1_epi8('\r')),
				_mm_cmpeq_epi8(data, _mm_set1_epi8('\n'))
			),
			cmpEq
		);
		unsigned int mask = _mm_movemask_epi8(cmp); // not the most accurate mask if we have invalid sequences; we fix this up later
		
		__m128i oData;
		if(escFirst) { // TODO: should be possible to eliminate branch by storing vectors adjacently
			// first byte needs escaping due to preceeding = in last loop iteration
			oData = _mm_sub_epi8(data, _mm_set_epi8(42,42,42,42,42,42,42,42,42,42,42,42,42,42,42,42+64));
		} else {
			oData = _mm_sub_epi8(data, _mm_set1_epi8(42));
		}
		mask &= ~escFirst;
		mask |= nextMask;
		
		if (mask != 0) {
			// a spec compliant encoder should never generate sequences: ==, =\n and =\r, but we'll handle them to be spec compliant
			// the yEnc specification requires any character following = to be unescaped, not skipped over, so we'll deal with that
			
#define LOAD_HALVES(a, b) _mm_castps_si128(_mm_loadh_pi( \
	_mm_castsi128_ps(_mm_loadl_epi64((__m128i*)(a))), \
	(b) \
))
			// firstly, resolve invalid sequences of = to deal with cases like '===='
			unsigned int maskEq = _mm_movemask_epi8(cmpEq);
			unsigned int tmp = eqFixLUT[(maskEq&0xff) & ~escFirst];
			maskEq = (eqFixLUT[(maskEq>>8) & ~(tmp>>7)] << 8) | tmp;
			
			// next, eliminate anything following a `=` from the special char mask; this eliminates cases of `=\r` so that they aren't removed
			mask &= ~(maskEq << 1);
			
			// unescape chars following `=`
			oData = _mm_sub_epi8(
				oData,
				_mm_slli_si128(LOAD_HALVES(
					eqSubLUT + (maskEq&0xff),
					eqSubLUT + (maskEq>>8)
				), 1)
			);
			
			// handle \r\n. sequences
			// RFC3977 requires the first dot on a line to be stripped, due to dot-stuffing
			if(isRaw) {
#ifdef __SSSE3__
# define ALIGNR _mm_alignr_epi8
#else
# define ALIGNR(a, b, i) _mm_or_si128(_mm_slli_si128(a, sizeof(__m128i)-(i)), _mm_srli_si128(b, i))
#endif
				__m128i nextData = _mm_load_si128((__m128i *)(src + i) + 1);
				// find instances of \r\n
				__m128i tmpData = ALIGNR(nextData, data, 1);
				__m128i cmp1 = _mm_cmpeq_epi16(data, _mm_set1_epi16(0x0a0d));
				__m128i cmp2 = _mm_cmpeq_epi16(tmpData, _mm_set1_epi16(0x0a0d));
				// trim matches to just the \n
				cmp1 = _mm_and_si128(cmp1, _mm_set1_epi16(0xff00));
				cmp2 = _mm_and_si128(cmp2, _mm_set1_epi16(0xff00));
				// merge the two comparisons
				cmp1 = _mm_or_si128(_mm_srli_si128(cmp1, 1), cmp2);
				// then check if there's a . after any of these instances
				tmpData = ALIGNR(nextData, data, 2);
				tmpData = _mm_cmpeq_epi8(tmpData, _mm_set1_epi8('.'));
				// grab bit-mask of matched . characters and OR with mask
				unsigned int killDots = _mm_movemask_epi8(_mm_and_si128(tmpData, cmp1));
				mask |= (killDots << 2) & 0xffff;
				nextMask = killDots >> (sizeof(__m128i)-2);
#undef ALIGNR
			}
			
			escFirst = (maskEq >> (sizeof(__m128i)-1));
			
			// all that's left is to 'compress' the data (skip over masked chars)
#ifdef __SSSE3__
# ifdef __POPCNT__
			unsigned char skipped = _mm_popcnt_u32(mask & 0xff);
# else
			unsigned char skipped = BitsSetTable256[mask & 0xff];
# endif
			// lookup compress masks and shuffle
			// load up two halves
			__m128i shuf = LOAD_HALVES(unshufLUT + (mask&0xff), unshufLUT + (mask>>8));
			
			// offset upper half by 8
			shuf = _mm_add_epi8(shuf, _mm_set_epi32(0x08080808, 0x08080808, 0, 0));
			// shift down upper half into lower
			shuf = _mm_shuffle_epi8(shuf, _mm_load_si128(pshufb_combine_table + skipped));
			
			// shuffle data
			oData = _mm_shuffle_epi8(oData, shuf);
			STOREU_XMM(p, oData);
			
			// increment output position
# ifdef __POPCNT__
			p += XMM_SIZE - _mm_popcnt_u32(mask);
# else
			p += XMM_SIZE - skipped - BitsSetTable256[mask >> 8];
# endif
			
#else
			ALIGN_32(uint32_t mmTmp[4]);
			_mm_store_si128((__m128i*)mmTmp, oData);
			
			for(int j=0; j<4; j++) {
				if(mask & 0xf) {
					unsigned char* pMmTmp = (unsigned char*)(mmTmp + j);
					unsigned int maskn = ~mask;
					*p = pMmTmp[0];
					p += (maskn & 1);
					*p = pMmTmp[1];
					p += (maskn & 2) >> 1;
					*p = pMmTmp[2];
					p += (maskn & 4) >> 2;
					*p = pMmTmp[3];
					p += (maskn & 8) >> 3;
				} else {
					*(uint32_t*)p = mmTmp[j];
					p += 4;
				}
				mask >>= 4;
			}
#endif
#undef LOAD_HALVES
		} else {
			STOREU_XMM(p, oData);
			p += XMM_SIZE;
			escFirst = 0;
			nextMask = 0;
		}
	}
	
	if(escFirst) *pState = 1; // escape next character
	else if(nextMask == 1) *pState = 0; // next character is '.', where previous two were \r\n
	else if(nextMask == 2) *pState = 2; // next characters are '\n.', previous is \r
	else *pState = 3;
	
	// end alignment
	if(i < len) {
		p += do_decode_scalar(src + i, p, len - i, pState, isRaw);
	}
	
	return p - dest;
}
#endif


union crc32 {
	uint32_t u32;
	unsigned char u8a[4];
};

#define PACK_4(arr) (((uint_fast32_t)arr[0] << 24) | ((uint_fast32_t)arr[1] << 16) | ((uint_fast32_t)arr[2] << 8) | (uint_fast32_t)arr[3])
#define UNPACK_4(arr, val) { \
	arr[0] = (unsigned char)(val >> 24) & 0xFF; \
	arr[1] = (unsigned char)(val >> 16) & 0xFF; \
	arr[2] = (unsigned char)(val >>  8) & 0xFF; \
	arr[3] = (unsigned char)val & 0xFF; \
}

#include "interface.h"
crcutil_interface::CRC* crc = NULL;

#ifdef X86_PCLMULQDQ_CRC
bool x86_cpu_has_pclmulqdq = false;
#include "crc_folding.c"
#else
#define x86_cpu_has_pclmulqdq false
#define crc_fold(a, b) 0
#endif

static inline void do_crc32(const void* data, size_t length, unsigned char out[4]) {
	// if we have the pclmulqdq instruction, use the insanely fast folding method
	if(x86_cpu_has_pclmulqdq) {
		uint32_t tmp = crc_fold((const unsigned char*)data, (long)length);
		UNPACK_4(out, tmp);
	} else {
		if(!crc) {
			crc = crcutil_interface::CRC::Create(
				0xEDB88320, 0, 32, true, 0, 0, 0, 0, NULL);
			// instance never deleted... oh well...
		}
		crcutil_interface::UINT64 tmp = 0;
		crc->Compute(data, length, &tmp);
		UNPACK_4(out, tmp);
	}
}

crcutil_interface::CRC* crcI = NULL;
static inline void do_crc32_incremental(const void* data, size_t length, unsigned char init[4]) {
	if(!crcI) {
		crcI = crcutil_interface::CRC::Create(
			0xEDB88320, 0, 32, false, 0, 0, 0, 0, NULL);
		// instance never deleted... oh well...
	}
	
	if(x86_cpu_has_pclmulqdq) {
		// TODO: think of a nicer way to do this than a combine
		crcutil_interface::UINT64 crc1_ = PACK_4(init);
		crcutil_interface::UINT64 crc2_ = crc_fold((const unsigned char*)data, (long)length);
		crcI->Concatenate(crc2_, 0, length, &crc1_);
		UNPACK_4(init, crc1_);
	} else {
		crcutil_interface::UINT64 tmp = PACK_4(init) ^ 0xffffffff;
		crcI->Compute(data, length, &tmp);
		tmp ^= 0xffffffff;
		UNPACK_4(init, tmp);
	}
}

static inline void do_crc32_combine(unsigned char crc1[4], const unsigned char crc2[4], size_t len2) {
	if(!crc) {
		crc = crcutil_interface::CRC::Create(
			0xEDB88320, 0, 32, true, 0, 0, 0, 0, NULL);
		// instance never deleted... oh well...
	}
	crcutil_interface::UINT64 crc1_ = PACK_4(crc1), crc2_ = PACK_4(crc2);
	crc->Concatenate(crc2_, 0, len2, &crc1_);
	UNPACK_4(crc1, crc1_);
}

static inline void do_crc32_zeros(unsigned char crc1[4], size_t len) {
	if(!crc) {
		crc = crcutil_interface::CRC::Create(
			0xEDB88320, 0, 32, true, 0, 0, 0, 0, NULL);
		// instance never deleted... oh well...
	}
	crcutil_interface::UINT64 crc_ = 0;
	crc->CrcOfZeroes(len, &crc_);
	UNPACK_4(crc1, crc_);
}

void free_buffer(char* data, void* _size) {
#if !NODE_VERSION_AT_LEAST(0, 11, 0)
	int size = (int)(size_t)_size;
	V8::AdjustAmountOfExternalAllocatedMemory(-size);
#endif
	//Isolate::GetCurrent()->AdjustAmountOfExternalAllocatedMemory(-size);
	free(data);
}

// TODO: encode should return col num for incremental processing
//       line limit + return input consumed
//       async processing?

#define YENC_MAX_SIZE(len, line_size) ( \
		  len * 2    /* all characters escaped */ \
		+ ((len*4) / line_size) /* newlines, considering the possibility of all chars escaped */ \
		+ 2 /* allocation for offset and that a newline may occur early */ \
		+ 32 /* allocation for XMM overflowing */ \
	)


// encode(str, line_size, col)
// crc32(str, init)
#if NODE_VERSION_AT_LEAST(0, 11, 0)

#if NODE_VERSION_AT_LEAST(3, 0, 0) // iojs3
#define BUFFER_NEW(...) node::Buffer::New(isolate, __VA_ARGS__).ToLocalChecked()
#else
#define BUFFER_NEW(...) node::Buffer::New(isolate, __VA_ARGS__)
#endif

// node 0.12 version
static void Encode(const FunctionCallbackInfo<Value>& args) {
	Isolate* isolate = Isolate::GetCurrent();
	HandleScope scope(isolate);
	
	if (args.Length() == 0 || !node::Buffer::HasInstance(args[0])) {
		isolate->ThrowException(Exception::Error(
			String::NewFromUtf8(isolate, "You must supply a Buffer"))
		);
		return;
	}
	
	size_t arg_len = node::Buffer::Length(args[0]);
	if (arg_len == 0) {
		args.GetReturnValue().Set( BUFFER_NEW(0) );
		return;
	}
	
	int line_size = 128, col = 0;
	if (args.Length() >= 2) {
		// TODO: probably should throw errors instead of transparently fixing these...
		line_size = args[1]->ToInteger()->Value();
		if (line_size < 1) line_size = 128;
		if (args.Length() >= 3) {
			col = args[2]->ToInteger()->Value();
			if (col >= line_size) col = 0;
		}
	}
	
	// allocate enough memory to handle worst case requirements
	size_t dest_len = YENC_MAX_SIZE(arg_len, line_size);
	
	unsigned char *result = (unsigned char*) malloc(dest_len);
	size_t len = do_encode(line_size, &col, (const unsigned char*)node::Buffer::Data(args[0]), result, arg_len);
	result = (unsigned char*)realloc(result, len);
	//isolate->AdjustAmountOfExternalAllocatedMemory(len);
	args.GetReturnValue().Set( BUFFER_NEW((char*)result, len, free_buffer, (void*)len) );
}

static void EncodeTo(const FunctionCallbackInfo<Value>& args) {
	Isolate* isolate = Isolate::GetCurrent();
	HandleScope scope(isolate);
	
	if (args.Length() < 2 || !node::Buffer::HasInstance(args[0]) || !node::Buffer::HasInstance(args[1])) {
		isolate->ThrowException(Exception::Error(
			String::NewFromUtf8(isolate, "You must supply two Buffers"))
		);
		return;
	}
	
	size_t arg_len = node::Buffer::Length(args[0]);
	if (arg_len == 0) {
		args.GetReturnValue().Set( Integer::New(isolate, 0) );
		return;
	}
	
	int line_size = 128, col = 0;
	if (args.Length() >= 3) {
		// TODO: probably should throw errors instead of transparently fixing these...
		line_size = args[2]->ToInteger()->Value();
		if (line_size < 1) line_size = 128;
		if (args.Length() >= 4) {
			col = args[3]->ToInteger()->Value();
			if (col >= line_size) col = 0;
		}
	}
	
	// check that destination buffer has enough space
	size_t dest_len = YENC_MAX_SIZE(arg_len, line_size);
	if(node::Buffer::Length(args[1]) < dest_len) {
		args.GetReturnValue().Set( Integer::New(isolate, 0) );
		return;
	}
	
	size_t len = do_encode(line_size, &col, (const unsigned char*)node::Buffer::Data(args[0]), (unsigned char*)node::Buffer::Data(args[1]), arg_len);
	args.GetReturnValue().Set( Integer::New(isolate, len) );
}

static void Decode(const FunctionCallbackInfo<Value>& args) {
	Isolate* isolate = Isolate::GetCurrent();
	HandleScope scope(isolate);
	
	if (args.Length() == 0 || !node::Buffer::HasInstance(args[0])) {
		isolate->ThrowException(Exception::Error(
			String::NewFromUtf8(isolate, "You must supply a Buffer"))
		);
		return;
	}
	
	size_t arg_len = node::Buffer::Length(args[0]);
	if (arg_len == 0) {
		args.GetReturnValue().Set( BUFFER_NEW(0) );
		return;
	}
	
	unsigned char *result = (unsigned char*) malloc(arg_len);
	size_t len = do_decode((const unsigned char*)node::Buffer::Data(args[0]), result, arg_len, NULL, true);
	result = (unsigned char*)realloc(result, len);
	//isolate->AdjustAmountOfExternalAllocatedMemory(len);
	args.GetReturnValue().Set( BUFFER_NEW((char*)result, len, free_buffer, (void*)len) );
}

static void DecodeTo(const FunctionCallbackInfo<Value>& args) {
	Isolate* isolate = Isolate::GetCurrent();
	HandleScope scope(isolate);
	
	if (args.Length() < 2 || !node::Buffer::HasInstance(args[0]) || !node::Buffer::HasInstance(args[1])) {
		isolate->ThrowException(Exception::Error(
			String::NewFromUtf8(isolate, "You must supply two Buffers"))
		);
		return;
	}
	
	size_t arg_len = node::Buffer::Length(args[0]);
	if (arg_len == 0) {
		args.GetReturnValue().Set( Integer::New(isolate, 0) );
		return;
	}
	
	// check that destination buffer has enough space
	if(node::Buffer::Length(args[1]) < arg_len) {
		args.GetReturnValue().Set( Integer::New(isolate, 0) );
		return;
	}
	
	size_t len = do_decode((const unsigned char*)node::Buffer::Data(args[0]), (unsigned char*)node::Buffer::Data(args[1]), arg_len, NULL, true);
	args.GetReturnValue().Set( Integer::New(isolate, len) );
}

#if NODE_VERSION_AT_LEAST(3, 0, 0)
// for whatever reason, iojs 3 gives buffer corruption if you pass in a pointer without a free function
#define RETURN_CRC(x) do { \
	Local<Object> buff = BUFFER_NEW(4); \
	*(uint32_t*)node::Buffer::Data(buff) = x.u32; \
	args.GetReturnValue().Set( buff ); \
} while(0)
#else
#define RETURN_CRC(x) args.GetReturnValue().Set( BUFFER_NEW((char*)x.u8a, 4) )
#endif

static void CRC32(const FunctionCallbackInfo<Value>& args) {
	Isolate* isolate = Isolate::GetCurrent();
	HandleScope scope(isolate);
	
	if (args.Length() == 0 || !node::Buffer::HasInstance(args[0])) {
		isolate->ThrowException(Exception::Error(
			String::NewFromUtf8(isolate, "You must supply a Buffer"))
		);
		return;
	}
	// TODO: support string args??
	
	union crc32 init;
	init.u32 = 0;
	if (args.Length() >= 2) {
		if (!node::Buffer::HasInstance(args[1]) || node::Buffer::Length(args[1]) != 4) {
			isolate->ThrowException(Exception::Error(
				String::NewFromUtf8(isolate, "Second argument must be a 4 byte buffer"))
			);
			return;
		}
		init.u32 = *(uint32_t*)node::Buffer::Data(args[1]);
		do_crc32_incremental(
			(const void*)node::Buffer::Data(args[0]),
			node::Buffer::Length(args[0]),
			init.u8a
		);
	} else {
		do_crc32(
			(const void*)node::Buffer::Data(args[0]),
			node::Buffer::Length(args[0]),
			init.u8a
		);
	}
	RETURN_CRC(init);
}

static void CRC32Combine(const FunctionCallbackInfo<Value>& args) {
	Isolate* isolate = Isolate::GetCurrent();
	HandleScope scope(isolate);
	
	if (args.Length() < 3) {
		isolate->ThrowException(Exception::Error(
			String::NewFromUtf8(isolate, "At least 3 arguments required"))
		);
		return;
	}
	if (!node::Buffer::HasInstance(args[0]) || node::Buffer::Length(args[0]) != 4
	|| !node::Buffer::HasInstance(args[1]) || node::Buffer::Length(args[1]) != 4) {
		isolate->ThrowException(Exception::Error(
			String::NewFromUtf8(isolate, "You must supply a 4 byte Buffer for the first two arguments"))
		);
		return;
	}
	
	union crc32 crc1, crc2;
	size_t len = (size_t)args[2]->ToInteger()->Value();
	
	crc1.u32 = *(uint32_t*)node::Buffer::Data(args[0]);
	crc2.u32 = *(uint32_t*)node::Buffer::Data(args[1]);
	
	do_crc32_combine(crc1.u8a, crc2.u8a, len);
	RETURN_CRC(crc1);
}

static void CRC32Zeroes(const FunctionCallbackInfo<Value>& args) {
	Isolate* isolate = Isolate::GetCurrent();
	HandleScope scope(isolate);
	
	if (args.Length() < 1) {
		isolate->ThrowException(Exception::Error(
			String::NewFromUtf8(isolate, "At least 1 argument required"))
		);
		return;
	}
	
	union crc32 crc1;
	size_t len = (size_t)args[0]->ToInteger()->Value();
	do_crc32_zeros(crc1.u8a, len);
	RETURN_CRC(crc1);
}
#else
// node 0.10 version
#define ReturnBuffer(buffer, size, offset) return scope.Close(Local<Object>::New((buffer)->handle_))

static Handle<Value> Encode(const Arguments& args) {
	HandleScope scope;
	
	if (args.Length() == 0 || !node::Buffer::HasInstance(args[0])) {
		return ThrowException(Exception::Error(
			String::New("You must supply a Buffer"))
		);
	}
	
	size_t arg_len = node::Buffer::Length(args[0]);
	if (arg_len == 0) {
		ReturnBuffer(node::Buffer::New(0), 0, 0);
	}
	
	int line_size = 128, col = 0;
	if (args.Length() >= 2) {
		// TODO: probably should throw errors instead of transparently fixing these...
		line_size = args[1]->ToInteger()->Value();
		if (line_size < 1) line_size = 128;
		if (args.Length() >= 3) {
			col = args[2]->ToInteger()->Value();
			if (col >= line_size) col = 0;
		}
	}
	
	// allocate enough memory to handle worst case requirements
	size_t dest_len = YENC_MAX_SIZE(arg_len, line_size);
	
	unsigned char *result = (unsigned char*) malloc(dest_len);
	size_t len = do_encode(line_size, &col, (const unsigned char*)node::Buffer::Data(args[0]), result, arg_len);
	result = (unsigned char*)realloc(result, len);
	V8::AdjustAmountOfExternalAllocatedMemory(len);
	ReturnBuffer(node::Buffer::New((char*)result, len, free_buffer, (void*)len), len, 0);
}

static Handle<Value> EncodeTo(const Arguments& args) {
	HandleScope scope;
	
	if (args.Length() < 2 || !node::Buffer::HasInstance(args[0]) || !node::Buffer::HasInstance(args[1])) {
		return ThrowException(Exception::Error(
			String::New("You must supply two Buffers"))
		);
	}
	
	size_t arg_len = node::Buffer::Length(args[0]);
	if (arg_len == 0) {
		return scope.Close(Integer::New(0));
	}
	
	int line_size = 128, col = 0;
	if (args.Length() >= 3) {
		// TODO: probably should throw errors instead of transparently fixing these...
		line_size = args[2]->ToInteger()->Value();
		if (line_size < 1) line_size = 128;
		if (args.Length() >= 4) {
			col = args[3]->ToInteger()->Value();
			if (col >= line_size) col = 0;
		}
	}
	
	// check that destination buffer has enough space
	size_t dest_len = YENC_MAX_SIZE(arg_len, line_size);
	if(node::Buffer::Length(args[1]) < dest_len) {
		return scope.Close(Integer::New(0));
	}
	
	size_t len = do_encode(line_size, &col, (const unsigned char*)node::Buffer::Data(args[0]), (unsigned char*)node::Buffer::Data(args[1]), arg_len);
	return scope.Close(Integer::New(len));
}

static Handle<Value> Decode(const Arguments& args) {
	HandleScope scope;
	
	if (args.Length() == 0 || !node::Buffer::HasInstance(args[0])) {
		return ThrowException(Exception::Error(
			String::New("You must supply a Buffer"))
		);
	}
	
	size_t arg_len = node::Buffer::Length(args[0]);
	if (arg_len == 0) {
		ReturnBuffer(node::Buffer::New(0), 0, 0);
	}
	
	unsigned char *result = (unsigned char*) malloc(arg_len);
	size_t len = do_decode((const unsigned char*)node::Buffer::Data(args[0]), result, arg_len, NULL, true);
	result = (unsigned char*)realloc(result, len);
	V8::AdjustAmountOfExternalAllocatedMemory(len);
	ReturnBuffer(node::Buffer::New((char*)result, len, free_buffer, (void*)len), len, 0);
}

static Handle<Value> DecodeTo(const Arguments& args) {
	HandleScope scope;
	
	if (args.Length() < 2 || !node::Buffer::HasInstance(args[0]) || !node::Buffer::HasInstance(args[1])) {
		return ThrowException(Exception::Error(
			String::New("You must supply two Buffers"))
		);
	}
	
	size_t arg_len = node::Buffer::Length(args[0]);
	if (arg_len == 0) {
		return scope.Close(Integer::New(0));
	}
	
	// check that destination buffer has enough space
	if(node::Buffer::Length(args[1]) < arg_len) {
		return scope.Close(Integer::New(0));
	}
	
	size_t len = do_decode((const unsigned char*)node::Buffer::Data(args[0]), (unsigned char*)node::Buffer::Data(args[1]), arg_len, NULL, true);
	return scope.Close(Integer::New(len));
}

static Handle<Value> CRC32(const Arguments& args) {
	HandleScope scope;
	
	if (args.Length() == 0 || !node::Buffer::HasInstance(args[0])) {
		return ThrowException(Exception::Error(
			String::New("You must supply a Buffer"))
		);
	}
	// TODO: support string args??
	
	union crc32 init;
	init.u32 = 0;
	if (args.Length() >= 2) {
		if (!node::Buffer::HasInstance(args[1]) || node::Buffer::Length(args[1]) != 4)
			return ThrowException(Exception::Error(
				String::New("Second argument must be a 4 byte buffer"))
			);
		
		init.u32 = *(uint32_t*)node::Buffer::Data(args[1]);
		do_crc32_incremental(
			(const void*)node::Buffer::Data(args[0]),
			node::Buffer::Length(args[0]),
			init.u8a
		);
	} else {
		do_crc32(
			(const void*)node::Buffer::Data(args[0]),
			node::Buffer::Length(args[0]),
			init.u8a
		);
	}
	ReturnBuffer(node::Buffer::New((char*)init.u8a, 4), 4, 0);
}

static Handle<Value> CRC32Combine(const Arguments& args) {
	HandleScope scope;
	
	if (args.Length() < 3) {
		return ThrowException(Exception::Error(
			String::New("At least 3 arguments required"))
		);
	}
	if (!node::Buffer::HasInstance(args[0]) || node::Buffer::Length(args[0]) != 4
	|| !node::Buffer::HasInstance(args[1]) || node::Buffer::Length(args[1]) != 4) {
		return ThrowException(Exception::Error(
			String::New("You must supply a 4 byte Buffer for the first two arguments"))
		);
	}
	
	union crc32 crc1, crc2;
	size_t len = (size_t)args[2]->ToInteger()->Value();
	
	crc1.u32 = *(uint32_t*)node::Buffer::Data(args[0]);
	crc2.u32 = *(uint32_t*)node::Buffer::Data(args[1]);
	
	do_crc32_combine(crc1.u8a, crc2.u8a, len);
	ReturnBuffer(node::Buffer::New((char*)crc1.u8a, 4), 4, 0);
}

static Handle<Value> CRC32Zeroes(const Arguments& args) {
	HandleScope scope;
	
	if (args.Length() < 1) {
		return ThrowException(Exception::Error(
			String::New("At least 1 argument required"))
		);
	}
	union crc32 crc1;
	size_t len = (size_t)args[0]->ToInteger()->Value();
	
	do_crc32_zeros(crc1.u8a, len);
	ReturnBuffer(node::Buffer::New((char*)crc1.u8a, 4), 4, 0);
}
#endif

void init(Handle<Object> target) {
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
	NODE_SET_METHOD(target, "encode", Encode);
	NODE_SET_METHOD(target, "encodeTo", EncodeTo);
	NODE_SET_METHOD(target, "decode", Decode);
	NODE_SET_METHOD(target, "decodeTo", DecodeTo);
	NODE_SET_METHOD(target, "crc32", CRC32);
	NODE_SET_METHOD(target, "crc32_combine", CRC32Combine);
	NODE_SET_METHOD(target, "crc32_zeroes", CRC32Zeroes);
	
	
	
#ifdef __SSSE3__
	uint32_t flags;
#ifdef _MSC_VER
	int cpuInfo[4];
	__cpuid(cpuInfo, 1);
	flags = cpuInfo[2];
#else
	// conveniently stolen from zlib-ng
	__asm__ __volatile__ (
		"cpuid"
	: "=c" (flags)
	: "a" (1)
	: "%edx", "%ebx"
	);
#endif
#ifdef X86_PCLMULQDQ_CRC
	x86_cpu_has_pclmulqdq = (flags & 0x80202) == 0x80202; // SSE4.1 + SSSE3 + CLMUL
#endif
	
	uint32_t fastYencMask = 0x200;
#ifdef __POPCNT__
	fastYencMask |= 0x800000;
#endif
	_do_encode = ((flags & fastYencMask) == fastYencMask) ? &do_encode_fast : &do_encode_slow; // SSSE3 + required stuff based on compiler flags
	
	if((flags & fastYencMask) == fastYencMask) {
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
		
		
		// generate unshuf LUT
		for(int i=0; i<256; i++) {
			int k = i;
			uint8_t res[8];
			int p = 0;
			for(int j=0; j<8; j++) {
				if(!(k & 1)) {
					res[p++] = j;
				}
				k >>= 1;
			}
			for(; p<8; p++)
				res[p] = 0;
			_mm_storel_epi64((__m128i*)(unshufLUT + i), _mm_loadl_epi64((__m128i*)res));
		}
	}
#endif
#ifdef __SSE2__
	// generate unshuf LUT
	for(int i=0; i<256; i++) {
		int k = i;
		uint8_t res[8];
		int p = 0;
		
		// fix LUT
		k = i;
		p = 0;
		for(int j=0; j<8; j++) {
			k = i >> j;
			if(k & 1) {
				p |= 1 << j;
				j++;
			}
		}
		eqFixLUT[i] = p;
		
		// sub LUT
		k = i;
		for(int j=0; j<8; j++) {
			res[j] = (k & 1) << 6;
			k >>= 1;
		}
		_mm_storel_epi64((__m128i*)(eqSubLUT + i), _mm_loadl_epi64((__m128i*)res));
	}
#endif
}

NODE_MODULE(yencode, init);
