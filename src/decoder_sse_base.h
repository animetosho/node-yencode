
#ifdef __SSE2__

#if defined(__clang__) && __clang_major__ == 6 && __clang_minor__ == 0
// VBMI2 introduced in clang 6.0, but 128-bit functions misnamed there; fixed in clang 7.0, but we'll handle those on 6.0
# define _mm_mask_compressstoreu_epi8 _mm128_mask_compressstoreu_epi8
# define _mm_shrdi_epi16 _mm128_shrdi_epi16
#endif

// GCC (ver 6-10(dev)) fails to optimize pure C version of mask testing, but has this intrinsic; Clang >= 7 optimizes C version fine
#if defined(__GNUC__) && __GNUC__ >= 7
# define KORTEST16(a, b) !_kortestz_mask16_u8((a), (b))
# define KAND16(a, b) _kand_mask16((a), (b))
# define KOR16(a, b) _kor_mask16((a), (b))
#else
# define KORTEST16(a, b) ((a) | (b))
# define KAND16(a, b) ((a) & (b))
# define KOR16(a, b) ((a) | (b))
#endif

static struct {
	const unsigned char BitsSetTable256inv[256];
	
	#pragma pack(16)
	struct { char bytes[16]; } ALIGN_TO(16, compact[32768]);
	#pragma pack()
	
	uint8_t eqFix[256];
	const uint64_t ALIGN_TO(8, eqAdd[256]);
#if defined(__LZCNT__) && defined(__tune_amdfam10__)
	const int8_t ALIGN_TO(16, unshuf_mask_lzc[32*16]);
#else
	const int8_t ALIGN_TO(16, unshuf_mask_bsr[16*16]);
#endif
	
} lookups = {
	/*BitsSetTable256inv*/ {
		#   define B2(n) 8-(n),     7-(n),     7-(n),     6-(n)
		#   define B4(n) B2(n), B2(n+1), B2(n+1), B2(n+2)
		#   define B6(n) B4(n), B4(n+1), B4(n+1), B4(n+2)
		    B6(0), B6(1), B6(1), B6(2)
		#undef B2
		#undef B4
		#undef B6
	},
	/*compact*/ {},
	
	/*eqFix*/ {},
	/*eqAdd*/ {
		#define _X3(n, k) ((((n) & (1<<k)) ? 192ULL : 0ULL) << (k*8))
		#define _X2(n) \
			_X3(n, 0) | _X3(n, 1) | _X3(n, 2) | _X3(n, 3) | _X3(n, 4) | _X3(n, 5) | _X3(n, 6) | _X3(n, 7)
		#define _X(n) _X2(n+0), _X2(n+1), _X2(n+2), _X2(n+3), _X2(n+4), _X2(n+5), _X2(n+6), _X2(n+7), \
			_X2(n+8), _X2(n+9), _X2(n+10), _X2(n+11), _X2(n+12), _X2(n+13), _X2(n+14), _X2(n+15)
		_X(0), _X(16), _X(32), _X(48), _X(64), _X(80), _X(96), _X(112),
		_X(128), _X(144), _X(160), _X(176), _X(192), _X(208), _X(224), _X(240)
		#undef _X3
		#undef _X2
		#undef _X
	},
	
	
	#define _X2(n,k) n>k?-1:0
	#define _X(n) _X2(n,0), _X2(n,1), _X2(n,2), _X2(n,3), _X2(n,4), _X2(n,5), _X2(n,6), _X2(n,7), _X2(n,8), _X2(n,9), _X2(n,10), _X2(n,11), _X2(n,12), _X2(n,13), _X2(n,14), _X2(n,15)
#if defined(__LZCNT__) && defined(__tune_amdfam10__)
	/*unshuf_mask_lzc*/ {
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
	}
#else
	/*unshuf_mask_bsr*/ {
		_X(0), _X(1), _X(2), _X(3), _X(4), _X(5), _X(6), _X(7),
		_X(8), _X(9), _X(10), _X(11), _X(12), _X(13), _X(14), _X(15)
	}
#endif
	#undef _X
	#undef _X2
	
	
};



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
		__m128i mergeMask = _mm_load_si128((__m128i*)lookups.unshuf_mask_lzc + bitIndex);
		mask ^= 0x80000000U>>bitIndex;
#else
		unsigned long _BSR_VAR(bitIndex, mask);
		__m128i mergeMask = _mm_load_si128((__m128i*)lookups.unshuf_mask_bsr + bitIndex);
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
	
#if defined(__SSSE3__) && !defined(__tune_atom__) && !defined(__tune_silvermont__) && !defined(__tune_btver1__)
	const bool _USING_FAST_MATCH = (use_isa >= ISA_LEVEL_SSSE3);
#else
	const bool _USING_FAST_MATCH = false;
#endif
	
	__m128i lfCompare = _mm_set1_epi8('\n');
	__m128i minMask = _mm_set1_epi8('.');
	if(_nextMask && isRaw) {
		if(_USING_FAST_MATCH)
			minMask = _mm_insert_epi16(minMask, _nextMask == 1 ? 0x2e00 : 0x002e, 0);
		else
			lfCompare = _mm_insert_epi16(lfCompare, _nextMask == 1 ? 0x0a2e /*".\n"*/ : 0x2e0a /*"\n."*/, 0);
	}
	long i;
	for(i = -len; i; i += sizeof(__m128i)*2) {
		__m128i oDataA = _mm_load_si128((__m128i *)(src+i));
		__m128i oDataB = _mm_load_si128((__m128i *)(src+i) + 1);
		
		// search for special chars
		__m128i cmpEqA, cmpEqB, cmpCrA, cmpCrB;
		__m128i cmpA, cmpB;
#if defined(__SSSE3__)
		if(_USING_FAST_MATCH) {
			cmpA = _mm_cmpeq_epi8(oDataA, _mm_shuffle_epi8(
				_mm_set_epi8(-1,'=','\r',-1,-1,'\n',-1,-1,-1,-1,-1,-1,-1,-1,-1,'.'),
				_mm_min_epu8(oDataA, minMask)
			));
			cmpB = _mm_cmpeq_epi8(oDataB, _mm_shuffle_epi8(
				_mm_set_epi8(-1,'=','\r',-1,-1,'\n',-1,-1,-1,-1,-1,-1,-1,-1,-1,'.'),
				_mm_min_epu8(oDataB, _mm_set1_epi8('.'))
			));
		} else
#endif
		{
			cmpEqA = _mm_cmpeq_epi8(oDataA, _mm_set1_epi8('='));
			cmpEqB = _mm_cmpeq_epi8(oDataB, _mm_set1_epi8('='));
			cmpCrA = _mm_cmpeq_epi8(oDataA, _mm_set1_epi8('\r'));
			cmpCrB = _mm_cmpeq_epi8(oDataB, _mm_set1_epi8('\r'));
			cmpA = _mm_or_si128(
				_mm_or_si128(
					_mm_cmpeq_epi8(oDataA, lfCompare), cmpCrA
				),
				cmpEqA
			);
			cmpB = _mm_or_si128(
				_mm_or_si128(
					_mm_cmpeq_epi8(oDataB, _mm_set1_epi8('\n')), cmpCrB
				),
				cmpEqB
			);
		}
		
		__m128i dataA = _mm_add_epi8(oDataA, yencOffset);
		uint32_t mask = (unsigned)_mm_movemask_epi8(cmpA) | ((unsigned)_mm_movemask_epi8(cmpB) << 16); // not the most accurate mask if we have invalid sequences; we fix this up later
		
		if (LIKELIHOOD(0.42 /* rough guess */, mask != 0)) {
			if(_USING_FAST_MATCH) {
				cmpEqA = _mm_cmpeq_epi8(oDataA, _mm_set1_epi8('='));
				cmpEqB = _mm_cmpeq_epi8(oDataB, _mm_set1_epi8('='));
			}
			
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
			if((isRaw || searchEnd) && LIKELIHOOD(0.25, mask != maskEq)) {
				__m128i tmpData2A = _mm_loadu_si128((__m128i*)(src+i+2));
				__m128i tmpData2B = _mm_loadu_si128((__m128i*)(src+i+2) + 1);
				__m128i match2EqA, match2EqB;
				
#if defined(__AVX512VL__) && defined(__AVX512BW__)
				__mmask16 match2EqMaskA, match2EqMaskB;
				__mmask16 match0CrMaskA, match0CrMaskB;
				__mmask16 match2CrXDtMaskA, match2CrXDtMaskB;
				if(use_isa >= ISA_LEVEL_AVX3 && searchEnd) {
					match2EqMaskA = _mm_cmpeq_epi8_mask(_mm_set1_epi8('='), tmpData2A);
					match2EqMaskB = _mm_cmpeq_epi8_mask(_mm_set1_epi8('='), tmpData2B);
				} else
#endif
				if(searchEnd) {
#if !defined(__tune_btver1__)
					if(use_isa < ISA_LEVEL_SSSE3)
#endif
						match2EqA = _mm_cmpeq_epi8(_mm_set1_epi8('='), tmpData2A);
					match2EqB = _mm_cmpeq_epi8(_mm_set1_epi8('='), tmpData2B);
				}
				int partialKillDotFound;
				__m128i match2CrXDtA, match2CrXDtB;
				if(isRaw) {
#if defined(__AVX512VL__) && defined(__AVX512BW__)
					if(use_isa >= ISA_LEVEL_AVX3) {
						match0CrMaskA = _mm_cmpeq_epi8_mask(oDataA, _mm_set1_epi8('\r'));
						match0CrMaskB = _mm_cmpeq_epi8_mask(oDataB, _mm_set1_epi8('\r'));
						match2CrXDtMaskA = _mm_mask_cmpeq_epi8_mask(match0CrMaskA, tmpData2A, _mm_set1_epi8('.'));
						match2CrXDtMaskB = _mm_mask_cmpeq_epi8_mask(match0CrMaskB, tmpData2B, _mm_set1_epi8('.'));
						partialKillDotFound = KORTEST16(match2CrXDtMaskA, match2CrXDtMaskB);
					} else
#endif
					{
						if(_USING_FAST_MATCH) {
							cmpCrA = _mm_cmpeq_epi8(oDataA, _mm_set1_epi8('\r'));
							cmpCrB = _mm_cmpeq_epi8(oDataB, _mm_set1_epi8('\r'));
						}
						match2CrXDtA = _mm_and_si128(cmpCrA, _mm_cmpeq_epi8(tmpData2A, _mm_set1_epi8('.')));
						match2CrXDtB = _mm_and_si128(cmpCrB, _mm_cmpeq_epi8(tmpData2B, _mm_set1_epi8('.')));
						partialKillDotFound = _mm_movemask_epi8(_mm_or_si128(match2CrXDtA, match2CrXDtB));
					}
				}
				
				if(isRaw && LIKELIHOOD(0.001, partialKillDotFound)) {
					__m128i match2NlDotA, match1NlA;
					__m128i match2NlDotB, match1NlB;
					// merge matches for \r\n.
#if defined(__AVX512VL__) && defined(__AVX512BW__)
					__mmask16 match1NlMaskA, match1NlMaskB;
					__mmask16 match2NlDotMaskA, match2NlDotMaskB;
					if(use_isa >= ISA_LEVEL_AVX3) {
						match1NlMaskA = _mm_mask_cmpeq_epi8_mask(
							match0CrMaskA,
							_mm_set1_epi8('\n'),
							_mm_loadu_si128((__m128i *)(src+i+1))
						);
						match1NlMaskB = _mm_mask_cmpeq_epi8_mask(
							match0CrMaskB,
							_mm_set1_epi8('\n'),
							_mm_loadu_si128((__m128i *)(src+i+1) + 1)
						);
						match2NlDotMaskA = KAND16(match2CrXDtMaskA, match1NlMaskA);
						match2NlDotMaskB = KAND16(match2CrXDtMaskB, match1NlMaskB);
					} else
#endif
					{
						__m128i match1LfA = _mm_cmpeq_epi8(_mm_set1_epi8('\n'), _mm_loadu_si128((__m128i*)(src+i+1)));
						__m128i match1LfB = _mm_cmpeq_epi8(_mm_set1_epi8('\n'), _mm_loadu_si128((__m128i*)(src+i+1) + 1));
						
						// always recompute cmpCr to avoid register spills above
#ifdef __GNUC__
						// nudge compiler into thinking that it can't reuse computed values above
						asm("" : "+x"(oDataA), "+x"(oDataB));
#endif
						cmpCrA = _mm_cmpeq_epi8(oDataA, _mm_set1_epi8('\r'));
						cmpCrB = _mm_cmpeq_epi8(oDataB, _mm_set1_epi8('\r'));
						match1NlA = _mm_and_si128(match1LfA, cmpCrA);
						match1NlB = _mm_and_si128(match1LfB, cmpCrB);
						match2NlDotA = _mm_and_si128(match2CrXDtA, match1NlA);
						match2NlDotB = _mm_and_si128(match2CrXDtB, match1NlB);
					}
					if(searchEnd) {
						__m128i tmpData3A = _mm_loadu_si128((__m128i*)(src+i+3));
						__m128i tmpData3B = _mm_loadu_si128((__m128i*)(src+i+3) + 1);
						__m128i tmpData4A = _mm_loadu_si128((__m128i*)(src+i+4));
						__m128i tmpData4B = _mm_loadu_si128((__m128i*)(src+i+4) + 1);
						// match instances of \r\n.\r\n and \r\n.=y
						// TODO: consider doing a PALIGNR using match1Nl for match4NlA
						__m128i match3CrA = _mm_cmpeq_epi8(_mm_set1_epi8('\r'), tmpData3A);
						__m128i match3CrB = _mm_cmpeq_epi8(_mm_set1_epi8('\r'), tmpData3B);
						__m128i match4LfA = _mm_cmpeq_epi8(tmpData4A, _mm_set1_epi8('\n'));
						__m128i match4LfB = _mm_cmpeq_epi8(tmpData4B, _mm_set1_epi8('\n'));
						__m128i match4EqYA = _mm_cmpeq_epi16(tmpData4A, _mm_set1_epi16(0x793d)); // =y
						__m128i match4EqYB = _mm_cmpeq_epi16(tmpData4B, _mm_set1_epi16(0x793d)); // =y
						
						int matchEnd;
#if defined(__AVX512VL__) && defined(__AVX512BW__)
						if(use_isa >= ISA_LEVEL_AVX3) {
							__mmask16 match3EqYMaskA = _mm_mask_cmpeq_epi8_mask(
								match2EqMaskA, _mm_set1_epi8('y'), tmpData3A
							);
							__mmask16 match3EqYMaskB = _mm_mask_cmpeq_epi8_mask(
								match2EqMaskB, _mm_set1_epi8('y'), tmpData3B
							);
							__m128i match34EqYA, match34EqYB;
# ifdef __AVX512VBMI2__
							if(use_isa >= ISA_LEVEL_VBMI2) {
								match34EqYA = _mm_shrdi_epi16(_mm_movm_epi8(match3EqYMaskA), match4EqYA, 8);
								match34EqYB = _mm_shrdi_epi16(_mm_movm_epi8(match3EqYMaskB), match4EqYB, 8);
							} else
# endif
							{
								// (match4EqY & 0xff00) | (match3EqY >> 8)
								match34EqYA = _mm_mask_blend_epi8(match3EqYMaskA>>1, _mm_and_si128(match4EqYA, _mm_set1_epi16(-0xff)), _mm_set1_epi8(-1));
								match34EqYB = _mm_mask_blend_epi8(match3EqYMaskB>>1, _mm_and_si128(match4EqYB, _mm_set1_epi16(-0xff)), _mm_set1_epi8(-1));
							}
							// merge \r\n and =y matches for tmpData4
							__m128i match4EndA = _mm_ternarylogic_epi32(match34EqYA, match3CrA, match4LfA, 0xF8); // (match3Cr & match4Lf) | match34EqY
							__m128i match4EndB = _mm_ternarylogic_epi32(match34EqYB, match3CrB, match4LfB, 0xF8);
							// merge with \r\n. and combine
							matchEnd = KORTEST16(
								KOR16(
									_mm_mask_test_epi8_mask(match2NlDotMaskA, match4EndA, match4EndA),
									KAND16(match3EqYMaskA, match1NlMaskA)
								),
								KOR16(
									_mm_mask_test_epi8_mask(match2NlDotMaskB, match4EndB, match4EndB),
									KAND16(match3EqYMaskB, match1NlMaskB)
								)
							);
						} else
#endif
						{
#if defined(__SSSE3__) && !defined(__tune_btver1__)
							if(use_isa >= ISA_LEVEL_SSSE3)
								match2EqA = _mm_alignr_epi8(cmpEqB, cmpEqA, 2);
#endif
							__m128i match3EqYA = _mm_and_si128(match2EqA, _mm_cmpeq_epi8(_mm_set1_epi8('y'), tmpData3A));
							__m128i match3EqYB = _mm_and_si128(match2EqB, _mm_cmpeq_epi8(_mm_set1_epi8('y'), tmpData3B));
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
							matchEnd = _mm_movemask_epi8(_mm_or_si128(
								_mm_or_si128(match4EndA, match3EndA),
								_mm_or_si128(match4EndB, match3EndB)
							));
						}
						
						if(LIKELIHOOD(0.001, matchEnd)) {
							// terminator found
							// there's probably faster ways to do this, but reverting to scalar code should be good enough
							len += i;
							break;
						}
					}
#if defined(__AVX512VL__) && defined(__AVX512BW__)
					if(use_isa >= ISA_LEVEL_AVX3) {
						mask |= match2NlDotMaskA << 2;
						mask |= (match2NlDotMaskB << 18) & 0xffffffff;
						minMask = _mm_maskz_mov_epi8(~(match2NlDotMaskB>>14), _mm_set1_epi8('.'));
					} else
#endif
					{
						mask |= (_mm_movemask_epi8(match2NlDotA) << 2);
						mask |= (_mm_movemask_epi8(match2NlDotB) << 18) & 0xffffffff;
						match2NlDotB = _mm_srli_si128(match2NlDotB, 14);
						if(_USING_FAST_MATCH)
							minMask = _mm_subs_epu8(_mm_set1_epi8('.'), match2NlDotB);
						else
							// this bitiwse trick works because '.'|'\n' == '.'
							lfCompare = _mm_or_si128(
								_mm_and_si128(match2NlDotB, _mm_set1_epi8('.')),
								_mm_set1_epi8('\n')
							);
					}
				}
				else if(searchEnd) {
					bool partialEndFound;
					__m128i match3EqYA, match3EqYB;
#if defined(__AVX512VL__) && defined(__AVX512BW__)
					__mmask16 match3EqYMaskA, match3EqYMaskB;
					if(use_isa >= ISA_LEVEL_AVX3) {
						match3EqYMaskA = _mm_mask_cmpeq_epi8_mask(
							match2EqMaskA,
							_mm_set1_epi8('y'),
							_mm_loadu_si128((__m128i*)(src+i+3))
						);
						match3EqYMaskB = _mm_mask_cmpeq_epi8_mask(
							match2EqMaskB,
							_mm_set1_epi8('y'),
							_mm_loadu_si128((__m128i*)(src+i+3) + 1)
						);
						partialEndFound = KORTEST16(match3EqYMaskA, match3EqYMaskB);
					} else
#endif
					{
						__m128i match3YA = _mm_cmpeq_epi8(
							_mm_set1_epi8('y'),
							_mm_loadu_si128((__m128i*)(src+i+3))
						);
						__m128i match3YB = _mm_cmpeq_epi8(
							_mm_set1_epi8('y'),
							_mm_loadu_si128((__m128i*)(src+i+3) + 1)
						);
#if defined(__SSSE3__) && !defined(__tune_btver1__)
						if(use_isa >= ISA_LEVEL_SSSE3)
							match2EqA = _mm_alignr_epi8(cmpEqB, cmpEqA, 2);
#endif
						match3EqYA = _mm_and_si128(match2EqA, match3YA);
						match3EqYB = _mm_and_si128(match2EqB, match3YB);
						partialEndFound = _mm_movemask_epi8(_mm_or_si128(match3EqYA, match3EqYB));
					}
					if(LIKELIHOOD(0.001, partialEndFound)) {
						// if the rare case of '=y' is found, do a more precise check
						bool endFound;
						
#if defined(__AVX512VL__) && defined(__AVX512BW__)
						if(use_isa >= ISA_LEVEL_AVX3) {
							__mmask16 match3LfEqYMaskA = _mm_mask_cmpeq_epi8_mask(
								match3EqYMaskA,
								_mm_set1_epi8('\n'),
								_mm_loadu_si128((__m128i*)(src+i+1))
							);
							__mmask16 match3LfEqYMaskB = _mm_mask_cmpeq_epi8_mask(
								match3EqYMaskB,
								_mm_set1_epi8('\n'),
								_mm_loadu_si128((__m128i*)(src+i+1) + 1)
							);
							
							endFound = KORTEST16(
								_mm_mask_cmpeq_epi8_mask(match3LfEqYMaskA, oDataA, _mm_set1_epi8('\r')),
								_mm_mask_cmpeq_epi8_mask(match3LfEqYMaskB, oDataB, _mm_set1_epi8('\r'))
							);
						} else
#endif
						{
							// always recompute cmpCr to avoid register spills above
#ifdef __GNUC__
							asm("" : "+x"(oDataA), "+x"(oDataB));
#endif
							cmpCrA = _mm_cmpeq_epi8(oDataA, _mm_set1_epi8('\r'));
							cmpCrB = _mm_cmpeq_epi8(oDataB, _mm_set1_epi8('\r'));
							__m128i match1LfA = _mm_cmpeq_epi8(
								_mm_set1_epi8('\n'),
								_mm_loadu_si128((__m128i*)(src+i+1))
							);
							__m128i match1LfB = _mm_cmpeq_epi8(
								_mm_set1_epi8('\n'),
								_mm_loadu_si128((__m128i*)(src+i+1) + 1)
							);
							endFound = _mm_movemask_epi8(_mm_or_si128(
								_mm_and_si128(
									match3EqYA,
									_mm_and_si128(match1LfA, cmpCrA)
								),
								_mm_and_si128(
									match3EqYB,
									_mm_and_si128(match1LfB, cmpCrB)
								)
							));
						}
						
						if(endFound) {
							len += i;
							break;
						}
					}
					if(isRaw) {
						if(_USING_FAST_MATCH)
							minMask = _mm_set1_epi8('.');
						else
							lfCompare = _mm_set1_epi8('\n');
					}
				}
				else if(isRaw) { // no \r_. found
					if(_USING_FAST_MATCH)
						minMask = _mm_set1_epi8('.');
					else
						lfCompare = _mm_set1_epi8('\n');
				}
			}
			
			__m128i dataB = _mm_add_epi8(oDataB, _mm_set1_epi8(-42));
			
			if(LIKELIHOOD(0.0001, (mask & ((maskEq << 1) + escFirst)) != 0)) {
				// resolve invalid sequences of = to deal with cases like '===='
				unsigned tmp = lookups.eqFix[(maskEq&0xff) & ~escFirst];
				uint32_t maskEq2 = tmp;
				for(int j=8; j<32; j+=8) {
					tmp = lookups.eqFix[((maskEq>>j)&0xff) & ~(tmp>>7)];
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
							lookups.eqAdd + (maskEq&0xff),
							lookups.eqAdd + ((maskEq>>8)&0xff)
						)
					);
					maskEq >>= 16;
					dataB = _mm_add_epi8(
						dataB,
						LOAD_HALVES(
							lookups.eqAdd + (maskEq&0xff),
							lookups.eqAdd + ((maskEq>>8)&0xff)
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
					_mm_mask_compressstoreu_epi8(p, KNOT16(mask), dataA);
					p -= popcnt32(mask & 0xffff);
					_mm_mask_compressstoreu_epi8(p+XMM_SIZE, KNOT16(mask>>16), dataB);
					p -= popcnt32(mask>>16);
					p += XMM_SIZE*2;
				} else
# endif
				{
					
					dataA = _mm_shuffle_epi8(dataA, _mm_load_si128((__m128i*)(lookups.compact + (mask&0x7fff))));
					STOREU_XMM(p, dataA);
					
					dataB = _mm_shuffle_epi8(dataB, _mm_load_si128((__m128i*)((char*)lookups.compact + ((mask >> 12) & 0x7fff0))));
					
# if defined(__POPCNT__) && !defined(__tune_btver1__)
					if(use_isa >= ISA_LEVEL_AVX) {
						p -= popcnt32(mask & 0xffff);
						STOREU_XMM(p+XMM_SIZE, dataB);
						p -= popcnt32(mask & 0xffff0000);
						p += XMM_SIZE*2;
					} else
# endif
					{
						p += lookups.BitsSetTable256inv[mask & 0xff] + lookups.BitsSetTable256inv[(mask >> 8) & 0xff];
						STOREU_XMM(p, dataB);
						mask >>= 16;
						p += lookups.BitsSetTable256inv[mask & 0xff] + lookups.BitsSetTable256inv[(mask >> 8) & 0xff];
					}
				}
			} else
#endif
			{
				dataA = sse2_compact_vect(mask & 0xffff, dataA);
				STOREU_XMM(p, dataA);
				p += lookups.BitsSetTable256inv[mask & 0xff] + lookups.BitsSetTable256inv[(mask >> 8) & 0xff];
				mask >>= 16;
				dataB = sse2_compact_vect(mask, dataB);
				STOREU_XMM(p, dataB);
				p += lookups.BitsSetTable256inv[mask & 0xff] + lookups.BitsSetTable256inv[(mask >> 8) & 0xff];
			}
#undef LOAD_HALVES
		} else {
			__m128i dataB = _mm_add_epi8(oDataB, _mm_set1_epi8(-42));
			
			STOREU_XMM(p, dataA);
			STOREU_XMM(p+XMM_SIZE, dataB);
			p += XMM_SIZE*2;
			escFirst = 0;
			yencOffset = _mm_set1_epi8(-42);
		}
	}
	_escFirst = (unsigned char)escFirst;
	if(isRaw) {
		if(len != 0) { // have to gone through at least one loop cycle
			if(src[i-2] == '\r' && src[i-1] == '\n' && src[i] == '.')
				_nextMask = 1;
			else if(src[i-1] == '\r' && src[i] == '\n' && src[i+1] == '.')
				_nextMask = 2;
			else
				_nextMask = 0;
		}
	} else
		_nextMask = 0;
}
#endif
