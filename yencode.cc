
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
#else
#define UINT16_PACK(a, b) ((a) | ((b) << 8))
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
#ifdef __AVX2__
#include <immintrin.h>
#endif
#endif

// runs at around 380MB/s on 2.4GHz Silvermont (worst: 125MB/s, best: 440MB/s)
static size_t do_encode_slow(int line_size, int col, const unsigned char* src, unsigned char* dest, size_t len) {
	unsigned char *p = dest; // destination pointer
	unsigned long i = 0; // input position
	unsigned char c, escaped; // input character; escaped input character
	
	if (col > 0) goto skip_first_char;
	while(1) {
		// first char in line
		c = src[i++];
		if (escapedLUT[c]) {
			*(uint16_t*)p = escapedLUT[c];
			p += 2;
			col = 2;
		} else {
			*(p++) = c + 42;
			col = 1;
		}
		if (i >= len) break;
		
		skip_first_char:
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
				_mm_storeu_si128((__m128i*)p, data);
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
		*(uint16_t*)p = UINT16_PACK('\r', '\n');
		p += 2;
	}
	
	end:
	return p - dest;
}


// slightly faster version which improves the worst case scenario significantly; since worst case doesn't happen often, overall speedup is relatively minor
// requires PSHUFB (SSSE3) instruction, but will use PBLENDV (SSE4.1) and POPCNT (SSE4.2 (or AMD's ABM, but Phenom doesn't support SSSE3 so doesn't matter)) if available (these only seem to give minor speedups, so considered optional)
#ifdef __SSSE3__
size_t (*_do_encode)(int, int, const unsigned char*, unsigned char*, size_t) = &do_encode_slow;
#define do_encode (*_do_encode)
ALIGN_32(__m128i shufLUT[256]);
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
static size_t do_encode_fast(int line_size, int col, const unsigned char* src, unsigned char* dest, size_t len) {
	unsigned char *p = dest; // destination pointer
	unsigned long i = 0; // input position
	unsigned char c, escaped; // input character; escaped input character
	
	__m128i equals = _mm_set1_epi8('=');
	
	if (col > 0) goto skip_first_char_fast;
	while(1) {
		// first char in line
		c = src[i++];
		if (escapedLUT[c]) {
			*(uint16_t*)p = escapedLUT[c];
			p += 2;
			col = 2;
		} else {
			*(p++) = c + 42;
			col = 1;
		}
		if (i >= len) break;
		
		skip_first_char_fast:
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
					_mm_cmpeq_epi8(data, equals)
				)
			);
			
			unsigned int mask = _mm_movemask_epi8(cmp);
			if (mask != 0) {
				// TODO: consider doing only 1 set of shuffles?
				uint8_t m1 = mask & 0xFF;
				uint8_t m2 = mask >> 8;
				
				// perform lookup for shuffle mask
				__m128i shufMA = _mm_load_si128(shufLUT + m1);
				__m128i shufMB = _mm_load_si128(shufLUT + m2);
				
				// second mask processes on second half, so add to the offsets
				shufMB = _mm_add_epi8(shufMB, _mm_set1_epi8(8));
				
				// expand halves
				//shuf = _mm_or_si128(_mm_cmpgt_epi8(shuf, _mm_set1_epi8(15)), shuf);
				__m128i data2 = _mm_shuffle_epi8(data, shufMB);
				data = _mm_shuffle_epi8(data, shufMA);
				
				// get the maskEsc for the escaped chars
				__m128i maskEscA = _mm_cmpeq_epi8(shufMA, _mm_srli_si128(shufMA, 1));
				__m128i maskEscB = _mm_cmpeq_epi8(shufMB, _mm_srli_si128(shufMB, 1));
				
				// blend escape chars in
				__m128i tmp1 = _mm_add_epi8(data, _mm_set1_epi8(64));
				__m128i tmp2 = _mm_add_epi8(data2, _mm_set1_epi8(64));
#ifdef __SSE4_1__
	#define BLENDV _mm_blendv_epi8
#else
	#define BLENDV(v1, v2, m) _mm_or_si128(_mm_andnot_si128(m, v1), _mm_and_si128(m, v2))
#endif
				data = BLENDV(data, equals, maskEscA);
				data2 = BLENDV(data2, equals, maskEscB);
				maskEscA = _mm_slli_si128(maskEscA, 1);
				maskEscB = _mm_slli_si128(maskEscB, 1);
				data = BLENDV(data, tmp1, maskEscA);
				data2 = BLENDV(data2, tmp2, maskEscB);
#undef BLENDV
				// store out
#ifdef __POPCNT__
				unsigned char shufALen = _mm_popcnt_u32(m1) + 8;
				unsigned char shufBLen = _mm_popcnt_u32(m2) + 8;
#else
				unsigned char shufALen = BitsSetTable256[m1] + 8;
				unsigned char shufBLen = BitsSetTable256[m2] + 8;
#endif
				_mm_storeu_si128((__m128i*)p, data);
				p += shufALen;
				_mm_storeu_si128((__m128i*)p, data2);
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
						isEsc = ((tst>>8) == (tst&0xFF));
						p += isEsc;
						i -= 8 - (tst>>8) - isEsc;
						//col = line_size-1 + isEsc; // doesn't need to be set, since it's never read again
						if(isEsc)
							goto after_last_char_fast;
						else
							goto last_char_fast;
					}
				}
			} else {
				_mm_storeu_si128((__m128i*)p, data);
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
		*(uint16_t*)p = UINT16_PACK('\r', '\n');
		p += 2;
	}
	
	end:
	return p - dest;
}

// experimental AVX2 version; seems to be slower than SSSE3 variant, so not used at the moment
#ifdef __AVX2__
#define YMM_SIZE 32
static size_t do_encode_avx2(int line_size, int col, const unsigned char* src, unsigned char* dest, size_t len) {
	unsigned char *p = dest; // destination pointer
	unsigned long i = 0; // input position
	unsigned char c, escaped; // input character; escaped input character
	
	__m256i equals = _mm256_set1_epi8('=');
	
	if (col > 0) goto skip_first_char_avx2;
	while(1) {
		// first char in line
		c = src[i++];
		if (escapedLUT[c]) {
			*(uint16_t*)p = escapedLUT[c];
			p += 2;
			col = 2;
		} else {
			*(p++) = c + 42;
			col = 1;
		}
		if (i >= len) break;
		
		skip_first_char_avx2:
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
					_mm256_cmpeq_epi8(data, equals)
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
				
				// get the maskEsc for the escaped chars
				__m256i maskEscA = _mm256_cmpeq_epi8(shufMA, _mm256_srli_si256(shufMA, 1));
				__m256i maskEscB = _mm256_cmpeq_epi8(shufMB, _mm256_srli_si256(shufMB, 1));
				
				// blend escape chars in
				__m256i tmp1 = _mm256_add_epi8(data1, _mm256_set1_epi8(64));
				__m256i tmp2 = _mm256_add_epi8(data2, _mm256_set1_epi8(64));
				data1 = _mm256_blendv_epi8(data1, equals, maskEscA);
				data2 = _mm256_blendv_epi8(data2, equals, maskEscB);
				data1 = _mm256_blendv_epi8(data1, tmp1, _mm256_slli_si256(maskEscA, 1));
				data2 = _mm256_blendv_epi8(data2, tmp2, _mm256_slli_si256(maskEscB, 1));
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
		*(uint16_t*)p = UINT16_PACK('\r', '\n');
		p += 2;
	}
	
	_mm256_zeroupper();
	
	end:
	return p - dest;
}
#endif

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
	
	return p - dest;
}
*/


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

#include "./crcutil-1.0/examples/interface.h"
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
		line_size = args[1]->ToInt32()->Value();
		if (line_size < 1) line_size = 128;
		if (args.Length() >= 3) {
			col = args[2]->ToInt32()->Value();
			if (col >= line_size) col = 0;
		}
	}
	
	// allocate enough memory to handle worst case requirements
	size_t dest_len = YENC_MAX_SIZE(arg_len, line_size);
	
	unsigned char *result = (unsigned char*) malloc(dest_len);
	size_t len = do_encode(line_size, col, (const unsigned char*)node::Buffer::Data(args[0]), result, arg_len);
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
		line_size = args[2]->ToInt32()->Value();
		if (line_size < 1) line_size = 128;
		if (args.Length() >= 4) {
			col = args[3]->ToInt32()->Value();
			if (col >= line_size) col = 0;
		}
	}
	
	// check that destination buffer has enough space
	size_t dest_len = YENC_MAX_SIZE(arg_len, line_size);
	if(node::Buffer::Length(args[1]) < dest_len) {
		args.GetReturnValue().Set( Integer::New(isolate, 0) );
		return;
	}
	
	size_t len = do_encode(line_size, col, (const unsigned char*)node::Buffer::Data(args[0]), (unsigned char*)node::Buffer::Data(args[1]), arg_len);
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
		line_size = args[1]->ToInt32()->Value();
		if (line_size < 1) line_size = 128;
		if (args.Length() >= 3) {
			col = args[2]->ToInt32()->Value();
			if (col >= line_size) col = 0;
		}
	}
	
	// allocate enough memory to handle worst case requirements
	size_t dest_len = YENC_MAX_SIZE(arg_len, line_size);
	
	unsigned char *result = (unsigned char*) malloc(dest_len);
	size_t len = do_encode(line_size, col, (const unsigned char*)node::Buffer::Data(args[0]), result, arg_len);
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
		line_size = args[2]->ToInt32()->Value();
		if (line_size < 1) line_size = 128;
		if (args.Length() >= 4) {
			col = args[3]->ToInt32()->Value();
			if (col >= line_size) col = 0;
		}
	}
	
	// check that destination buffer has enough space
	size_t dest_len = YENC_MAX_SIZE(arg_len, line_size);
	if(node::Buffer::Length(args[1]) < dest_len) {
		return scope.Close(Integer::New(0));
	}
	
	size_t len = do_encode(line_size, col, (const unsigned char*)node::Buffer::Data(args[0]), (unsigned char*)node::Buffer::Data(args[1]), arg_len);
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
#ifdef __SSE4_1__
	fastYencMask |= 0x80000;
#endif
	_do_encode = ((flags & fastYencMask) == fastYencMask) ? &do_encode_fast : &do_encode_slow; // SSSE3 + required stuff based on compiler flags
	
	if((flags & fastYencMask) == fastYencMask) {
		// generate shuf LUT
		for(int i=0; i<256; i++) {
			int k = i;
			uint8_t res[16];
			int p = 0;
			for(int j=0; j<8; j++) {
				res[j+p] = j;
				if(k & 1) {
					p++;
					res[j+p] = j;
				}
				k >>= 1;
			}
			for(; p<8; p++)
				res[8+p] = 8+p;
			_mm_store_si128(shufLUT + i, _mm_loadu_si128((__m128i*)res));
		}
	}
#endif
}

NODE_MODULE(yencode, init);
