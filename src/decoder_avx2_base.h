
#ifdef __AVX2__

template<bool isRaw, bool searchEnd, enum YEncDecIsaLevel use_isa>
HEDLEY_ALWAYS_INLINE void do_decode_avx2(const uint8_t* HEDLEY_RESTRICT src, long& len, unsigned char* HEDLEY_RESTRICT & p, unsigned char& _escFirst, uint16_t& _nextMask) {
	HEDLEY_ASSUME(_escFirst == 0 || _escFirst == 1);
	HEDLEY_ASSUME(_nextMask == 0 || _nextMask == 1 || _nextMask == 2);
	unsigned long escFirst = _escFirst;
	__m256i yencOffset = escFirst ? _mm256_set_epi8(
		-42,-42,-42,-42,-42,-42,-42,-42,-42,-42,-42,-42,-42,-42,-42,-42,
		-42,-42,-42,-42,-42,-42,-42,-42,-42,-42,-42,-42,-42,-42,-42,-42-64
	) : _mm256_set1_epi8(-42);
	__m256i lfCompare = _mm256_set1_epi8('\n');
	if(_nextMask && isRaw) {
		lfCompare = _mm256_set_epi8(
			'\n','\n','\n','\n','\n','\n','\n','\n','\n','\n','\n','\n','\n','\n','\n','\n',
			'\n','\n','\n','\n','\n','\n','\n','\n','\n','\n','\n','\n','\n','\n',_nextMask==2?'.':'\n',_nextMask==1?'.':'\n'
		);
	}
	for(long i = -len; i; i += sizeof(__m256i)*2) {
		__m256i dataA = _mm256_load_si256((__m256i *)(src+i));
		__m256i dataB = _mm256_load_si256((__m256i *)(src+i) + 1);
		
		// search for special chars
		__m256i cmpEqA = _mm256_cmpeq_epi8(dataA, _mm256_set1_epi8('='));
		__m256i cmpEqB = _mm256_cmpeq_epi8(dataB, _mm256_set1_epi8('='));
		__m256i cmpCrA = _mm256_cmpeq_epi8(dataA, _mm256_set1_epi8('\r'));
		__m256i cmpCrB = _mm256_cmpeq_epi8(dataB, _mm256_set1_epi8('\r'));
		__m256i cmpA, cmpB;
#ifdef __AVX512VL__
		if(use_isa >= ISA_LEVEL_AVX3) {
			cmpA = _mm256_ternarylogic_epi32(
				_mm256_cmpeq_epi8(dataA, lfCompare),
				cmpCrA,
				cmpEqA,
				0xFE
			);
			cmpB = _mm256_ternarylogic_epi32(
				_mm256_cmpeq_epi8(dataB, _mm256_set1_epi8('\n')),
				cmpCrB,
				cmpEqB,
				0xFE
			);
		} else
#endif
		{
			cmpA = _mm256_or_si256(
				_mm256_or_si256(
					_mm256_cmpeq_epi8(dataA, lfCompare),
					cmpCrA
				),
				cmpEqA
			);
			cmpB = _mm256_or_si256(
				_mm256_or_si256(
					_mm256_cmpeq_epi8(dataB, _mm256_set1_epi8('\n')),
					cmpCrB
				),
				cmpEqB
			);
		}
		
		// TODO: can OR the vectors together to save generating a mask, but may not be worth it
		uint64_t mask = (uint32_t)_mm256_movemask_epi8(cmpB); // not the most accurate mask if we have invalid sequences; we fix this up later
		mask = (mask << 32) | (uint32_t)_mm256_movemask_epi8(cmpA);
		dataA = _mm256_add_epi8(dataA, yencOffset);
		dataB = _mm256_add_epi8(dataB, _mm256_set1_epi8(-42));
		
		if (mask != 0) {
			uint64_t maskEq = (uint32_t)_mm256_movemask_epi8(cmpEqB);
			maskEq = (maskEq << 32) | (uint32_t)_mm256_movemask_epi8(cmpEqA);
			
			// handle \r\n. sequences
			// RFC3977 requires the first dot on a line to be stripped, due to dot-stuffing
			if((isRaw || searchEnd) && LIKELIHOOD(0.3, mask != maskEq)) {
				__m256i tmpData2A = _mm256_loadu_si256((__m256i *)(src+i+2));
				__m256i tmpData2B = _mm256_loadu_si256((__m256i *)(src+i+2) + 1);
				__m256i match2EqA, match2DotA;
				__m256i match2EqB, match2DotB;
#if defined(__AVX512VL__) && defined(__AVX512BW__)
				__mmask32 match2EqMaskA, match2EqMaskB;
				if(use_isa >= ISA_LEVEL_AVX3 && searchEnd) {
					match2EqMaskA = _mm256_cmpeq_epi8_mask(_mm256_set1_epi8('='), tmpData2A);
					match2EqMaskB = _mm256_cmpeq_epi8_mask(_mm256_set1_epi8('='), tmpData2B);
				} else
#endif
				if(searchEnd) {
					match2EqA = _mm256_cmpeq_epi8(_mm256_set1_epi8('='), tmpData2A);
					match2EqB = _mm256_cmpeq_epi8(_mm256_set1_epi8('='), tmpData2B);
				}
				
				__m256i partialKillDotFound;
				if(isRaw) {
					match2DotA = _mm256_cmpeq_epi8(tmpData2A, _mm256_set1_epi8('.'));
					match2DotB = _mm256_cmpeq_epi8(tmpData2B, _mm256_set1_epi8('.'));
					// find patterns of \r_.
					
#ifdef __AVX512VL__
					if(use_isa >= ISA_LEVEL_AVX3)
						partialKillDotFound = _mm256_ternarylogic_epi32(
							_mm256_and_si256(cmpCrA, match2DotA),
							cmpCrB, match2DotB,
							0xf8
						);
					else
#endif
						partialKillDotFound = _mm256_or_si256(
							_mm256_and_si256(cmpCrA, match2DotA),
							_mm256_and_si256(cmpCrB, match2DotB)
						);
				}
				
				if(isRaw && LIKELIHOOD(0.002, _mm256_movemask_epi8(partialKillDotFound))) {
					// merge matches for \r\n.
					__m256i match1LfA = _mm256_cmpeq_epi8(
						_mm256_set1_epi8('\n'),
						_mm256_loadu_si256((__m256i *)(src+i+1))
					);
					__m256i match1LfB = _mm256_cmpeq_epi8(
						_mm256_set1_epi8('\n'),
						_mm256_loadu_si256((__m256i *)(src+i+1) + 1)
					);
					__m256i match2NlDotA, match1NlA;
					__m256i match2NlDotB, match1NlB;
#ifdef __AVX512VL__
					if(use_isa >= ISA_LEVEL_AVX3) {
						match2NlDotA = _mm256_ternarylogic_epi32(match2DotA, match1LfA, cmpCrA, 0x80);
						match2NlDotB = _mm256_ternarylogic_epi32(match2DotB, match1LfB, cmpCrB, 0x80);
					} else
#endif
					{
						match1NlA = _mm256_and_si256(match1LfA, cmpCrA);
						match1NlB = _mm256_and_si256(match1LfB, cmpCrB);
						match2NlDotA = _mm256_and_si256(
							_mm256_cmpeq_epi8(_mm256_set1_epi8('.'), _mm256_loadu_si256((__m256i *)(src+i+2))), // match2DotA
							match1NlA
						);
						match2NlDotB = _mm256_and_si256(
							_mm256_cmpeq_epi8(_mm256_set1_epi8('.'), _mm256_loadu_si256((__m256i *)(src+i+2) + 1)), // match2DotB
							match1NlB
						);
					}
					if(searchEnd) {
						__m256i tmpData4A = _mm256_loadu_si256((__m256i *)(src+i+4));
						__m256i tmpData4B = _mm256_loadu_si256((__m256i *)(src+i+4) + 1);
						// match instances of \r\n.\r\n and \r\n.=y
						__m256i match3CrA = _mm256_cmpeq_epi8(
							_mm256_set1_epi8('\r'),
							_mm256_loadu_si256((__m256i *)(src+i+3))
						);
						__m256i match3CrB = _mm256_cmpeq_epi8(
							_mm256_set1_epi8('\r'),
							_mm256_loadu_si256((__m256i *)(src+i+3) + 1)
						);
						__m256i match4LfA = _mm256_cmpeq_epi8(tmpData4A, _mm256_set1_epi8('\n'));
						__m256i match4LfB = _mm256_cmpeq_epi8(tmpData4B, _mm256_set1_epi8('\n'));
						__m256i match4EqYA = _mm256_cmpeq_epi16(tmpData4A, _mm256_set1_epi16(0x793d)); // =y
						__m256i match4EqYB = _mm256_cmpeq_epi16(tmpData4B, _mm256_set1_epi16(0x793d)); // =y
						
						__m256i matchEnd;
#if defined(__AVX512VL__) && defined(__AVX512BW__)
						if(use_isa >= ISA_LEVEL_AVX3) {
							__mmask32 match3EqYMaskA = _mm256_mask_cmpeq_epi8_mask(
								match2EqMaskA,
								_mm256_set1_epi8('y'),
								_mm256_loadu_si256((__m256i *)(src+i+3))
							);
							__mmask32 match3EqYMaskB = _mm256_mask_cmpeq_epi8_mask(
								match2EqMaskB,
								_mm256_set1_epi8('y'),
								_mm256_loadu_si256((__m256i *)(src+i+3) + 1)
							);
							// match \r\n=y
							__m256i match3EndA = _mm256_maskz_min_epu8(match3EqYMaskA, match1LfA, cmpCrA); // match3EqY & match1Nl
							__m256i match3EndB = _mm256_maskz_min_epu8(match3EqYMaskB, match1LfB, cmpCrB);
							__m256i match34EqYA, match34EqYB;
# ifdef __AVX512VBMI2__
							if(use_isa >= ISA_LEVEL_VBMI2) {
								match34EqYA = _mm256_shrdi_epi16(_mm256_movm_epi8(match3EqYMaskA), match4EqYA, 8);
								match34EqYB = _mm256_shrdi_epi16(_mm256_movm_epi8(match3EqYMaskB), match4EqYB, 8);
							} else
# endif
							{
								// (match4EqY & 0xff00) | (match3EqY >> 8)
								match34EqYA = _mm256_mask_blend_epi8(match3EqYMaskA>>1, _mm256_and_si256(match4EqYA, _mm256_set1_epi16(-0xff)), _mm256_set1_epi8(-1));
								match34EqYB = _mm256_mask_blend_epi8(match3EqYMaskB>>1, _mm256_and_si256(match4EqYB, _mm256_set1_epi16(-0xff)), _mm256_set1_epi8(-1));
							}
							// merge \r\n and =y matches for tmpData4
							__m256i match4EndA = _mm256_ternarylogic_epi32(match34EqYA, match3CrA, match4LfA, 0xF8); // (match3Cr & match4Lf) | match34EqY
							__m256i match4EndB = _mm256_ternarylogic_epi32(match34EqYB, match3CrB, match4LfB, 0xF8);
							// merge with \r\n. and combine
							matchEnd = _mm256_or_si256(
								_mm256_ternarylogic_epi32(match4EndA, match2NlDotA, match3EndA, 0xEA), // (match4End & match2NlDot) | match3End
								_mm256_ternarylogic_epi32(match4EndB, match2NlDotB, match3EndB, 0xEA)
							);
						} else
#endif
						{
							__m256i match3EqYA = _mm256_and_si256(match2EqA, _mm256_cmpeq_epi8(
								_mm256_set1_epi8('y'),
								_mm256_loadu_si256((__m256i *)(src+i+3))
							));
							__m256i match3EqYB = _mm256_and_si256(match2EqB, _mm256_cmpeq_epi8(
								_mm256_set1_epi8('y'),
								_mm256_loadu_si256((__m256i *)(src+i+3) + 1)
							));
							match4EqYA = _mm256_slli_epi16(match4EqYA, 8); // TODO: also consider using PBLENDVB here with shifted match3EqY instead
							match4EqYB = _mm256_slli_epi16(match4EqYB, 8);
							// merge \r\n and =y matches for tmpData4
							__m256i match4EndA = _mm256_or_si256(
								_mm256_and_si256(match3CrA, match4LfA),
								_mm256_or_si256(match4EqYA, _mm256_srli_epi16(match3EqYA, 8)) // _mm256_srli_si256 by 1 also works
							);
							__m256i match4EndB = _mm256_or_si256(
								_mm256_and_si256(match3CrB, match4LfB),
								_mm256_or_si256(match4EqYB, _mm256_srli_epi16(match3EqYB, 8))
							);
							// merge with \r\n.
							match4EndA = _mm256_and_si256(match4EndA, match2NlDotA);
							match4EndB = _mm256_and_si256(match4EndB, match2NlDotB);
							// match \r\n=y
							__m256i match3EndA = _mm256_and_si256(match3EqYA, match1NlA);
							__m256i match3EndB = _mm256_and_si256(match3EqYB, match1NlB);
							// combine match sequences
							matchEnd = _mm256_or_si256(
								_mm256_or_si256(match4EndA, match3EndA),
								_mm256_or_si256(match4EndB, match3EndB)
							);
						}
						if(LIKELIHOOD(0.002, _mm256_movemask_epi8(matchEnd))) {
							// terminator found
							// there's probably faster ways to do this, but reverting to scalar code should be good enough
							len += i;
							break;
						}
					}
					mask |= (uint64_t)((uint32_t)_mm256_movemask_epi8(match2NlDotA)) << 2;
					mask |= (uint64_t)((uint32_t)_mm256_movemask_epi8(match2NlDotB)) << 34;
					match2NlDotB = _mm256_castsi128_si256(_mm_srli_si128(_mm256_extracti128_si256(match2NlDotB, 1), 14));
#ifdef __AVX512VL__
					if(use_isa >= ISA_LEVEL_AVX3)
						lfCompare = _mm256_ternarylogic_epi32(
							match2NlDotB, _mm256_set1_epi8('\n'), _mm256_set1_epi8('.'), 0xEC
						);
					else
#endif
					{
						// this bitiwse trick works because '.'|'\n' == '.'
						lfCompare = _mm256_or_si256(
							_mm256_and_si256(match2NlDotB, _mm256_set1_epi8('.')),
							_mm256_set1_epi8('\n')
						);
					}
				}
				else if(searchEnd) {
					bool partialEndFound;
					__m256i match3EqYA, match3EqYB;
#if defined(__AVX512VL__) && defined(__AVX512BW__)
					__mmask32 match3EqYMaskA, match3EqYMaskB;
					if(use_isa >= ISA_LEVEL_AVX3) {
						match3EqYMaskA = _mm256_mask_cmpeq_epi8_mask(
							match2EqMaskA,
							_mm256_set1_epi8('y'),
							_mm256_loadu_si256((__m256i *)(src+i+3))
						);
						match3EqYMaskB = _mm256_mask_cmpeq_epi8_mask(
							match2EqMaskB,
							_mm256_set1_epi8('y'),
							_mm256_loadu_si256((__m256i *)(src+i+3) + 1)
						);
# if defined(__GNUC__) && __GNUC__ >= 7
						// GCC (ver 6-10(dev)) fails to optimize pure C version, but has this intrinsic; Clang >= 7 optimizes C version fine
						partialEndFound = !_kortestz_mask32_u8(match3EqYMaskA, match3EqYMaskB);
# else
						partialEndFound = !(match3EqYMaskA | match3EqYMaskB);
# endif
					} else
#endif
					{
						__m256i match3YA = _mm256_cmpeq_epi8(
							_mm256_set1_epi8('y'),
							_mm256_loadu_si256((__m256i *)(src+i+3))
						);
						__m256i match3YB = _mm256_cmpeq_epi8(
							_mm256_set1_epi8('y'),
							_mm256_loadu_si256((__m256i *)(src+i+3) + 1)
						);
						match3EqYA = _mm256_and_si256(match2EqA, match3YA);
						match3EqYB = _mm256_and_si256(match2EqB, match3YB);
						partialEndFound = _mm256_movemask_epi8(_mm256_or_si256(match3EqYA, match3EqYB));
					}
					if(LIKELIHOOD(0.002, partialEndFound)) {
						bool endFound;
#if defined(__AVX512VL__) && defined(__AVX512BW__)
						if(use_isa >= ISA_LEVEL_AVX3) {
							__mmask32 match3LfEqYMaskA = _mm256_mask_cmpeq_epi8_mask(
								match3EqYMaskA,
								_mm256_set1_epi8('\n'),
								_mm256_loadu_si256((__m256i *)(src+i+1))
							);
							__mmask32 match3LfEqYMaskB = _mm256_mask_cmpeq_epi8_mask(
								match3EqYMaskB,
								_mm256_set1_epi8('\n'),
								_mm256_loadu_si256((__m256i *)(src+i+1) + 1)
							);
							
# if defined(__GNUC__) && __GNUC__ >= 7
							endFound = !_kortestz_mask32_u8(
								_mm256_mask_test_epi8_mask(match3LfEqYMaskA, cmpCrA, cmpCrA),
								_mm256_mask_test_epi8_mask(match3LfEqYMaskB, cmpCrB, cmpCrB)
							);
# else
							endFound = !(
								_mm256_mask_test_epi8_mask(match3LfEqYMaskA, cmpCrA, cmpCrA) |
								_mm256_mask_test_epi8_mask(match3LfEqYMaskB, cmpCrB, cmpCrB)
							);
# endif
						} else
#endif
						{
							__m256i match1LfA = _mm256_cmpeq_epi8(
								_mm256_set1_epi8('\n'),
								_mm256_loadu_si256((__m256i *)(src+i+1))
							);
							__m256i match1LfB = _mm256_cmpeq_epi8(
								_mm256_set1_epi8('\n'),
								_mm256_loadu_si256((__m256i *)(src+i+1) + 1)
							);
							endFound = _mm256_movemask_epi8(_mm256_or_si256(
								_mm256_and_si256(
									match3EqYA,
									_mm256_and_si256(match1LfA, cmpCrA)
								),
								_mm256_and_si256(
									match3EqYB,
									_mm256_and_si256(match1LfB, cmpCrB)
								)
							));
						}
						if(endFound) {
							len += i;
							break;
						}
					}
					if(isRaw) lfCompare = _mm256_set1_epi8('\n');
				}
				else if(isRaw) // no \r_. found
					lfCompare = _mm256_set1_epi8('\n');
			}
			
			if(LIKELIHOOD(0.0001, (mask & ((maskEq << 1) + escFirst)) != 0)) {
				unsigned long tmp = eqFixLUT[(maskEq&0xff) & ~(uint64_t)escFirst];
				uint64_t maskEq2 = tmp;
				for(int j=8; j<64; j+=8) {
					tmp = eqFixLUT[((maskEq>>j)&0xff) & ~(uint64_t)(tmp>>7)];
					maskEq2 |= (uint64_t)tmp<<j;
				}
				maskEq = maskEq2;
				
				mask &= ~(uint64_t)escFirst;
				escFirst = tmp>>7;
				// next, eliminate anything following a `=` from the special char mask; this eliminates cases of `=\r` so that they aren't removed
				maskEq <<= 1;
				mask &= ~maskEq;
				
				// unescape chars following `=`
#if defined(__AVX512VL__) && defined(__AVX512BW__)
				if(use_isa >= ISA_LEVEL_AVX3) {
					// GCC < 7 seems to generate rubbish assembly for this
					dataA = _mm256_mask_add_epi8(
						dataA,
						(__mmask32)maskEq,
						dataA,
						_mm256_set1_epi8(-64)
					);
					dataB = _mm256_mask_add_epi8(
						dataB,
						(__mmask32)(maskEq>>32),
						dataB,
						_mm256_set1_epi8(-64)
					);
				} else
#endif
				{
					// convert maskEq into vector form (i.e. reverse pmovmskb)
					__m256i vMaskEq = _mm256_broadcastq_epi64(_mm_cvtsi64_si128(maskEq));
					__m256i vMaskEqA = _mm256_shuffle_epi8(vMaskEq, _mm256_set_epi32(
						0x03030303, 0x03030303, 0x02020202, 0x02020202,
						0x01010101, 0x01010101, 0x00000000, 0x00000000
					));
					__m256i vMaskEqB = _mm256_shuffle_epi8(vMaskEq, _mm256_set_epi32(
						0x07070707, 0x07070707, 0x06060606, 0x06060606,
						0x05050505, 0x05050505, 0x04040404, 0x04040404
					));
					vMaskEqA = _mm256_cmpeq_epi8(
						_mm256_and_si256(vMaskEqA, _mm256_set1_epi64x(0x8040201008040201ULL)),
						_mm256_set1_epi64x(0x8040201008040201ULL)
					);
					vMaskEqB = _mm256_cmpeq_epi8(
						_mm256_and_si256(vMaskEqB, _mm256_set1_epi64x(0x8040201008040201ULL)),
						_mm256_set1_epi64x(0x8040201008040201ULL)
					);
					dataA = _mm256_add_epi8(dataA, _mm256_and_si256(vMaskEqA, _mm256_set1_epi8(-64)));
					dataB = _mm256_add_epi8(dataB, _mm256_and_si256(vMaskEqB, _mm256_set1_epi8(-64)));
					
					/*
					dataA = _mm256_add_epi8(dataA, _mm256_i32gather_epi64(
						(long long int*)eqAddLUT, _mm_cvtepu8_epi32(_mm_cvtsi32_si128(maskEq)), 8
					));
					maskEq >>= 32;
					dataB = _mm256_add_epi8(dataB, _mm256_i32gather_epi64(
						(long long int*)eqAddLUT, _mm_cvtepu8_epi32(_mm_cvtsi32_si128(maskEq)), 8
					));
					*/
					
				}
			} else {
				escFirst = (maskEq >> 63);
				
#if defined(__AVX512VL__) && defined(__AVX512BW__)
				if(use_isa >= ISA_LEVEL_AVX3) {
					dataA = _mm256_mask_add_epi8(
						dataA,
						(__mmask32)(maskEq << 1),
						dataA,
						_mm256_set1_epi8(-64)
					);
					dataB = _mm256_mask_add_epi8(
						dataB,
						(__mmask32)(maskEq >> 31),
						dataB,
						_mm256_set1_epi8(-64)
					);
				} else
#endif
				{
					// << 1 byte
					// TODO: may be worth re-loading from source than trying to permute
#if defined(__tune_znver1__) || defined(__tune_bdver4__)
					cmpEqB = _mm256_cmpeq_epi8(_mm256_set1_epi8('='), _mm256_loadu_si256((__m256i *)(src+i-1) + 1));
					cmpEqA = _mm256_alignr_epi8(cmpEqA, _mm256_inserti128_si256(
						_mm256_setzero_si256(), _mm256_castsi256_si128(cmpEqA), 1
					), 15);
#else
					cmpEqB = _mm256_alignr_epi8(cmpEqB, _mm256_permute2x128_si256(cmpEqA, cmpEqB, 0x21), 15);
					cmpEqA = _mm256_alignr_epi8(cmpEqA, _mm256_permute2x128_si256(cmpEqA, cmpEqA, 0x08), 15);
#endif
					dataA = _mm256_add_epi8(
						dataA,
						_mm256_and_si256(
							cmpEqA,
							_mm256_set1_epi8(-64)
						)
					);
					dataB = _mm256_add_epi8(
						dataB,
						_mm256_and_si256(
							cmpEqB,
							_mm256_set1_epi8(-64)
						)
					);
				}
			}
			// subtract 64 from first element if escFirst == 1
#if defined(__AVX512VL__) && defined(__AVX512BW__)
			if(use_isa >= ISA_LEVEL_AVX3) {
				yencOffset = _mm256_mask_add_epi8(_mm256_set1_epi8(-42), (__mmask32)escFirst, _mm256_set1_epi8(-42), _mm256_set1_epi8(-64));
			} else
#endif
			{
				yencOffset = _mm256_xor_si256(_mm256_set1_epi8(-42), _mm256_castsi128_si256(
					_mm_slli_epi16(_mm_cvtsi32_si128(escFirst), 6)
				));
			}
			
			// all that's left is to 'compress' the data (skip over masked chars)
# if defined(__AVX512VBMI2__) && defined(__AVX512VL__)
			if(use_isa >= ISA_LEVEL_VBMI2) {
				_mm256_mask_compressstoreu_epi8(p, (__mmask32)(~mask), dataA);
				p -= popcnt32(mask & 0xffffffff);
				_mm256_mask_compressstoreu_epi8((p + XMM_SIZE*2), (__mmask32)(~(mask>>32)), dataB);
				p += XMM_SIZE*4 - popcnt32(mask >> 32);
			} else
# endif
			{
				// lookup compress masks and shuffle
				__m256i shuf = _mm256_inserti128_si256(
					_mm256_castsi128_si256(_mm_load_si128((__m128i*)(unshufLUTBig + (mask & 0x7fff)))),
					*(__m128i*)((char*)unshufLUTBig + ((mask >> 12) & 0x7fff0)),
					1
				);
				dataA = _mm256_shuffle_epi8(dataA, shuf);
				
				_mm_storeu_si128((__m128i*)p, _mm256_castsi256_si128(dataA));
				// increment output position
				p -= popcnt32(mask & 0xffff);
				
				_mm_storeu_si128((__m128i*)(p + XMM_SIZE), _mm256_extracti128_si256(dataA, 1));
				p -= popcnt32(mask & 0xffff0000);
				
				mask >>= 28;
				shuf = _mm256_inserti128_si256(
					_mm256_castsi128_si256(_mm_load_si128((__m128i*)((char*)unshufLUTBig + (mask & 0x7fff0)))),
					*(__m128i*)((char*)unshufLUTBig + ((mask >> 16) & 0x7fff0)),
					1
				);
				dataB = _mm256_shuffle_epi8(dataB, shuf);
				
				_mm_storeu_si128((__m128i*)(p + XMM_SIZE*2), _mm256_castsi256_si128(dataB));
				p -= popcnt32(mask & 0xffff0);
				
				_mm_storeu_si128((__m128i*)(p + XMM_SIZE*3), _mm256_extracti128_si256(dataB, 1));
				p -= popcnt32(mask >> 20);
				p += XMM_SIZE*4;
			}
		} else {
			_mm256_storeu_si256((__m256i*)p, dataA);
			_mm256_storeu_si256((__m256i*)p + 1, dataB);
			p += sizeof(__m256i)*2;
			escFirst = 0;
			yencOffset = _mm256_set1_epi8(-42);
		}
	}
	_escFirst = (unsigned char)escFirst;
	_nextMask = (uint16_t)_mm256_movemask_epi8(_mm256_cmpeq_epi8(lfCompare, _mm256_set1_epi8('.')));
	_mm256_zeroupper();
}
#endif
