
#ifdef __SSE2__

#define _X3(n, k) ((((n) & (1<<k)) ? 192ULL : 0ULL) << (k*8))
#define _X2(n) \
	_X3(n, 0) | _X3(n, 1) | _X3(n, 2) | _X3(n, 3) | _X3(n, 4) | _X3(n, 5) | _X3(n, 6) | _X3(n, 7)
#define _X(n) _X2(n+0), _X2(n+1), _X2(n+2), _X2(n+3), _X2(n+4), _X2(n+5), _X2(n+6), _X2(n+7), \
	_X2(n+8), _X2(n+9), _X2(n+10), _X2(n+11), _X2(n+12), _X2(n+13), _X2(n+14), _X2(n+15)
static uint64_t ALIGN_TO(8, eqAddLUT[256]) = {
	_X(0), _X(16), _X(32), _X(48), _X(64), _X(80), _X(96), _X(112),
	_X(128), _X(144), _X(160), _X(176), _X(192), _X(208), _X(224), _X(240)
};
#undef _X3
#undef _X2
#undef _X


#define _X2(n,k) n>k?-1:0
#define _X(n) _X2(n,0), _X2(n,1), _X2(n,2), _X2(n,3), _X2(n,4), _X2(n,5), _X2(n,6), _X2(n,7), _X2(n,8), _X2(n,9), _X2(n,10), _X2(n,11), _X2(n,12), _X2(n,13), _X2(n,14), _X2(n,15)

#if defined(__LZCNT__) && defined(__tune_amdfam10__)
static const int8_t ALIGN_TO(16, _unshuf_mask_lzc_table[512]) = {
	// 256 bytes of padding; this allows us to save doing a subtraction in-loop
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	
	_X(15), _X(14), _X(13), _X(12), _X(11), _X(10), _X(9), _X(8),
	_X(7), _X(6), _X(5), _X(4), _X(3), _X(2), _X(1), _X(0)
};
static const __m128i* unshuf_mask_lzc_table = (const __m128i*)_unshuf_mask_lzc_table;
#else
static const int8_t ALIGN_TO(16, _unshuf_mask_bsr_table[256]) = {
	_X(0), _X(1), _X(2), _X(3), _X(4), _X(5), _X(6), _X(7),
	_X(8), _X(9), _X(10), _X(11), _X(12), _X(13), _X(14), _X(15)
};
static const __m128i* unshuf_mask_bsr_table = (const __m128i*)_unshuf_mask_bsr_table;
#endif

#undef _X
#undef _X2

// for LZCNT/BSR
#ifdef _MSC_VER
# include <intrin.h>
# include <ammintrin.h>
# define _BSR_VAR(var, src) var; _BitScanReverse(&var, src)
#elif defined(__GNUC__)
// have seen Clang not like _bit_scan_reverse
# include <x86intrin.h> // for lzcnt
# define _BSR_VAR(var, src) var = (31^__builtin_clz(src))
#else
# include <x86intrin.h>
# define _BSR_VAR(var, src) var = _bit_scan_reverse(src)
#endif

static HEDLEY_ALWAYS_INLINE __m128i sse2_compact_vect(uint32_t mask, __m128i data) {
	while(mask) {
#if defined(__LZCNT__) && defined(__tune_amdfam10__)
		// lzcnt is always at least as fast as bsr, so prefer it if it's available
		unsigned long bitIndex = _lzcnt_u32(mask);
		__m128i mergeMask = _mm_load_si128(unshuf_mask_lzc_table + bitIndex);
		mask ^= 0x80000000U>>bitIndex;
#else
		unsigned long _BSR_VAR(bitIndex, mask);
		__m128i mergeMask = _mm_load_si128(unshuf_mask_bsr_table + bitIndex);
		mask ^= 1<<bitIndex;
#endif
		data = _mm_or_si128(
			_mm_and_si128(mergeMask, data),
			_mm_andnot_si128(mergeMask, _mm_srli_si128(data, 1))
		);
	}
	return data;
}

template<bool isRaw, bool searchEnd, enum YEncDecIsaLevel use_isa>
HEDLEY_ALWAYS_INLINE void do_decode_sse(const uint8_t* HEDLEY_RESTRICT src, long& len, unsigned char* HEDLEY_RESTRICT & p, unsigned char& _escFirst, uint16_t& _nextMask) {
	HEDLEY_ASSUME(_escFirst == 0 || _escFirst == 1);
	HEDLEY_ASSUME(_nextMask == 0 || _nextMask == 1 || _nextMask == 2);
	unsigned long escFirst = _escFirst;
	__m128i yencOffset = escFirst ? _mm_set_epi8(
		-42,-42,-42,-42,-42,-42,-42,-42,-42,-42,-42,-42,-42,-42,-42,-42-64
	) : _mm_set1_epi8(-42);
	__m128i lfCompare = _mm_set1_epi8('\n');
	if(_nextMask && isRaw) {
		lfCompare = _mm_insert_epi16(lfCompare, _nextMask == 1 ? 0x0a2e /*".\n"*/ : 0x2e0a /*"\n."*/, 0);
	}
	for(long i = -len; i; i += sizeof(__m128i)*2) {
		__m128i dataA = _mm_load_si128((__m128i *)(src+i));
		__m128i dataB = _mm_load_si128((__m128i *)(src+i) + 1);
		
		// search for special chars
		__m128i cmpEqA = _mm_cmpeq_epi8(dataA, _mm_set1_epi8('='));
		__m128i cmpEqB = _mm_cmpeq_epi8(dataB, _mm_set1_epi8('='));
		__m128i cmpCrA = _mm_cmpeq_epi8(dataA, _mm_set1_epi8('\r'));
		__m128i cmpCrB = _mm_cmpeq_epi8(dataB, _mm_set1_epi8('\r'));
		__m128i cmpA, cmpB;
#ifdef __AVX512VL__
		if(use_isa >= ISA_LEVEL_AVX3) {
			cmpA = _mm_ternarylogic_epi32(
				_mm_cmpeq_epi8(dataA, lfCompare),
				cmpCrA,
				cmpEqA,
				0xFE
			);
			cmpB = _mm_ternarylogic_epi32(
				_mm_cmpeq_epi8(dataB, _mm_set1_epi8('\n')),
				cmpCrB,
				cmpEqB,
				0xFE
			);
		} else
#endif
		{
			cmpA = _mm_or_si128(
				_mm_or_si128(
					_mm_cmpeq_epi8(dataA, lfCompare), cmpCrA
				),
				cmpEqA
			);
			cmpB = _mm_or_si128(
				_mm_or_si128(
					_mm_cmpeq_epi8(dataB, _mm_set1_epi8('\n')), cmpCrB
				),
				cmpEqB
			);
		}

		uint32_t mask = (unsigned)_mm_movemask_epi8(cmpA) | ((unsigned)_mm_movemask_epi8(cmpB) << 16); // not the most accurate mask if we have invalid sequences; we fix this up later
		
		dataA = _mm_add_epi8(dataA, yencOffset);
		dataB = _mm_add_epi8(dataB, _mm_set1_epi8(-42));
		
		if (LIKELIHOOD(0.42 /* rough guess */, mask != 0)) {
			
#define LOAD_HALVES(a, b) _mm_castps_si128(_mm_loadh_pi( \
	_mm_castsi128_ps(_mm_loadl_epi64((__m128i*)(a))), \
	(__m64*)(b) \
))
			
			// a spec compliant encoder should never generate sequences: ==, =\n and =\r, but we'll handle them to be spec compliant
			// the yEnc specification requires any character following = to be unescaped, not skipped over, so we'll deal with that
			// firstly, check for invalid sequences of = (we assume that these are rare, as a spec compliant yEnc encoder should not generate these)
			uint32_t maskEq = (unsigned)_mm_movemask_epi8(cmpEqA) | ((unsigned)_mm_movemask_epi8(cmpEqB) << 16);
			
			// handle \r\n. sequences
			// RFC3977 requires the first dot on a line to be stripped, due to dot-stuffing
			if((isRaw || searchEnd) && LIKELIHOOD(0.15, mask != maskEq)) {
				__m128i tmpData2A = _mm_loadu_si128((__m128i*)(src+i+2));
				__m128i tmpData2B = _mm_loadu_si128((__m128i*)(src+i+2) + 1);
				__m128i match2EqA, match2DotA;
				__m128i match2EqB, match2DotB;
				
				if(searchEnd) {
#if defined(__SSSE3__) && !defined(__tune_btver1__)
					if(!isRaw && use_isa >= ISA_LEVEL_SSSE3)
						match2EqA = _mm_alignr_epi8(cmpEqB, cmpEqA, 2);
					else
#endif
						match2EqA = _mm_cmpeq_epi8(_mm_set1_epi8('='), tmpData2A);
					match2EqB = _mm_cmpeq_epi8(_mm_set1_epi8('='), tmpData2B);
				}
				__m128i partialKillDotFound;
				if(isRaw) {
					match2DotA = _mm_cmpeq_epi8(tmpData2A, _mm_set1_epi8('.'));
					match2DotB = _mm_cmpeq_epi8(tmpData2B, _mm_set1_epi8('.'));
					// find patterns of \r_.
#ifdef __AVX512VL__
					if(use_isa >= ISA_LEVEL_AVX3)
						partialKillDotFound = _mm_ternarylogic_epi32(
							_mm_and_si128(cmpCrA, match2DotA),
							cmpCrB, match2DotB,
							0xf8
						);
					else
#endif
						partialKillDotFound = _mm_or_si128(
							_mm_and_si128(cmpCrA, match2DotA),
							_mm_and_si128(cmpCrB, match2DotB)
						);
				}
				
				if(isRaw && LIKELIHOOD(0.001, _mm_movemask_epi8(partialKillDotFound))) {
					__m128i match1LfA = _mm_cmpeq_epi8(_mm_set1_epi8('\n'), _mm_loadu_si128((__m128i*)(src+i+1)));
					__m128i match1LfB = _mm_cmpeq_epi8(_mm_set1_epi8('\n'), _mm_loadu_si128((__m128i*)(src+i+1) + 1));
					
					__m128i match2NlDotA, match1NlA;
					__m128i match2NlDotB, match1NlB;
					// merge matches for \r\n.
#ifdef __AVX512VL__
					if(use_isa >= ISA_LEVEL_AVX3) {
						match2NlDotA = _mm_ternarylogic_epi32(match2DotA, match1LfA, cmpCrA, 0x80);
						match2NlDotB = _mm_ternarylogic_epi32(match2DotB, match1LfB, cmpCrB, 0x80);
					} else
#endif
					{
						match1NlA = _mm_and_si128(match1LfA, cmpCrA);
						match1NlB = _mm_and_si128(match1LfB, cmpCrB);
						match2NlDotA = _mm_and_si128(match2DotA, match1NlA);
						match2NlDotB = _mm_and_si128(match2DotB, match1NlB);
					}
					if(searchEnd) {
						__m128i tmpData3A = _mm_loadu_si128((__m128i*)(src+i+3));
						__m128i tmpData3B = _mm_loadu_si128((__m128i*)(src+i+3) + 1);
						__m128i tmpData4A = _mm_loadu_si128((__m128i*)(src+i+4));
						__m128i tmpData4B = _mm_loadu_si128((__m128i*)(src+i+4) + 1);
						// match instances of \r\n.\r\n and \r\n.=y
						__m128i match3CrA = _mm_cmpeq_epi8(_mm_set1_epi8('\r'), tmpData3A);
						__m128i match3CrB = _mm_cmpeq_epi8(_mm_set1_epi8('\r'), tmpData3B);
						__m128i match4LfA = _mm_cmpeq_epi8(tmpData4A, _mm_set1_epi8('\n'));
						__m128i match4LfB = _mm_cmpeq_epi8(tmpData4B, _mm_set1_epi8('\n'));
						__m128i match4EqYA = _mm_cmpeq_epi16(tmpData4A, _mm_set1_epi16(0x793d)); // =y
						__m128i match4EqYB = _mm_cmpeq_epi16(tmpData4B, _mm_set1_epi16(0x793d)); // =y
						
						__m128i matchEnd;
						__m128i match3EqYA = _mm_and_si128(match2EqA, _mm_cmpeq_epi8(_mm_set1_epi8('y'), tmpData3A));
						__m128i match3EqYB = _mm_and_si128(match2EqB, _mm_cmpeq_epi8(_mm_set1_epi8('y'), tmpData3B));
#ifdef __AVX512VL__
						if(use_isa >= ISA_LEVEL_AVX3) {
							// match \r\n=y
							__m128i match3EndA = _mm_ternarylogic_epi32(match1LfA, cmpCrA, match3EqYA, 0x80); // match3EqY & match1Nl
							__m128i match3EndB = _mm_ternarylogic_epi32(match1LfB, cmpCrB, match3EqYB, 0x80);
							__m128i match34EqYA = _mm_ternarylogic_epi32(match4EqYA, _mm_srli_epi16(match3EqYA, 8), _mm_set1_epi16(-256), 0xEC); // (match4EqY & 0xff00) | (match3EqY >> 8)
							__m128i match34EqYB = _mm_ternarylogic_epi32(match4EqYB, _mm_srli_epi16(match3EqYB, 8), _mm_set1_epi16(-256), 0xEC);
							// merge \r\n and =y matches for tmpData4
							__m128i match4EndA = _mm_ternarylogic_epi32(match34EqYA, match3CrA, match4LfA, 0xF8); // (match3Cr & match4Lf) | match34EqY
							__m128i match4EndB = _mm_ternarylogic_epi32(match34EqYB, match3CrB, match4LfB, 0xF8);
							// merge with \r\n. and combine
							matchEnd = _mm_or_si128(
								_mm_ternarylogic_epi32(match4EndA, match2NlDotA, match3EndA, 0xEA), // (match4End & match2NlDot) | match3End
								_mm_ternarylogic_epi32(match4EndB, match2NlDotB, match3EndB, 0xEA)
							);
						} else
#endif
						{
							match4EqYA = _mm_slli_epi16(match4EqYA, 8); // TODO: also consider using PBLENDVB here with shifted match3EqY instead
							match4EqYB = _mm_slli_epi16(match4EqYB, 8);
							// merge \r\n and =y matches for tmpData4
							__m128i match4EndA = _mm_or_si128(
								_mm_and_si128(match3CrA, match4LfA),
								_mm_or_si128(match4EqYA, _mm_srli_epi16(match3EqYA, 8)) // _mm_srli_si128 by 1 also works
							);
							__m128i match4EndB = _mm_or_si128(
								_mm_and_si128(match3CrB, match4LfB),
								_mm_or_si128(match4EqYB, _mm_srli_epi16(match3EqYB, 8)) // _mm_srli_si128 by 1 also works
							);
							// merge with \r\n.
							match4EndA = _mm_and_si128(match4EndA, match2NlDotA);
							match4EndB = _mm_and_si128(match4EndB, match2NlDotB);
							// match \r\n=y
							__m128i match3EndA = _mm_and_si128(match3EqYA, match1NlA);
							__m128i match3EndB = _mm_and_si128(match3EqYB, match1NlB);
							// combine match sequences
							matchEnd = _mm_or_si128(
								_mm_or_si128(match4EndA, match3EndA),
								_mm_or_si128(match4EndB, match3EndB)
							);
						}
						
						if(LIKELIHOOD(0.001, _mm_movemask_epi8(matchEnd))) {
							// terminator found
							// there's probably faster ways to do this, but reverting to scalar code should be good enough
							len += i;
							break;
						}
					}
					mask |= (_mm_movemask_epi8(match2NlDotA) << 2);
					mask |= (_mm_movemask_epi8(match2NlDotB) << 18) & 0xffffffff;
#ifdef __AVX512VL__
					if(use_isa >= ISA_LEVEL_AVX3)
						lfCompare = _mm_ternarylogic_epi32(
							_mm_srli_si128(match2NlDotB, 14), _mm_set1_epi8('\n'), _mm_set1_epi8('.'), 0xEC
						);
					else
#endif
					{
						// this bitiwse trick works because '.'|'\n' == '.'
						lfCompare = _mm_or_si128(
							_mm_and_si128(
								_mm_srli_si128(match2NlDotB, 14),
								_mm_set1_epi8('.')
							),
							_mm_set1_epi8('\n')
						);
					}
				}
				else if(searchEnd) {
					__m128i match3YA = _mm_cmpeq_epi8(
						_mm_set1_epi8('y'),
						_mm_loadu_si128((__m128i*)(src+i+3))
					);
					__m128i match3YB = _mm_cmpeq_epi8(
						_mm_set1_epi8('y'),
						_mm_loadu_si128((__m128i*)(src+i+3) + 1)
					);
					__m128i partialEndFound;
#ifdef __AVX512VL__
					if(use_isa >= ISA_LEVEL_AVX3)
						partialEndFound = _mm_ternarylogic_epi32(
							_mm_and_si128(match2EqA, match3YA),
							match2EqB, match3YB,
							0xf8
						);
					else
#endif
						partialEndFound = _mm_or_si128(
							_mm_and_si128(match2EqA, match3YA),
							_mm_and_si128(match2EqB, match3YB)
						);
					if(LIKELIHOOD(0.001, _mm_movemask_epi8(partialEndFound))) {
						// if the rare case of '=y' is found, do a more precise check
						__m128i endFound;
						__m128i match1LfA = _mm_cmpeq_epi8(
							_mm_set1_epi8('\n'),
							_mm_loadu_si128((__m128i*)(src+i+1))
						);
						__m128i match1LfB = _mm_cmpeq_epi8(
							_mm_set1_epi8('\n'),
							_mm_loadu_si128((__m128i*)(src+i+1) + 1)
						);
						
#ifdef __AVX512VL__
						if(use_isa >= ISA_LEVEL_AVX3)
							endFound = _mm_ternarylogic_epi32(
								_mm_ternarylogic_epi32(match3YB, match2EqB, _mm_and_si128(match1LfB, cmpCrB), 0x80),
								_mm_ternarylogic_epi32(match3YA, match2EqA, match1LfA, 0x80),
								cmpCrA,
								0xf8
							);
						else
#endif
							endFound = _mm_or_si128(
								_mm_and_si128(
									_mm_and_si128(match2EqA, match3YA),
									_mm_and_si128(match1LfA, cmpCrA)
								),
								_mm_and_si128(
									_mm_and_si128(match2EqB, match3YB),
									_mm_and_si128(match1LfB, cmpCrB)
								)
							);
						
						if(_mm_movemask_epi8(endFound)) {
							len += i;
							break;
						}
					}
					if(isRaw) lfCompare = _mm_set1_epi8('\n');
				}
				else if(isRaw) // no \r_. found
					lfCompare = _mm_set1_epi8('\n');
			}
			
			if(LIKELIHOOD(0.0001, (mask & ((maskEq << 1) + escFirst)) != 0)) {
				// resolve invalid sequences of = to deal with cases like '===='
				unsigned tmp = eqFixLUT[(maskEq&0xff) & ~escFirst];
				uint32_t maskEq2 = tmp;
				for(int j=8; j<32; j+=8) {
					tmp = eqFixLUT[((maskEq>>j)&0xff) & ~(tmp>>7)];
					maskEq2 |= tmp<<j;
				}
				maskEq = maskEq2;
				
				mask &= ~escFirst;
				escFirst = (maskEq >> 31);
				// next, eliminate anything following a `=` from the special char mask; this eliminates cases of `=\r` so that they aren't removed
				maskEq <<= 1;
				mask &= ~maskEq;
				
				// unescape chars following `=`
#if defined(__AVX512VL__) && defined(__AVX512BW__)
				if(use_isa >= ISA_LEVEL_AVX3) {
					// GCC < 7 seems to generate rubbish assembly for this
					dataA = _mm_mask_add_epi8(
						dataA,
						(__mmask16)maskEq,
						dataA,
						_mm_set1_epi8(-64)
					);
					dataB = _mm_mask_add_epi8(
						dataB,
						(__mmask16)(maskEq>>16),
						dataB,
						_mm_set1_epi8(-64)
					);
				} else
#endif
				{
					dataA = _mm_add_epi8(
						dataA,
						LOAD_HALVES(
							eqAddLUT + (maskEq&0xff),
							eqAddLUT + ((maskEq>>8)&0xff)
						)
					);
					maskEq >>= 16;
					dataB = _mm_add_epi8(
						dataB,
						LOAD_HALVES(
							eqAddLUT + (maskEq&0xff),
							eqAddLUT + ((maskEq>>8)&0xff)
						)
					);
				}
			} else {
				// no invalid = sequences found - we can cut out some things from above
				// this code path is a shortened version of above; it's here because it's faster, and what we'll be dealing with most of the time
				escFirst = (maskEq >> 31);
				
#if defined(__AVX512VL__) && defined(__AVX512BW__)
				// using mask-add seems to be faster when doing complex checks, slower otherwise, maybe due to higher register pressure?
				if(use_isa >= ISA_LEVEL_AVX3 && (isRaw || searchEnd)) {
					dataA = _mm_mask_add_epi8(dataA, (__mmask16)(maskEq << 1), dataA, _mm_set1_epi8(-64));
					dataB = _mm_mask_add_epi8(dataB, (__mmask16)(maskEq >> 15), dataB, _mm_set1_epi8(-64));
				} else
#endif
				{
					dataA = _mm_add_epi8(
						dataA,
						_mm_and_si128(
							_mm_slli_si128(cmpEqA, 1),
							_mm_set1_epi8(-64)
						)
					);
#if defined(__SSSE3__) && !defined(__tune_btver1__)
					if(use_isa >= ISA_LEVEL_SSSE3)
						cmpEqB = _mm_alignr_epi8(cmpEqB, cmpEqA, 15);
					else
#endif
						cmpEqB = _mm_or_si128(
							_mm_slli_si128(cmpEqB, 1),
							_mm_srli_si128(cmpEqA, 15)
						);
					dataB = _mm_add_epi8(
						dataB,
						_mm_and_si128(cmpEqB, _mm_set1_epi8(-64))
					);
				}
			}
			// subtract 64 from first element if escFirst == 1
#if defined(__AVX512VL__) && defined(__AVX512BW__)
			if(use_isa >= ISA_LEVEL_AVX3) {
				yencOffset = _mm_mask_add_epi8(_mm_set1_epi8(-42), (__mmask16)escFirst, _mm_set1_epi8(-42), _mm_set1_epi8(-64));
			} else
#endif
			{
				yencOffset = _mm_xor_si128(_mm_set1_epi8(-42), 
					_mm_slli_epi16(_mm_cvtsi32_si128(escFirst), 6)
				);
			}
			
			// all that's left is to 'compress' the data (skip over masked chars)
#ifdef __SSSE3__
			if(use_isa >= ISA_LEVEL_SSSE3) {
# if defined(__AVX512VBMI2__) && defined(__AVX512VL__) && defined(__POPCNT__)
				if(use_isa >= ISA_LEVEL_VBMI2) {
#  if defined(__clang__) && __clang_major__ == 6 && __clang_minor__ == 0
					/* VBMI2 introduced in clang 6.0, but misnamed there; presumably will be fixed in 6.1 */
					_mm128_mask_compressstoreu_epi8(p, (__mmask16)(~mask), dataA);
					p -= popcnt32(mask & 0xffff);
					_mm128_mask_compressstoreu_epi8(p+XMM_SIZE, (__mmask16)(~(mask>>16)), dataB);
					
#  else
					_mm_mask_compressstoreu_epi8(p, (__mmask16)(~mask), dataA);
					p -= popcnt32(mask & 0xffff);
					_mm_mask_compressstoreu_epi8(p+XMM_SIZE, (__mmask16)(~(mask>>16)), dataB);
#  endif
					p -= popcnt32(mask>>16);
					p += XMM_SIZE*2;
				} else
# endif
				{
					
					dataA = _mm_shuffle_epi8(dataA, _mm_load_si128((__m128i*)(unshufLUTBig + (mask&0x7fff))));
					STOREU_XMM(p, dataA);
					
					dataB = _mm_shuffle_epi8(dataB, _mm_load_si128((__m128i*)((char*)unshufLUTBig + ((mask >> 12) & 0x7fff0))));
					
# if defined(__POPCNT__) && !defined(__tune_btver1__)
					if(use_isa >= ISA_LEVEL_AVX) {
						p -= popcnt32(mask & 0xffff);
						STOREU_XMM(p+XMM_SIZE, dataB);
						p -= popcnt32(mask & 0xffff0000);
						p += XMM_SIZE*2;
					} else
# endif
					{
						p += BitsSetTable256inv[mask & 0xff] + BitsSetTable256inv[(mask >> 8) & 0xff];
						STOREU_XMM(p, dataB);
						mask >>= 16;
						p += BitsSetTable256inv[mask & 0xff] + BitsSetTable256inv[(mask >> 8) & 0xff];
					}
				}
			} else
#endif
			{
				dataA = sse2_compact_vect(mask & 0xffff, dataA);
				STOREU_XMM(p, dataA);
				p += BitsSetTable256inv[mask & 0xff] + BitsSetTable256inv[(mask >> 8) & 0xff];
				mask >>= 16;
				dataB = sse2_compact_vect(mask, dataB);
				STOREU_XMM(p, dataB);
				p += BitsSetTable256inv[mask & 0xff] + BitsSetTable256inv[(mask >> 8) & 0xff];
			}
#undef LOAD_HALVES
		} else {
			STOREU_XMM(p, dataA);
			STOREU_XMM(p+XMM_SIZE, dataB);
			p += XMM_SIZE*2;
			escFirst = 0;
			yencOffset = _mm_set1_epi8(-42);
		}
	}
	_escFirst = (unsigned char)escFirst;
	_nextMask = (uint16_t)_mm_movemask_epi8(_mm_cmpeq_epi8(lfCompare, _mm_set1_epi8('.')));
}
#endif
