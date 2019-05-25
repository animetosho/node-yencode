
#ifdef __AVX2__

template<bool isRaw, bool searchEnd, enum YEncDecIsaLevel use_isa>
inline void do_decode_avx2(const uint8_t* src, long& len, unsigned char*& p, unsigned char& _escFirst, uint16_t& _nextMask) {
	int escFirst = _escFirst;
	uint32_t nextMask = _nextMask;
	for(long i = -len; i; i += sizeof(__m256i)) {
		__m256i data = _mm256_load_si256((__m256i *)(src+i));
		
		// search for special chars
		__m256i cmpEq = _mm256_cmpeq_epi8(data, _mm256_set1_epi8('='));
		__m256i cmp;
#ifdef __AVX512VL__
		if(use_isa >= ISA_LEVEL_AVX3) {
			cmp = _mm256_ternarylogic_epi32(
				_mm256_cmpeq_epi8(data, _mm256_set1_epi16(0x0a0d)),
				_mm256_cmpeq_epi8(data, _mm256_set1_epi16(0x0d0a)),
				cmpEq,
				0xFE
			);
		} else
#endif
		{
			cmp = _mm256_or_si256(
				_mm256_or_si256(
					_mm256_cmpeq_epi8(data, _mm256_set1_epi16(0x0a0d)), // \r\n
					_mm256_cmpeq_epi8(data, _mm256_set1_epi16(0x0d0a))  // \n\r
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
				// find instances of \r\n
				// load w/ 16 byte overlap (this loads 32 bytes, though we only need 20; it's probably faster to do this than to shuffle stuff around)
				// TODO: do test the idea of load 128 + load 32
				//__m256i nextData = _mm256_permute2x128_si256(data, _mm256_castsi128_si256(_mm_loadu_si32(src+i+32)), 0x21);
				__m256i nextData, tmpData1, tmpData2;
				if(searchEnd) {
					nextData = _mm256_loadu_si256((__m256i *)(src+i+16));
					
					tmpData1 = _mm256_alignr_epi8(nextData, data, 1);
					tmpData2 = _mm256_alignr_epi8(nextData, data, 2);
				} else {
					// these are only referenced once, so the loads can get inlined into the vpcmpeq* instructions
					tmpData1 = _mm256_loadu_si256((__m256i *)(src+i+1));
					tmpData2 = _mm256_loadu_si256((__m256i *)(src+i+2));
				}
				__m256i matchNl1 = _mm256_cmpeq_epi16(data, _mm256_set1_epi16(0x0a0d));
				__m256i matchNl2 = _mm256_cmpeq_epi16(tmpData1, _mm256_set1_epi16(0x0a0d));
				
				__m256i matchNlDots;
				uint32_t killDots;
				if(isRaw) {
					__m256i matchDots = _mm256_cmpeq_epi8(tmpData2, _mm256_set1_epi8('.'));
					// merge preparation (for non-raw, it doesn't matter if this is shifted or not)
					matchNl1 = _mm256_and_si256(matchNl1, _mm256_set1_epi16(0xff));
					
					// merge matches of \r\n with those for .
#ifdef __AVX512VL__
					if(use_isa >= ISA_LEVEL_AVX3)
						matchNlDots = _mm256_ternarylogic_epi32(matchDots, matchNl1, matchNl2, 0xE0);
					else
#endif
						matchNlDots = _mm256_and_si256(matchDots, _mm256_or_si256(matchNl1, matchNl2));
					killDots = _mm256_movemask_epi8(matchNlDots);
					if(!searchEnd) {
						mask |= (killDots << 2) & 0xffffffff;
						nextMask = killDots >> (sizeof(__m256i)-2);
					}
				}
				
				if(searchEnd) {
					__m256i tmpData3 = _mm256_alignr_epi8(nextData, data, 3);
					__m256i cmpB1 = _mm256_cmpeq_epi16(tmpData2, _mm256_set1_epi16(0x793d)); // "=y"
					__m256i cmpB2 = _mm256_cmpeq_epi16(tmpData3, _mm256_set1_epi16(0x793d));
					if(isRaw && LIKELIHOOD(0.002, killDots)) {
						__m256i tmpData4 = _mm256_alignr_epi8(nextData, data, 4);
						// match instances of \r\n.\r\n and \r\n.=y
						__m256i cmpC1 = _mm256_cmpeq_epi16(tmpData3, _mm256_set1_epi16(0x0a0d)); // "\r\n"
						__m256i cmpC2 = _mm256_cmpeq_epi16(tmpData4, _mm256_set1_epi16(0x0a0d));
						__m256i cmpB3 = _mm256_cmpeq_epi16(tmpData4, _mm256_set1_epi16(0x793d));
						cmpC1 = _mm256_or_si256(cmpC1, cmpB2);
						
#ifdef __AVX512VL__
						if(use_isa >= ISA_LEVEL_AVX3) {
							cmpC2 = _mm256_ternarylogic_epi32(cmpC2, cmpB3, _mm256_set1_epi16(0xff), 0x54); // (cmpC2 | cmpB3) & ~0x00ff
							cmpC1 = _mm256_ternarylogic_epi32(cmpC1, cmpC2, matchNlDots, 0xA8); // (cmpC1 | cmpC2) & matchNlDots
							cmpB2 = _mm256_ternarylogic_epi32(cmpB2, matchNl2, cmpC1, 0xEA); // (cmpB2 & matchNl2) | cmpC1
							cmpB1 = _mm256_ternarylogic_epi32(cmpB1, matchNl1, cmpB2, 0xEA); // (cmpB1 & matchNl1) | cmpB2
						} else
#endif
						{
							cmpC2 = _mm256_andnot_si256(_mm256_set1_epi16(0xff), _mm256_or_si256(cmpC2, cmpB3));
							// prepare cmpB
							cmpB1 = _mm256_and_si256(cmpB1, matchNl1);
							cmpB2 = _mm256_and_si256(cmpB2, matchNl2);
							// and w/ dots
							cmpC1 = _mm256_and_si256(_mm256_or_si256(cmpC1, cmpC2), matchNlDots);
							cmpB1 = _mm256_or_si256(cmpC1, _mm256_or_si256(
								cmpB1, cmpB2
							));
						}
						if(LIKELIHOOD(0.002, _mm256_movemask_epi8(cmpB1))) {
							// terminator found
							// there's probably faster ways to do this, but reverting to scalar code should be good enough
							escFirst = oldEscFirst;
							len += i;
							break;
						}
						mask |= (killDots << 2) & 0xffffffff;
						nextMask = killDots >> (sizeof(__m256i)-2);
					} else {
						if(LIKELIHOOD(0.002, _mm256_movemask_epi8(
							_mm256_or_si256(cmpB1, cmpB2)
						))) {
#ifdef __AVX512VL__
							if(use_isa >= ISA_LEVEL_AVX3)
								cmpB1 = _mm256_ternarylogic_epi32(cmpB1, matchNl1, _mm256_and_si256(cmpB2, matchNl2), 0xEA);
							else
#endif
								cmpB1 = _mm256_or_si256(
									_mm256_and_si256(cmpB1, matchNl1),
									_mm256_and_si256(cmpB2, matchNl2)
								);
							if(_mm256_movemask_epi8(cmpB1)) {
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
