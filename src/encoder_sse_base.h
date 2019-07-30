#include "common.h"

// slightly faster version which improves the worst case scenario significantly; since worst case doesn't happen often, overall speedup is relatively minor
// requires PSHUFB (SSSE3) instruction, but will use POPCNT (SSE4.2 (or AMD's ABM, but Phenom doesn't support SSSE3 so doesn't matter)) if available (these only seem to give minor speedups, so considered optional)
#include "encoder.h"
#include "encoder_common.h"

#ifdef __SSSE3__
#pragma pack(16)
struct TShufMix {
	__m128i shuf, mix;
};
#pragma pack()
static struct TShufMix ALIGN_TO(32, shufMixLUT[256]);


static uint16_t expandLUT[256];

static void encoder_ssse3_lut() {
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
#endif

// for SSE2 expanding
static const uint8_t ALIGN_TO(16, _expand_mix_table[256]) = {
	'=', 64, 0 , 0 , 0 , 0 , 0 , 0 , 0 , 0 , 0 , 0 , 0 , 0 , 0 , 0 ,
	 0 ,'=', 64, 0 , 0 , 0 , 0 , 0 , 0 , 0 , 0 , 0 , 0 , 0 , 0 , 0 ,
	 0 , 0 ,'=', 64, 0 , 0 , 0 , 0 , 0 , 0 , 0 , 0 , 0 , 0 , 0 , 0 ,
	 0 , 0 , 0 ,'=', 64, 0 , 0 , 0 , 0 , 0 , 0 , 0 , 0 , 0 , 0 , 0 ,
	 0 , 0 , 0 , 0 ,'=', 64, 0 , 0 , 0 , 0 , 0 , 0 , 0 , 0 , 0 , 0 ,
	 0 , 0 , 0 , 0 , 0 ,'=', 64, 0 , 0 , 0 , 0 , 0 , 0 , 0 , 0 , 0 ,
	 0 , 0 , 0 , 0 , 0 , 0 ,'=', 64, 0 , 0 , 0 , 0 , 0 , 0 , 0 , 0 ,
	 0 , 0 , 0 , 0 , 0 , 0 , 0 ,'=', 64, 0 , 0 , 0 , 0 , 0 , 0 , 0 ,
	 0 , 0 , 0 , 0 , 0 , 0 , 0 , 0 ,'=', 64, 0 , 0 , 0 , 0 , 0 , 0 ,
	 0 , 0 , 0 , 0 , 0 , 0 , 0 , 0 , 0 ,'=', 64, 0 , 0 , 0 , 0 , 0 ,
	 0 , 0 , 0 , 0 , 0 , 0 , 0 , 0 , 0 , 0 ,'=', 64, 0 , 0 , 0 , 0 ,
	 0 , 0 , 0 , 0 , 0 , 0 , 0 , 0 , 0 , 0 , 0 ,'=', 64, 0 , 0 , 0 ,
	 0 , 0 , 0 , 0 , 0 , 0 , 0 , 0 , 0 , 0 , 0 , 0 ,'=', 64, 0 , 0 ,
	 0 , 0 , 0 , 0 , 0 , 0 , 0 , 0 , 0 , 0 , 0 , 0 , 0 ,'=', 64, 0 ,
	 0 , 0 , 0 , 0 , 0 , 0 , 0 , 0 , 0 , 0 , 0 , 0 , 0 , 0 ,'=', 64,
	 0 , 0 , 0 , 0 , 0 , 0 , 0 , 0 , 0 , 0 , 0 , 0 , 0 , 0 , 0 ,'='
};
static const __m128i* expand_mix_table = (const __m128i*)_expand_mix_table;

static const int8_t ALIGN_TO(16, _expand_mask_table[256]) = {
	 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	-1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	-1,-1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	-1,-1,-1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	-1,-1,-1,-1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	-1,-1,-1,-1,-1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	-1,-1,-1,-1,-1,-1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	-1,-1,-1,-1,-1,-1,-1, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	-1,-1,-1,-1,-1,-1,-1,-1, 0, 0, 0, 0, 0, 0, 0, 0,
	-1,-1,-1,-1,-1,-1,-1,-1,-1, 0, 0, 0, 0, 0, 0, 0,
	-1,-1,-1,-1,-1,-1,-1,-1,-1,-1, 0, 0, 0, 0, 0, 0,
	-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1, 0, 0, 0, 0, 0,
	-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1, 0, 0, 0, 0,
	-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1, 0, 0, 0,
	-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1, 0, 0,
	-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1, 0
};
static const __m128i* expand_mask_table = (const __m128i*)_expand_mask_table;

// for LZCNT/BSF
#ifdef _MSC_VER
# include <intrin.h>
# include <ammintrin.h>
#else
# include <x86intrin.h>
#endif


static const unsigned char* escapeLUT;
static const uint16_t* escapedLUT;

template<enum YEncDecIsaLevel use_isa>
static size_t do_encode_sse(int line_size, int* colOffset, const unsigned char* src, unsigned char* dest, size_t len) {
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
		while (i < -1-XMM_SIZE && col < line_size-1) {
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
#ifdef __SSSE3__
				if(use_isa >= ISA_LEVEL_SSSE3) {
					uint8_t m1 = mask & 0xFF;
					uint8_t m2 = mask >> 8;
					__m128i shufMA, shufMB;
					__m128i data2;
					
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
						if(ovrflowAmt > 0) {
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
						if(ovrflowAmt > 0) { // calculate parts if overflowed
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
					if(LIKELIHOOD(0.15 /*guess, using 128b lines*/, ovrflowAmt > 0)) {
# if defined(__POPCNT__)
						if(use_isa >= ISA_LEVEL_AVX) {
							// from experimentation, it doesn't seem like it's worth trying to branch here, i.e. it isn't worth trying to avoid a pmovmskb+shift+or by checking overflow amount
							uint32_t eqMask = (_mm_movemask_epi8(shufMB) << shufALen) | _mm_movemask_epi8(shufMA);
							eqMask >>= shufBLen+shufALen - ovrflowAmt -1;
							i -= ovrflowAmt - popcnt32(eqMask);
							p -= ovrflowAmt - (eqMask & 1);
							if(eqMask & 1)
								goto after_last_char_fast;
							else
								goto last_char_fast;
						} else
# endif
						{
							// we overflowed - find correct position to revert back to
							p -= ovrflowAmt;
							if(ovrflowAmt == shufBLen) {
								i -= 8;
								goto last_char_fast;
							} else {
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
								if(isEsc)
									goto after_last_char_fast;
								else
									goto last_char_fast;
							}
						}
					}
				} else
#endif
				{
					if(mask & (mask-1)) {
						unsigned char* sp = p;
						uint32_t ALIGN_TO(16, mmTmp[4]);
						// special characters exist
						_mm_store_si128((__m128i*)mmTmp, data);
						#define DO_THING(n) \
							c = es[i-XMM_SIZE+n], escaped = escapeLUT[c]; \
							if (LIKELIHOOD(0.9844, escaped != 0)) \
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
							i -= XMM_SIZE;
							break;
						}
						#undef DO_THING_4
						#undef DO_THING
					} else {
						// shortcut for common case of only 1 bit set
#if defined(__LZCNT__)
						// lzcnt is faster than bsf on AMD
						intptr_t bitIndex = 31 - _lzcnt_u32(mask);
#else
# ifdef _MSC_VER
						unsigned long bitIndex;
						_BitScanForward(&bitIndex, mask);
# else
						int bitIndex = _bit_scan_forward(mask);
# endif
#endif
						__m128i mergeMask = _mm_load_si128(expand_mask_table + bitIndex);
						data = _mm_or_si128(
							_mm_and_si128(mergeMask, data),
							_mm_slli_si128(_mm_andnot_si128(mergeMask, data), 1)
						);
						// add escape chars
						data = _mm_add_epi8(data, _mm_load_si128(expand_mix_table + bitIndex));
						
						// store main part
						STOREU_XMM(p, data);
						// store final char
						p[XMM_SIZE] = es[i-1] + 42 + (64 & (mask>>(XMM_SIZE-1-6)));
						
						p += XMM_SIZE + 1;
						col += XMM_SIZE + 1;
						
						int ovrflowAmt = col - (line_size-1);
						if(LIKELIHOOD(0.15, ovrflowAmt > 0)) {
							bitIndex = 15-bitIndex;
							if(ovrflowAmt-1 == bitIndex) {
								// this is an escape character, so line will need to overflow
								p -= ovrflowAmt - 1;
								i -= ovrflowAmt - 1;
								goto after_last_char_fast;
							} else {
								int overflowedPastEsc = (unsigned int)(ovrflowAmt-1) > (unsigned int)bitIndex;
								p -= ovrflowAmt;
								i -= ovrflowAmt - overflowedPastEsc;
								goto last_char_fast;
							}
						}
					}
				}
			} else {
				STOREU_XMM(p, data);
				p += XMM_SIZE;
				col += XMM_SIZE;
				if(LIKELIHOOD(0.15, col > line_size-1)) {
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
			last_char_fast:
			c = es[i++];
			if (LIKELIHOOD(0.0234, escapedLUT[c] && c != '.'-42)) {
				*(uint16_t*)p = escapedLUT[c];
				p += 2;
			} else {
				*(p++) = c + 42;
			}
		}
		
		after_last_char_fast:
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

