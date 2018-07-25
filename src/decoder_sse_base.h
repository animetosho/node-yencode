
#ifdef __SSE2__
static uint8_t eqFixLUT[256];
ALIGN_32(static __m64 eqAddLUT[256]);
ALIGN_32(static __m64 unshufLUT[256]); // not needed for SSE2 method, but declared because it's written to for LUT creation
#ifdef __SSSE3__
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

template<bool isRaw, bool searchEnd, bool use_ssse3>
inline void do_decode_sse(const uint8_t* src, long& len, unsigned char*& p, unsigned char& escFirst, uint16_t& nextMask) {
	for(long i = -len; i; i += sizeof(__m128i)) {
		__m128i data = _mm_load_si128((__m128i *)(src+i));
		
		// search for special chars
		__m128i cmpEq = _mm_cmpeq_epi8(data, _mm_set1_epi8('=')),
#ifdef __AVX512VL__
		cmp = _mm_ternarylogic_epi32(
			_mm_cmpeq_epi8(data, _mm_set1_epi16(0x0a0d)),
			_mm_cmpeq_epi8(data, _mm_set1_epi16(0x0d0a)),
			cmpEq,
			0xFE
		);
#else
		cmp = _mm_or_si128(
			_mm_or_si128(
				_mm_cmpeq_epi8(data, _mm_set1_epi16(0x0a0d)), // \r\n
				_mm_cmpeq_epi8(data, _mm_set1_epi16(0x0d0a))  // \n\r
			),
			cmpEq
		);
#endif
		uint16_t mask = _mm_movemask_epi8(cmp); // not the most accurate mask if we have invalid sequences; we fix this up later
		
		__m128i oData;
		if(escFirst) { // rarely hit branch: seems to be faster to use 'if' than a lookup table, possibly due to values being able to be held in registers?
			// first byte needs escaping due to preceeding = in last loop iteration
			oData = _mm_sub_epi8(data, _mm_set_epi8(42,42,42,42,42,42,42,42,42,42,42,42,42,42,42,42+64));
			mask &= ~1;
		} else {
			oData = _mm_sub_epi8(data, _mm_set1_epi8(42));
		}
		if(isRaw) mask |= nextMask;
		
		if (mask != 0) {
			// a spec compliant encoder should never generate sequences: ==, =\n and =\r, but we'll handle them to be spec compliant
			// the yEnc specification requires any character following = to be unescaped, not skipped over, so we'll deal with that
			
#define LOAD_HALVES(a, b) _mm_castps_si128(_mm_loadh_pi( \
	_mm_castsi128_ps(_mm_loadl_epi64((__m128i*)(a))), \
	(b) \
))
			// firstly, resolve invalid sequences of = to deal with cases like '===='
			uint16_t maskEq = _mm_movemask_epi8(cmpEq);
			uint16_t tmp = eqFixLUT[(maskEq&0xff) & ~escFirst];
			maskEq = (eqFixLUT[(maskEq>>8) & ~(tmp>>7)] << 8) | tmp;
			
			unsigned char oldEscFirst = escFirst;
			escFirst = (maskEq >> (sizeof(__m128i)-1));
			// next, eliminate anything following a `=` from the special char mask; this eliminates cases of `=\r` so that they aren't removed
			maskEq <<= 1;
			mask &= ~maskEq;
			
			// unescape chars following `=`
#if defined(__AVX512VL__) && defined(__AVX512BW__)
			// GCC < 7 seems to generate rubbish assembly for this
			oData = _mm_mask_add_epi8(
				oData,
				maskEq,
				oData,
				_mm_set1_epi8(-64)
			);
#else
			oData = _mm_add_epi8(
				oData,
				LOAD_HALVES(
					eqAddLUT + (maskEq&0xff),
					eqAddLUT + ((maskEq>>8)&0xff)
				)
			);
#endif
			
			// handle \r\n. sequences
			// RFC3977 requires the first dot on a line to be stripped, due to dot-stuffing
			if(isRaw || searchEnd) {
				// find instances of \r\n
				__m128i tmpData1, tmpData2, tmpData3, tmpData4;
#if defined(__SSSE3__) && !defined(__tune_btver1__)
				if(use_ssse3) {
					__m128i nextData = _mm_loadl_epi64((__m128i *)(src+i) + 1);
					tmpData1 = _mm_alignr_epi8(nextData, data, 1);
					tmpData2 = _mm_alignr_epi8(nextData, data, 2);
					if(searchEnd) tmpData3 = _mm_alignr_epi8(nextData, data, 3);
					if(searchEnd && isRaw) tmpData4 = _mm_alignr_epi8(nextData, data, 4);
				} else {
#endif
					tmpData1 = _mm_insert_epi16(_mm_srli_si128(data, 1), *(uint16_t*)((src+i) + sizeof(__m128i)-1), 7);
					tmpData2 = _mm_insert_epi16(_mm_srli_si128(data, 2), *(uint16_t*)((src+i) + sizeof(__m128i)), 7);
					if(searchEnd)
						tmpData3 = _mm_insert_epi16(_mm_srli_si128(tmpData1, 2), *(uint16_t*)((src+i) + sizeof(__m128i)+1), 7);
					if(searchEnd && isRaw)
						tmpData4 = _mm_insert_epi16(_mm_srli_si128(tmpData2, 2), *(uint16_t*)((src+i) + sizeof(__m128i)+2), 7);
#ifdef __SSSE3__
				}
#endif
				__m128i matchNl1 = _mm_cmpeq_epi16(data, _mm_set1_epi16(0x0a0d));
				__m128i matchNl2 = _mm_cmpeq_epi16(tmpData1, _mm_set1_epi16(0x0a0d));
				
				__m128i matchDots, matchNlDots;
				uint16_t killDots;
				if(isRaw) {
					matchDots = _mm_cmpeq_epi8(tmpData2, _mm_set1_epi8('.'));
					// merge preparation (for non-raw, it doesn't matter if this is shifted or not)
					matchNl1 = _mm_srli_si128(matchNl1, 1);
					
					// merge matches of \r\n with those for .
#ifdef __AVX512VL__
					matchNlDots = _mm_ternarylogic_epi32(matchDots, matchNl1, matchNl2, 0xE0);
#else
					matchNlDots = _mm_and_si128(matchDots, _mm_or_si128(matchNl1, matchNl2));
#endif
					killDots = _mm_movemask_epi8(matchNlDots);
				}
				
				if(searchEnd) {
					__m128i cmpB1 = _mm_cmpeq_epi16(tmpData2, _mm_set1_epi16(0x793d)); // "=y"
					__m128i cmpB2 = _mm_cmpeq_epi16(tmpData3, _mm_set1_epi16(0x793d));
					if(isRaw && killDots) {
						// match instances of \r\n.\r\n and \r\n.=y
						__m128i cmpC1 = _mm_cmpeq_epi16(tmpData3, _mm_set1_epi16(0x0a0d)); // "\r\n"
						__m128i cmpC2 = _mm_cmpeq_epi16(tmpData4, _mm_set1_epi16(0x0a0d));
						cmpC1 = _mm_or_si128(cmpC1, cmpB2);
						cmpC2 = _mm_or_si128(cmpC2, _mm_cmpeq_epi16(tmpData4, _mm_set1_epi16(0x793d)));
						cmpC2 = _mm_slli_si128(cmpC2, 1);
						
						// prepare cmpB
						cmpB1 = _mm_and_si128(cmpB1, matchNl1);
						cmpB2 = _mm_and_si128(cmpB2, matchNl2);
						
						// and w/ dots
#ifdef __AVX512VL__
						cmpC1 = _mm_ternarylogic_epi32(cmpC1, cmpC2, matchNlDots, 0xA8);
						cmpB1 = _mm_ternarylogic_epi32(cmpB1, cmpB2, cmpC1, 0xFE);
#else
						cmpC1 = _mm_and_si128(_mm_or_si128(cmpC1, cmpC2), matchNlDots);
						cmpB1 = _mm_or_si128(cmpC1, _mm_or_si128(
							cmpB1, cmpB2
						));
#endif
					} else {
#ifdef __AVX512VL__
						cmpB1 = _mm_ternarylogic_epi32(cmpB1, matchNl1, _mm_and_si128(cmpB2, matchNl2), 0xEA);
#else
						cmpB1 = _mm_or_si128(
							_mm_and_si128(cmpB1, matchNl1),
							_mm_and_si128(cmpB2, matchNl2)
						);
#endif
					}
					if(_mm_movemask_epi8(cmpB1)) {
						// terminator found
						// there's probably faster ways to do this, but reverting to scalar code should be good enough
						escFirst = oldEscFirst;
						len += i;
						break;
					}
				}
				if(isRaw) {
					mask |= (killDots << 2) & 0xffff;
					nextMask = killDots >> (sizeof(__m128i)-2);
				}
			}
			
			// all that's left is to 'compress' the data (skip over masked chars)
#ifdef __SSSE3__
			if(use_ssse3) {
# if defined(__AVX512VBMI2__) && defined(__AVX512VL__) && defined(__POPCNT__)
#  if defined(__clang__) && __clang_major__ == 6 && __clang_minor__ == 0
				/* VBMI2 introduced in clang 6.0, but misnamed there; presumably will be fixed in 6.1 */
				_mm128_mask_compressstoreu_epi8(p, ~mask, oData);
#  else
				_mm_mask_compressstoreu_epi8(p, ~mask, oData);
#  endif
				p += XMM_SIZE - _mm_popcnt_u32(mask);
# else
#  if defined(__POPCNT__) && (defined(__tune_znver1__) || defined(__tune_btver2__))
				unsigned char skipped = _mm_popcnt_u32(mask & 0xff);
#  else
				unsigned char skipped = BitsSetTable256[mask & 0xff];
#  endif
				// lookup compress masks and shuffle
				// load up two halves
				__m128i shuf = LOAD_HALVES(unshufLUT + (mask&0xff), unshufLUT + (mask>>8));
				
				// offset upper half by 8
				shuf = _mm_add_epi8(shuf, _mm_set_epi32(0x08080808, 0x08080808, 0, 0));
				// shift down upper half into lower
				// TODO: consider using `mask & 0xff` in table instead of counting bits
				shuf = _mm_shuffle_epi8(shuf, _mm_load_si128(pshufb_combine_table + skipped));
				
				// shuffle data
				oData = _mm_shuffle_epi8(oData, shuf);
				STOREU_XMM(p, oData);
				
				// increment output position
#  if defined(__POPCNT__) && !defined(__tune_btver1__)
				p += XMM_SIZE - _mm_popcnt_u32(mask);
#  else
				p += XMM_SIZE - skipped - BitsSetTable256[mask >> 8];
#  endif
# endif
			} else {
#endif
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
#ifdef __SSSE3__
			}
#endif
#undef LOAD_HALVES
		} else {
			STOREU_XMM(p, oData);
			p += XMM_SIZE;
			escFirst = 0;
			if(isRaw) nextMask = 0;
		}
	}
}
#endif
