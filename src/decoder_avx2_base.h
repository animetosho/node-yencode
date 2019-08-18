
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
	for(long i = -len; i; i += sizeof(__m256i)) {
		__m256i data = _mm256_load_si256((__m256i *)(src+i));
		
		// search for special chars
		__m256i cmpEq = _mm256_cmpeq_epi8(data, _mm256_set1_epi8('='));
		__m256i cmpCr = _mm256_cmpeq_epi8(data, _mm256_set1_epi8('\r'));
		__m256i cmp;
#ifdef __AVX512VL__
		if(use_isa >= ISA_LEVEL_AVX3) {
			cmp = _mm256_ternarylogic_epi32(
				_mm256_cmpeq_epi8(data, lfCompare),
				cmpCr,
				cmpEq,
				0xFE
			);
		} else
#endif
		{
			cmp = _mm256_or_si256(
				_mm256_or_si256(
					_mm256_cmpeq_epi8(data, lfCompare),
					cmpCr
				),
				cmpEq
			);
		}
		
		uint32_t mask = _mm256_movemask_epi8(cmp); // not the most accurate mask if we have invalid sequences; we fix this up later
		data = _mm256_add_epi8(data, yencOffset);
		
		if (LIKELIHOOD(0.42 /*guess*/, mask != 0)) {
			uint32_t maskEq = _mm256_movemask_epi8(cmpEq);
			
			// handle \r\n. sequences
			// RFC3977 requires the first dot on a line to be stripped, due to dot-stuffing
			if((isRaw || searchEnd) && LIKELIHOOD(0.3, mask != maskEq)) {
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
							__m256i match34EqY = _mm256_ternarylogic_epi32(match4EqY, _mm256_srli_epi16(match3EqY, 8), _mm256_set1_epi16(-0xff), 0xEC); // (match4EqY & 0xff00) | (match3EqY >> 8)
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
							len += i;
							break;
						}
					}
					mask |= (_mm256_movemask_epi8(match2NlDot) << 2) & 0xffffffff;
					match2NlDot = _mm256_castsi128_si256(_mm_srli_si128(_mm256_extracti128_si256(match2NlDot, 1), 14));
#ifdef __AVX512VL__
					if(use_isa >= ISA_LEVEL_AVX3)
						lfCompare = _mm256_ternarylogic_epi32(
							match2NlDot, _mm256_set1_epi8('\n'), _mm256_set1_epi8('.'), 0xEC
						);
					else
#endif
					{
						// this bitiwse trick works because '.'|'\n' == '.'
						lfCompare = _mm256_or_si256(
							_mm256_and_si256(match2NlDot, _mm256_set1_epi8('.')),
							_mm256_set1_epi8('\n')
						);
					}
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
				unsigned tmp = eqFixLUT[(maskEq&0xff) & ~escFirst];
				uint32_t maskEq2 = tmp;
				for(int j=8; j<32; j+=8) {
					tmp = eqFixLUT[((maskEq>>j)&0xff) & ~(tmp>>7)];
					maskEq2 |= tmp<<j;
				}
				maskEq = maskEq2;
				
				mask &= ~escFirst;
				escFirst = tmp>>7;
				// next, eliminate anything following a `=` from the special char mask; this eliminates cases of `=\r` so that they aren't removed
				maskEq <<= 1;
				mask &= ~maskEq;
				
				// unescape chars following `=`
#if defined(__AVX512VL__) && defined(__AVX512BW__)
				if(use_isa >= ISA_LEVEL_AVX3) {
					// GCC < 7 seems to generate rubbish assembly for this
					data = _mm256_mask_add_epi8(
						data,
						(__mmask32)maskEq,
						data,
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
					//__m256i addMask = _mm256_i32gather_epi64((long long int*)eqAddLUT, _mm_cvtepu8_epi32(_mm_cvtsi32_si128(maskEq)), 8);
					data = _mm256_add_epi8(data, addMask);
				}
			} else {
				escFirst = (maskEq >> (sizeof(__m256i)-1));
				
#if defined(__AVX512VL__) && defined(__AVX512BW__)
				if(use_isa >= ISA_LEVEL_AVX3) {
					data = _mm256_mask_add_epi8(
						data,
						(__mmask32)(maskEq << 1),
						data,
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
					data = _mm256_add_epi8(
						data,
						_mm256_and_si256(
							cmpEq,
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
				_mm256_mask_compressstoreu_epi8(p, (__mmask32)(~mask), data);
				p += XMM_SIZE*2 - popcnt32(mask);
			} else
# endif
			{
				// lookup compress masks and shuffle
				__m256i shuf = _mm256_inserti128_si256(
					_mm256_castsi128_si256(_mm_load_si128((__m128i*)(unshufLUTBig + (mask & 0x7fff)))),
					*(__m128i*)((char*)unshufLUTBig + ((mask >> 12) & 0x7fff0)),
					1
				);
				data = _mm256_shuffle_epi8(data, shuf);
				
				_mm_storeu_si128((__m128i*)p, _mm256_castsi256_si128(data));
				// increment output position
				p -= popcnt32(mask & 0xffff);
				
				_mm_storeu_si128((__m128i*)(p + XMM_SIZE), _mm256_extracti128_si256(data, 1));
				p -= popcnt32(mask & 0xffff0000);
				p += XMM_SIZE*2;
				
			}
		} else {
			_mm256_storeu_si256((__m256i*)p, data);
			p += sizeof(__m256i);
			escFirst = 0;
			yencOffset = _mm256_set1_epi8(-42);
		}
	}
	_escFirst = (unsigned char)escFirst;
	_nextMask = (uint16_t)_mm256_movemask_epi8(_mm256_cmpeq_epi8(lfCompare, _mm256_set1_epi8('.')));
	_mm256_zeroupper();
}
#endif
