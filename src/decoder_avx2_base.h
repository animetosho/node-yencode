
#ifdef __AVX2__

template<bool isRaw, bool searchEnd, enum YEncDecIsaLevel use_isa>
inline void do_decode_avx2(const uint8_t* src, long& len, unsigned char*& p, unsigned char& _escFirst, uint16_t& _nextMask) {
	int escFirst = _escFirst;
	uint32_t nextMask = _nextMask;
	for(long i = -len; i; i += sizeof(__m256i)) {
		__m256i data = _mm256_load_si256((__m256i *)(src+i));
		
		// search for special chars
		__m256i cmpEq = _mm256_cmpeq_epi8(data, _mm256_set1_epi8('='));
		__m256i cmpCr = _mm256_cmpeq_epi8(data, _mm256_set1_epi8('\r'));
		__m256i cmp;
#ifdef __AVX512VL__
		if(use_isa >= ISA_LEVEL_AVX3) {
			cmp = _mm256_ternarylogic_epi32(
				_mm256_cmpeq_epi8(data, _mm256_set1_epi8('\n')),
				cmpCr,
				cmpEq,
				0xFE
			);
		} else
#endif
		{
			cmp = _mm256_or_si256(
				_mm256_or_si256(
					_mm256_cmpeq_epi8(data, _mm256_set1_epi8('\n')),
					cmpCr
				),
				cmpEq
			);
		}

		uint32_t mask = _mm256_movemask_epi8(cmp); // not the most accurate mask if we have invalid sequences; we fix this up later
		uint32_t oMask = mask;
		
		__m256i oData;
		if(LIKELIHOOD(0.01, escFirst!=0)) { // rarely hit branch: seems to be faster to use 'if' than a lookup table, possibly due to values being able to be held in registers?
			// first byte needs escaping due to preceeding = in last loop iteration
			oData = _mm256_add_epi8(data, _mm256_set_epi8(
				-42,-42,-42,-42,-42,-42,-42,-42,-42,-42,-42,-42,-42,-42,-42,-42,
				-42,-42,-42,-42,-42,-42,-42,-42,-42,-42,-42,-42,-42,-42,-42,-42-64
			));
			mask &= ~1;
		} else {
			oData = _mm256_add_epi8(data, _mm256_set1_epi8(-42));
		}
		if(isRaw) mask |= nextMask;
		
		if (LIKELIHOOD(0.42 /*guess*/, mask != 0)) {
			uint32_t maskEq = _mm256_movemask_epi8(cmpEq);
			bool checkNewlines = (isRaw || searchEnd) && LIKELIHOOD(0.3, mask != maskEq);
			int oldEscFirst = escFirst;
			if(LIKELIHOOD(0.0001, (oMask & ((maskEq << 1) + escFirst)) != 0)) {
				uint8_t tmp = eqFixLUT[(maskEq&0xff) & ~escFirst];
				uint32_t maskEq2 = tmp;
				for(int j=8; j<32; j+=8) {
					tmp = eqFixLUT[((maskEq>>j)&0xff) & ~(tmp>>7)];
					maskEq2 |= tmp<<j;
				}
				maskEq = maskEq2;
				
				escFirst = tmp>>7;
				// next, eliminate anything following a `=` from the special char mask; this eliminates cases of `=\r` so that they aren't removed
				maskEq <<= 1;
				mask &= ~maskEq;
				
				// unescape chars following `=`
#if defined(__AVX512VL__) && defined(__AVX512BW__)
				if(use_isa >= ISA_LEVEL_AVX3) {
					// GCC < 7 seems to generate rubbish assembly for this
					oData = _mm256_mask_add_epi8(
						oData,
						maskEq,
						oData,
						_mm256_set1_epi8(-64)
					);
				} else
#endif
				{
					// TODO: better to compute the mask instead??
					__m256i addMask = _mm256_set_epi64x(
						eqAddLUT[(maskEq>>24)&0xff],
						eqAddLUT[(maskEq>>16)&0xff],
						eqAddLUT[(maskEq>>8)&0xff],
						eqAddLUT[maskEq&0xff]
					);
					//__m256i addMask = _mm256_i32gather_epi64(_mm_cvtepu8_epi32(_mm_cvtsi32_si128(maskEq)), eqAddLUT, 8);
					oData = _mm256_add_epi8(oData, addMask);
				}
			} else {
				escFirst = (maskEq >> (sizeof(__m256i)-1));
				
#if defined(__AVX512VL__) && defined(__AVX512BW__)
				if(use_isa >= ISA_LEVEL_AVX3) {
					oData = _mm256_mask_add_epi8(
						oData,
						maskEq << 1,
						oData,
						_mm256_set1_epi8(-64)
					);
				} else
#endif
				{
					// << 1 byte
#if defined(__tune_znver1__) || defined(__tune_bdver4__)
					cmpEq = _mm256_alignr_epi8(cmpEq, _mm256_inserti128_si256(
						_mm256_setzero_si256(), _mm256_castsi256_si128(cmpEq), 1
					), 15);
#else
					cmpEq = _mm256_alignr_epi8(cmpEq, _mm256_permute2x128_si256(cmpEq, cmpEq, 0x08), 15);
#endif
					oData = _mm256_add_epi8(
						oData,
						_mm256_and_si256(
							cmpEq,
							_mm256_set1_epi8(-64)
						)
					);
				}
			}
			
			// handle \r\n. sequences
			// RFC3977 requires the first dot on a line to be stripped, due to dot-stuffing
			if(checkNewlines) {
				__m256i tmpData2 = _mm256_loadu_si256((__m256i *)(src+i+2));
				__m256i match2Eq, match3Y, match2Dot;
				if(searchEnd) {
					match2Eq = _mm256_cmpeq_epi8(_mm256_set1_epi8('='), tmpData2);
					match3Y  = _mm256_cmpeq_epi8(
						_mm256_set1_epi8('y'),
						_mm256_loadu_si256((__m256i *)(src+i+3))
					);
				}
				if(isRaw)
					match2Dot = _mm256_cmpeq_epi8(tmpData2, _mm256_set1_epi8('.'));
				
				// find patterns of \r_.
				if(isRaw && LIKELIHOOD(0.002, !_mm256_testz_si256(cmpCr, match2Dot))) {
					// merge matches for \r\n.
					__m256i match1Lf = _mm256_cmpeq_epi8(
						_mm256_set1_epi8('\n'),
						_mm256_loadu_si256((__m256i *)(src+i+1))
					);
					__m256i match2NlDot, match1Nl;
#ifdef __AVX512VL__
					if(use_isa >= ISA_LEVEL_AVX3)
						match2NlDot = _mm256_ternarylogic_epi32(match2Dot, match1Lf, cmpCr, 0x80);
					else
#endif
					{
						match1Nl = _mm256_and_si256(match1Lf, cmpCr);
						match2NlDot = _mm256_and_si256(match2Dot, match1Nl);
					}
					if(searchEnd) {
						__m256i tmpData4 = _mm256_loadu_si256((__m256i *)(src+i+4));
						// match instances of \r\n.\r\n and \r\n.=y
						__m256i match3Cr = _mm256_cmpeq_epi8(
							_mm256_set1_epi8('\r'),
							_mm256_loadu_si256((__m256i *)(src+i+3))
						);
						__m256i match4Lf = _mm256_cmpeq_epi8(tmpData4, _mm256_set1_epi8('\n'));
						__m256i match4EqY = _mm256_cmpeq_epi16(tmpData4, _mm256_set1_epi16(0x793d)); // =y
						
						__m256i matchEnd;
						__m256i match3EqY = _mm256_and_si256(match2Eq, match3Y);
#ifdef __AVX512VL__
						if(use_isa >= ISA_LEVEL_AVX3) {
							// match \r\n=y
							__m256i match3End = _mm256_ternarylogic_epi32(match1Lf, cmpCr, match3EqY, 0x80); // match3EqY & match1Nl
							__m256i match34EqY = _mm256_ternarylogic_epi32(match4EqY, _mm256_srli_epi16(match3EqY, 8), _mm256_set1_epi16(0xff00), 0xEC); // (match4EqY & 0xff00) | (match3EqY >> 8)
							// merge \r\n and =y matches for tmpData4
							__m256i match4End = _mm256_ternarylogic_epi32(match34EqY, match3Cr, match4Lf, 0xF8); // (match3Cr & match4Lf) | match34EqY
							// merge with \r\n. and combine
							matchEnd = _mm256_ternarylogic_epi32(match4End, match2NlDot, match3End, 0xEA); // (match4End & match2NlDot) | match3End
						} else
#endif
						{
							match4EqY = _mm256_slli_epi16(match4EqY, 8); // TODO: also consider using PBLENDVB here with shifted match3EqY instead
							// merge \r\n and =y matches for tmpData4
							__m256i match4End = _mm256_or_si256(
								_mm256_and_si256(match3Cr, match4Lf),
								_mm256_or_si256(match4EqY, _mm256_srli_epi16(match3EqY, 8)) // _mm256_srli_si256 by 1 also works
							);
							// merge with \r\n.
							match4End = _mm256_and_si256(match4End, match2NlDot);
							// match \r\n=y
							__m256i match3End = _mm256_and_si256(match3EqY, match1Nl);
							// combine match sequences
							matchEnd = _mm256_or_si256(match4End, match3End);
						}
						if(LIKELIHOOD(0.002, _mm256_movemask_epi8(matchEnd))) {
							// terminator found
							// there's probably faster ways to do this, but reverting to scalar code should be good enough
							escFirst = oldEscFirst;
							len += i;
							break;
						}
					}
					uint32_t killDots = _mm256_movemask_epi8(match2NlDot);
					mask |= (killDots << 2) & 0xffffffff;
					nextMask = killDots >> (sizeof(__m256i)-2);
				}
				else if(searchEnd) {
					if(LIKELIHOOD(0.002, !_mm256_testz_si256(match2Eq, match3Y))) {
						bool endNotFound;
						__m256i match1Lf = _mm256_cmpeq_epi8(
							_mm256_set1_epi8('\n'),
							_mm256_loadu_si256((__m256i *)(src+i+1))
						);
#ifdef __AVX512VL__
						if(use_isa >= ISA_LEVEL_AVX3)
							endNotFound = _mm256_testz_si256(
								_mm256_ternarylogic_epi32(match3Y, match2Eq, match1Lf, 0x80),
								cmpCr
							);
						else
#endif
							endNotFound = _mm256_testz_si256(
								_mm256_and_si256(match2Eq, match3Y),
								_mm256_and_si256(match1Lf, cmpCr)
							);
						if(!endNotFound) {
							escFirst = oldEscFirst;
							len += i;
							break;
						}
					}
					if(isRaw) nextMask = 0;
				}
				else if(isRaw) // no \r_. found
					nextMask = 0;
			}
			
			// all that's left is to 'compress' the data (skip over masked chars)
# if defined(__AVX512VBMI2__) && defined(__AVX512VL__)
			if(use_isa >= ISA_LEVEL_VBMI2) {
				_mm256_mask_compressstoreu_epi8(p, ~mask, oData);
				p += XMM_SIZE - _mm_popcnt_u32(mask);
			} else
# endif
			{
				// lookup compress masks and shuffle
				__m256i shuf = _mm256_inserti128_si256(
					_mm256_castsi128_si256(_mm_load_si128((__m128i*)(unshufLUTBig + (mask & 0x7fff)))),
					*(__m128i*)((char*)unshufLUTBig + ((mask >> 12) & 0x7fff0)),
					1
				);
				oData = _mm256_shuffle_epi8(oData, shuf);
				
				_mm_storeu_si128((__m128i*)p, _mm256_castsi256_si128(oData));
				// increment output position
				p -= _mm_popcnt_u32(mask & 0xffff);
				
				_mm_storeu_si128((__m128i*)(p + XMM_SIZE), _mm256_extracti128_si256(oData, 1));
				p += XMM_SIZE*2 - _mm_popcnt_u32(mask & 0xffff0000);
				
			}
		} else {
			_mm256_storeu_si256((__m256i*)p, oData);
			p += sizeof(__m256i);
			escFirst = 0;
		}
	}
	_mm256_zeroupper();
	_escFirst = escFirst;
	_nextMask = nextMask;
}
#endif
