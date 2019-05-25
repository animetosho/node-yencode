
#ifdef __SSE2__

template<bool isRaw, bool searchEnd, enum YEncDecIsaLevel use_isa>
inline void do_decode_sse(const uint8_t* src, long& len, unsigned char*& p, unsigned char& _escFirst, uint16_t& _nextMask) {
	int escFirst = _escFirst;
	int nextMask = _nextMask;
	for(long i = -len; i; i += sizeof(__m128i)) {
		__m128i data = _mm_load_si128((__m128i *)(src+i));
		
		// search for special chars
		__m128i cmpEq = _mm_cmpeq_epi8(data, _mm_set1_epi8('='));
		__m128i cmp;
#ifdef __AVX512VL__
		if(use_isa >= ISA_LEVEL_AVX3) {
			cmp = _mm_ternarylogic_epi32(
				_mm_cmpeq_epi8(data, _mm_set1_epi16(0x0a0d)),
				_mm_cmpeq_epi8(data, _mm_set1_epi16(0x0d0a)),
				cmpEq,
				0xFE
			);
		} else
#endif
		{
			cmp = _mm_or_si128(
				_mm_or_si128(
					_mm_cmpeq_epi8(data, _mm_set1_epi16(0x0a0d)), // \r\n
					_mm_cmpeq_epi8(data, _mm_set1_epi16(0x0d0a))  // \n\r
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
				// find instances of \r\n
				__m128i tmpData1, tmpData2, tmpData3, nextData;
#if defined(__SSSE3__) && !defined(__tune_btver1__)
				if(use_isa >= ISA_LEVEL_SSSE3) {
					nextData = _mm_cvtsi32_si128(*(uint32_t*)(src+i + sizeof(__m128i)));
					tmpData1 = _mm_alignr_epi8(nextData, data, 1);
					tmpData2 = _mm_alignr_epi8(nextData, data, 2);
					if(searchEnd) tmpData3 = _mm_alignr_epi8(nextData, data, 3);
				} else
#endif
				{
					tmpData1 = _mm_insert_epi16(_mm_srli_si128(data, 1), *(uint16_t*)((src+i) + sizeof(__m128i)-1), 7);
					tmpData2 = _mm_insert_epi16(_mm_srli_si128(data, 2), *(uint16_t*)((src+i) + sizeof(__m128i)), 7);
					if(searchEnd)
						tmpData3 = _mm_insert_epi16(_mm_srli_si128(tmpData1, 2), *(uint16_t*)((src+i) + sizeof(__m128i)+1), 7);
				}
				__m128i matchNl1 = _mm_cmpeq_epi16(data, _mm_set1_epi16(0x0a0d));
				__m128i matchNl2 = _mm_cmpeq_epi16(tmpData1, _mm_set1_epi16(0x0a0d));
				
				__m128i matchNlDots;
				int killDots;
				if(isRaw) {
					__m128i matchDots = _mm_cmpeq_epi8(tmpData2, _mm_set1_epi8('.'));
					// merge preparation (for non-raw, it doesn't matter if this is shifted or not)
					matchNl1 = _mm_and_si128(matchNl1, _mm_set1_epi16(0xff));
					
					// merge matches of \r\n with those for .
#ifdef __AVX512VL__
					if(use_isa >= ISA_LEVEL_AVX3)
						matchNlDots = _mm_ternarylogic_epi32(matchDots, matchNl1, matchNl2, 0xE0);
					else
#endif
						matchNlDots = _mm_and_si128(matchDots, _mm_or_si128(matchNl1, matchNl2));
					killDots = _mm_movemask_epi8(matchNlDots); // using PTEST to substitute this and above AND doesn't seem to be worth it
					if(!searchEnd) {
						mask |= (killDots << 2) & 0xffff;
						nextMask = killDots >> (sizeof(__m128i)-2);
					}
				}
				
				if(searchEnd) {
					__m128i cmpB1 = _mm_cmpeq_epi16(tmpData2, _mm_set1_epi16(0x793d)); // "=y"
					__m128i cmpB2 = _mm_cmpeq_epi16(tmpData3, _mm_set1_epi16(0x793d));
					if(isRaw && LIKELIHOOD(0.001, killDots!=0)) {
						__m128i tmpData4;
#if defined(__SSSE3__) && !defined(__tune_btver1__)
						if(use_isa >= ISA_LEVEL_SSSE3)
							tmpData4 = _mm_alignr_epi8(nextData, data, 4);
						else
#endif
							tmpData4 = _mm_insert_epi16(_mm_srli_si128(tmpData2, 2), *(uint16_t*)((src+i) + sizeof(__m128i)+2), 7);
						
						// match instances of \r\n.\r\n and \r\n.=y
						__m128i cmpC1 = _mm_cmpeq_epi16(tmpData3, _mm_set1_epi16(0x0a0d)); // "\r\n"
						__m128i cmpC2 = _mm_cmpeq_epi16(tmpData4, _mm_set1_epi16(0x0a0d));
						__m128i cmpB3 = _mm_cmpeq_epi16(tmpData4, _mm_set1_epi16(0x793d));
						cmpC1 = _mm_or_si128(cmpC1, cmpB2);
						
#ifdef __AVX512VL__
						if(use_isa >= ISA_LEVEL_AVX3) {
							cmpC2 = _mm_ternarylogic_epi32(cmpC2, cmpB3, _mm_set1_epi16(0xff), 0x54); // (cmpC2 | cmpB3) & ~0x00ff
							cmpC1 = _mm_ternarylogic_epi32(cmpC1, cmpC2, matchNlDots, 0xA8); // (cmpC1 | cmpC2) & matchNlDots
							cmpB2 = _mm_ternarylogic_epi32(cmpB2, matchNl2, cmpC1, 0xEA); // (cmpB2 & matchNl2) | cmpC1
							cmpB1 = _mm_ternarylogic_epi32(cmpB1, matchNl1, cmpB2, 0xEA); // (cmpB1 & matchNl1) | cmpB2
						} else
#endif
						{
							cmpC2 = _mm_andnot_si128(_mm_set1_epi16(0xff), _mm_or_si128(cmpC2, cmpB3));
							// prepare cmpB
							cmpB1 = _mm_and_si128(cmpB1, matchNl1);
							cmpB2 = _mm_and_si128(cmpB2, matchNl2);
							// and w/ dots
							cmpC1 = _mm_and_si128(_mm_or_si128(cmpC1, cmpC2), matchNlDots);
							cmpB1 = _mm_or_si128(cmpC1, _mm_or_si128(
								cmpB1, cmpB2
							));
						}
						
						if(LIKELIHOOD(0.001, _mm_movemask_epi8(cmpB1))) {
							// terminator found
							// there's probably faster ways to do this, but reverting to scalar code should be good enough
							escFirst = oldEscFirst;
							len += i;
							break;
						}
						mask |= (killDots << 2) & 0xffff;
						nextMask = killDots >> (sizeof(__m128i)-2);
					} else {
						if(LIKELIHOOD(0.001, _mm_movemask_epi8(
							_mm_or_si128(cmpB1, cmpB2)
						))) {
							// if the rare case of '=y' is found, do a more precise check
#ifdef __AVX512VL__
							if(use_isa >= ISA_LEVEL_AVX3)
								cmpB1 = _mm_ternarylogic_epi32(cmpB1, matchNl1, _mm_and_si128(cmpB2, matchNl2), 0xEA);
							else
#endif
								cmpB1 = _mm_or_si128(
									_mm_and_si128(cmpB1, matchNl1),
									_mm_and_si128(cmpB2, matchNl2)
								);
							if(_mm_movemask_epi8(cmpB1)) {
								escFirst = oldEscFirst;
								len += i;
								break;
							}
						}
						if(isRaw) nextMask = 0;
					}
				}
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
				ALIGN_32(uint32_t mmTmp[4]);
				_mm_store_si128((__m128i*)mmTmp, oData);
				
				for(int j=0; j<4; j++) {
					if(LIKELIHOOD(0.3 /*rough estimate*/, (mask & 0xf) != 0)) {
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
