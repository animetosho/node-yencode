
#ifdef __SSE2__

#define _X(n) -(n>0),-(n>1),-(n>2),-(n>3),-(n>4),-(n>5),-(n>6),-(n>7),-(n>8),-(n>9),-(n>10),-(n>11),-(n>12),-(n>13),-(n>14),-(n>15)

#ifdef __LZCNT__
ALIGN_32(static const int8_t _unshuf_mask_lzc_table[256]) = {
	_X(15), _X(14), _X(13), _X(12), _X(11), _X(10), _X(9), _X(8),
	_X(7), _X(6), _X(5), _X(4), _X(3), _X(2), _X(1), _X(0)
};
static const __m128i* unshuf_mask_lzc_table = (const __m128i*)_unshuf_mask_lzc_table - 16;
#else
ALIGN_32(static const int8_t _unshuf_mask_bsr_table[256]) = {
	_X(0), _X(1), _X(2), _X(3), _X(4), _X(5), _X(6), _X(7),
	_X(8), _X(9), _X(10), _X(11), _X(12), _X(13), _X(14), _X(15)
};
static const __m128i* unshuf_mask_bsr_table = (const __m128i*)_unshuf_mask_bsr_table;
#endif

#undef _X

#include <x86intrin.h> // for LZCNT/BSR

template<bool isRaw, bool searchEnd, enum YEncDecIsaLevel use_isa>
inline void do_decode_sse(const uint8_t* src, long& len, unsigned char*& p, unsigned char& _escFirst, uint16_t& _nextMask) {
	int escFirst = _escFirst;
	int nextMask = _nextMask;
	for(long i = -len; i; i += sizeof(__m128i)) {
		__m128i data = _mm_load_si128((__m128i *)(src+i));
		
		// search for special chars
		__m128i cmpEq = _mm_cmpeq_epi8(data, _mm_set1_epi8('='));
		__m128i cmpCr = _mm_cmpeq_epi8(data, _mm_set1_epi8('\r'));
		__m128i cmp;
#ifdef __AVX512VL__
		if(use_isa >= ISA_LEVEL_AVX3) {
			cmp = _mm_ternarylogic_epi32(
				_mm_cmpeq_epi8(data, _mm_set1_epi8('\n')),
				cmpCr,
				cmpEq,
				0xFE
			);
		} else
#endif
		{
			cmp = _mm_or_si128(
				_mm_or_si128(
					_mm_cmpeq_epi8(data, _mm_set1_epi8('\n')), cmpCr
				),
				cmpEq
			);
		}

		int mask = _mm_movemask_epi8(cmp); // not the most accurate mask if we have invalid sequences; we fix this up later
		int oMask = mask;
		
		__m128i oData;
		if(LIKELIHOOD(0.01 /* guess */, escFirst!=0)) { // rarely hit branch: seems to be faster to use 'if' than a lookup table, possibly due to values being able to be held in registers?
			// first byte needs escaping due to preceeding = in last loop iteration
			oData = _mm_add_epi8(data, _mm_set_epi8(-42,-42,-42,-42,-42,-42,-42,-42,-42,-42,-42,-42,-42,-42,-42,-42-64));
			mask &= 0xfffe;
		} else {
			oData = _mm_add_epi8(data, _mm_set1_epi8(-42));
		}
		if(isRaw) mask |= nextMask;
		
		if (LIKELIHOOD(0.25 /* rough guess */, mask != 0)) {
			
#define LOAD_HALVES(a, b) _mm_castps_si128(_mm_loadh_pi( \
	_mm_castsi128_ps(_mm_loadl_epi64((__m128i*)(a))), \
	(__m64*)(b) \
))
			
			// a spec compliant encoder should never generate sequences: ==, =\n and =\r, but we'll handle them to be spec compliant
			// the yEnc specification requires any character following = to be unescaped, not skipped over, so we'll deal with that
			// firstly, check for invalid sequences of = (we assume that these are rare, as a spec compliant yEnc encoder should not generate these)
			int maskEq = _mm_movemask_epi8(cmpEq);
			bool checkNewlines = (isRaw || searchEnd) && LIKELIHOOD(0.15, mask != maskEq);
			int oldEscFirst = escFirst;
			if(LIKELIHOOD(0.0001, (oMask & ((maskEq << 1) + escFirst)) != 0)) {
				// resolve invalid sequences of = to deal with cases like '===='
				int tmp = eqFixLUT[(maskEq&0xff) & ~escFirst];
				maskEq = (eqFixLUT[(maskEq>>8) & ~(tmp>>7)] << 8) | tmp;
				
				escFirst = (maskEq >> (sizeof(__m128i)-1));
				// next, eliminate anything following a `=` from the special char mask; this eliminates cases of `=\r` so that they aren't removed
				maskEq <<= 1;
				mask &= ~maskEq;
				
				// unescape chars following `=`
#if defined(__AVX512VL__) && defined(__AVX512BW__)
				if(use_isa >= ISA_LEVEL_AVX3) {
					// GCC < 7 seems to generate rubbish assembly for this
					oData = _mm_mask_add_epi8(
						oData,
						maskEq,
						oData,
						_mm_set1_epi8(-64)
					);
				} else
#endif
				{
					oData = _mm_add_epi8(
						oData,
						LOAD_HALVES(
							eqAddLUT + (maskEq&0xff),
							eqAddLUT + ((maskEq>>8)&0xff)
						)
					);
				}
			} else {
				// no invalid = sequences found - we can cut out some things from above
				// this code path is a shortened version of above; it's here because it's faster, and what we'll be dealing with most of the time
				escFirst = (maskEq >> (sizeof(__m128i)-1));
				
#if defined(__AVX512VL__) && defined(__AVX512BW__)
				// using mask-add seems to be faster when doing complex checks, slower otherwise, maybe due to higher register pressure?
				if(use_isa >= ISA_LEVEL_AVX3 && (isRaw || searchEnd)) {
					oData = _mm_mask_add_epi8(oData, maskEq << 1, oData, _mm_set1_epi8(-64));
				} else
#endif
				{
					oData = _mm_add_epi8(
						oData,
						_mm_and_si128(
							_mm_slli_si128(cmpEq, 1),
							_mm_set1_epi8(-64)
						)
					);
				}
			}
			
			// handle \r\n. sequences
			// RFC3977 requires the first dot on a line to be stripped, due to dot-stuffing
			if(checkNewlines) {
				__m128i tmpData2 = _mm_loadu_si128((__m128i*)(src+i+2));
				__m128i match2Eq, match2Dot;
				
				if(searchEnd)
					match2Eq = _mm_cmpeq_epi8(_mm_set1_epi8('='), tmpData2);
				if(isRaw)
					match2Dot = _mm_cmpeq_epi8(tmpData2, _mm_set1_epi8('.'));
				
#if defined(__AVX__) // or more accurately, __SSE4_1__
# define TEST_VECT_NON_ZERO(a, b) (use_isa >= ISA_LEVEL_AVX ? !_mm_testz_si128(a, b) : _mm_movemask_epi8(_mm_and_si128(a, b)))
#else
# define TEST_VECT_NON_ZERO(a, b) _mm_movemask_epi8(_mm_and_si128(a, b))
#endif
				
				// find patterns of \r_.
				if(isRaw && LIKELIHOOD(0.001, TEST_VECT_NON_ZERO(cmpCr, match2Dot))) {
					__m128i tmpData1, tmpData3, tmpData4;
					if(searchEnd) {
#if defined(__SSSE3__) && !defined(__tune_btver1__)
						if(use_isa >= ISA_LEVEL_SSSE3) {
							__m128i nextData = _mm_cvtsi32_si128(*(uint32_t*)(src+i + sizeof(__m128i)));
							tmpData1 = _mm_alignr_epi8(nextData, data, 1);
							tmpData3 = _mm_alignr_epi8(nextData, data, 3);
							tmpData4 = _mm_alignr_epi8(nextData, data, 4);
						} else
#endif
						{
							tmpData1 = _mm_loadu_si128((__m128i*)(src+i+1));
							tmpData3 = _mm_loadu_si128((__m128i*)(src+i+3));
							tmpData4 = _mm_loadu_si128((__m128i*)(src+i+4));
						}
					} else {
						tmpData1 = _mm_loadu_si128((__m128i*)(src+i+1));
					}
					__m128i match1Lf = _mm_cmpeq_epi8(_mm_set1_epi8('\n'), tmpData1);
					
					__m128i match2NlDot, match1Nl;
					// merge matches for \r\n.
#ifdef __AVX512VL__
					if(use_isa >= ISA_LEVEL_AVX3)
						match2NlDot = _mm_ternarylogic_epi32(match2Dot, match1Lf, cmpCr, 0x80);
					else
#endif
					{
						match1Nl = _mm_and_si128(match1Lf, cmpCr);
						match2NlDot = _mm_and_si128(match2Dot, match1Nl);
					}
					if(searchEnd) {
						// match instances of \r\n.\r\n and \r\n.=y
						__m128i match3Cr = _mm_cmpeq_epi8(_mm_set1_epi8('\r'), tmpData3);
						__m128i match4Lf = _mm_cmpeq_epi8(tmpData4, _mm_set1_epi8('\n'));
						__m128i match4EqY = _mm_cmpeq_epi16(tmpData4, _mm_set1_epi16(0x793d)); // =y
						
						__m128i matchEnd;
						__m128i match3EqY = _mm_and_si128(match2Eq, _mm_cmpeq_epi8(_mm_set1_epi8('y'), tmpData3));
#ifdef __AVX512VL__
						if(use_isa >= ISA_LEVEL_AVX3) {
							// match \r\n=y
							__m128i match3End = _mm_ternarylogic_epi32(match1Lf, cmpCr, match3EqY, 0x80); // match3EqY & match1Nl
							__m128i match34EqY = _mm_ternarylogic_epi32(match4EqY, _mm_srli_epi16(match3EqY, 8), _mm_set1_epi16(0xff00), 0xEC); // (match4EqY & 0xff00) | (match3EqY >> 8)
							// merge \r\n and =y matches for tmpData4
							__m128i match4End = _mm_ternarylogic_epi32(match34EqY, match3Cr, match4Lf, 0xF8); // (match3Cr & match4Lf) | match34EqY
							// merge with \r\n. and combine
							matchEnd = _mm_ternarylogic_epi32(match4End, match2NlDot, match3End, 0xEA); // (match4End & match2NlDot) | match3End
						} else
#endif
						{
							match4EqY = _mm_slli_epi16(match4EqY, 8); // TODO: also consider using PBLENDVB here with shifted match3EqY instead
							// merge \r\n and =y matches for tmpData4
							__m128i match4End = _mm_or_si128(
								_mm_and_si128(match3Cr, match4Lf),
								_mm_or_si128(match4EqY, _mm_srli_epi16(match3EqY, 8)) // _mm_srli_si128 by 1 also works
							);
							// merge with \r\n.
							match4End = _mm_and_si128(match4End, match2NlDot);
							// match \r\n=y
							__m128i match3End = _mm_and_si128(match3EqY, match1Nl);
							// combine match sequences
							matchEnd = _mm_or_si128(match4End, match3End);
						}
						
						if(LIKELIHOOD(0.001, _mm_movemask_epi8(matchEnd))) {
							// terminator found
							// there's probably faster ways to do this, but reverting to scalar code should be good enough
							escFirst = oldEscFirst;
							len += i;
							break;
						}
					}
					int killDots = _mm_movemask_epi8(match2NlDot);
					mask |= (killDots << 2) & 0xffff;
					nextMask = killDots >> (sizeof(__m128i)-2);
				}
				else if(searchEnd) {
					__m128i match3Y = _mm_cmpeq_epi8(
						_mm_set1_epi8('y'),
						_mm_loadu_si128((__m128i*)(src+i+3))
					);
					if(LIKELIHOOD(0.001, TEST_VECT_NON_ZERO(match2Eq, match3Y))) {
						// if the rare case of '=y' is found, do a more precise check
						int endFound;
						__m128i match1Lf = _mm_cmpeq_epi8(
							_mm_set1_epi8('\n'),
							_mm_loadu_si128((__m128i*)(src+i+1))
						);
						
#ifdef __AVX512VL__
						if(use_isa >= ISA_LEVEL_AVX3)
							endFound = !_mm_testz_si128(
								_mm_ternarylogic_epi32(match3Y, match2Eq, match1Lf, 0x80),
								cmpCr
							);
						else
#endif
							endFound = TEST_VECT_NON_ZERO(
								_mm_and_si128(match2Eq, match3Y),
								_mm_and_si128(match1Lf, cmpCr)
							);
						
						if(endFound) {
							escFirst = oldEscFirst;
							len += i;
							break;
						}
					}
					if(isRaw) nextMask = 0;
				}
				else if(isRaw) // no \r_. found
					nextMask = 0;
#undef TEST_VECT_NON_ZERO
			}
			
			// all that's left is to 'compress' the data (skip over masked chars)
#ifdef __SSSE3__
			if(use_isa >= ISA_LEVEL_SSSE3) {
# if defined(__AVX512VBMI2__) && defined(__AVX512VL__) && defined(__POPCNT__)
				if(use_isa >= ISA_LEVEL_VBMI2) {
#  if defined(__clang__) && __clang_major__ == 6 && __clang_minor__ == 0
					/* VBMI2 introduced in clang 6.0, but misnamed there; presumably will be fixed in 6.1 */
					_mm128_mask_compressstoreu_epi8(p, ~mask, oData);
#  else
					_mm_mask_compressstoreu_epi8(p, ~mask, oData);
#  endif
					p += XMM_SIZE - _mm_popcnt_u32(mask);
				} else
# endif
				{
					
					oData = _mm_shuffle_epi8(oData, _mm_load_si128((__m128i*)(unshufLUTBig + (mask&0x7fff))));
					STOREU_XMM(p, oData);
					
					// increment output position
# if defined(__POPCNT__) && !defined(__tune_btver1__)
					if(use_isa >= ISA_LEVEL_AVX)
						p += XMM_SIZE - _mm_popcnt_u32(mask);
					else
# endif
						p += BitsSetTable256inv[mask & 0xff] + BitsSetTable256inv[(mask >> 8) & 0xff];
				}
			} else
#endif
			{
				
				intptr_t pAdvance = BitsSetTable256inv[mask & 0xff] + BitsSetTable256inv[(mask >> 8) & 0xff];
				do {
#ifdef __LZCNT__
					// lzcnt is always at least as fast as bsr, so prefer it if it's available
					intptr_t bitIndex = _lzcnt_u32(mask);
					__m128i mergeMask = _mm_load_si128(unshuf_mask_lzc_table + bitIndex);
					mask ^= 0x80000000U>>bitIndex;
#else
					intptr_t bitIndex = _bit_scan_reverse(mask);
					__m128i mergeMask = _mm_load_si128(unshuf_mask_bsr_table + bitIndex);
					mask ^= 1<<bitIndex;
#endif
					oData = _mm_or_si128(
						_mm_and_si128(mergeMask, oData),
						_mm_andnot_si128(mergeMask, _mm_srli_si128(oData, 1))
					);
				} while(mask);
				STOREU_XMM(p, oData);
				p += pAdvance;
			}
#undef LOAD_HALVES
		} else {
			STOREU_XMM(p, oData);
			p += XMM_SIZE;
			escFirst = 0;
		}
	}
	_escFirst = escFirst;
	_nextMask = nextMask;
}
#endif
