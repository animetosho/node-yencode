#include "common.h"
#ifdef __ARM_NEON

#include "decoder_common.h"

#define _X3(n, k) ((((n) & (1<<k)) ? 150ULL : 214ULL) << (k*8))
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


static bool neon_vect_is_nonzero(uint8x16_t v) {
	return !!(vget_lane_u64(vreinterpret_u64_u32(vqmovn_u64(vreinterpretq_u64_u8(v))), 0));
}


template<bool isRaw, bool searchEnd>
HEDLEY_ALWAYS_INLINE void do_decode_neon(const uint8_t* HEDLEY_RESTRICT src, long& len, unsigned char* HEDLEY_RESTRICT & p, unsigned char& escFirst, uint16_t& nextMask) {
	HEDLEY_ASSUME(escFirst == 0 || escFirst == 1);
	HEDLEY_ASSUME(nextMask == 0 || nextMask == 1 || nextMask == 2);
	uint8x16_t yencOffset = escFirst ? (uint8x16_t){42+64,42,42,42,42,42,42,42,42,42,42,42,42,42,42,42} : vdupq_n_u8(42);
	for(long i = -len; i; i += sizeof(uint8x16_t)*4) {
		uint8x16_t dataA = vld1q_u8(src+i);
		uint8x16_t dataB = vld1q_u8(src+i+sizeof(uint8x16_t));
		uint8x16_t dataC = vld1q_u8(src+i+sizeof(uint8x16_t)*2);
		uint8x16_t dataD = vld1q_u8(src+i+sizeof(uint8x16_t)*3);
		
		// search for special chars
		uint8x16_t cmpEqA = vceqq_u8(dataA, vdupq_n_u8('=')),
		cmpEqB = vceqq_u8(dataB, vdupq_n_u8('=')),
		cmpEqC = vceqq_u8(dataC, vdupq_n_u8('=')),
		cmpEqD = vceqq_u8(dataD, vdupq_n_u8('=')),
		cmpA = vqtbx1q_u8(
			cmpEqA,
			//                                \n      \r
			(uint8x16_t){0,0,0,0,0,0,0,0,0,0,255,0,0,255,0,0},
			dataA
		),
		cmpB = vqtbx1q_u8(
			cmpEqB,
			(uint8x16_t){0,0,0,0,0,0,0,0,0,0,255,0,0,255,0,0},
			dataB
		),
		cmpC = vqtbx1q_u8(
			cmpEqC,
			(uint8x16_t){0,0,0,0,0,0,0,0,0,0,255,0,0,255,0,0},
			dataC
		),
		cmpD = vqtbx1q_u8(
			cmpEqD,
			(uint8x16_t){0,0,0,0,0,0,0,0,0,0,255,0,0,255,0,0},
			dataD
		);
		
		
		if (LIKELIHOOD(0.42 /*guess*/, neon_vect_is_nonzero(vorrq_u8(
			vorrq_u8(cmpA, cmpB),
			vorrq_u8(cmpC, cmpD)
		))) || (isRaw && LIKELIHOOD(0.001, nextMask!=0))) {
			uint8x16_t cmpMerge = vpaddq_u8(
				vpaddq_u8(
					vandq_u8(cmpA, (uint8x16_t){1,2,4,8,16,32,64,128, 1,2,4,8,16,32,64,128}),
					vandq_u8(cmpB, (uint8x16_t){1,2,4,8,16,32,64,128, 1,2,4,8,16,32,64,128})
				),
				vpaddq_u8(
					vandq_u8(cmpC, (uint8x16_t){1,2,4,8,16,32,64,128, 1,2,4,8,16,32,64,128}),
					vandq_u8(cmpD, (uint8x16_t){1,2,4,8,16,32,64,128, 1,2,4,8,16,32,64,128})
				)
			);
			uint8x16_t cmpEqMerge = vpaddq_u8(
				vpaddq_u8(
					vandq_u8(cmpEqA, (uint8x16_t){1,2,4,8,16,32,64,128, 1,2,4,8,16,32,64,128}),
					vandq_u8(cmpEqB, (uint8x16_t){1,2,4,8,16,32,64,128, 1,2,4,8,16,32,64,128})
				),
				vpaddq_u8(
					vandq_u8(cmpEqC, (uint8x16_t){1,2,4,8,16,32,64,128, 1,2,4,8,16,32,64,128}),
					vandq_u8(cmpEqD, (uint8x16_t){1,2,4,8,16,32,64,128, 1,2,4,8,16,32,64,128})
				)
			);
			
			uint8x16_t cmpCombined = vpaddq_u8(cmpMerge, cmpEqMerge);
			uint64_t mask = vgetq_lane_u64(vreinterpretq_u64_u8(cmpCombined), 0);
			uint64_t maskEq = vgetq_lane_u64(vreinterpretq_u64_u8(cmpCombined), 1);
			
			if(isRaw) mask |= nextMask;
			
			// handle \r\n. sequences
			// RFC3977 requires the first dot on a line to be stripped, due to dot-stuffing
			if((isRaw || searchEnd) && LIKELIHOOD(0.15, mask != maskEq)) {
				// vext seems to be a cheap operation on ARM, relative to loads, so only avoid it if there's only one load (isRaw only)
				uint8x16_t tmpData2, nextData;
				if(isRaw && !searchEnd) {
					tmpData2 = vld1q_u8(src+i + 2 + sizeof(uint8x16_t)*3);
				} else {
					nextData = vld1q_u8(src+i + sizeof(uint8x16_t)*4); // only 32-bits needed, but there doesn't appear a nice way to do this via intrinsics: https://stackoverflow.com/questions/46910799/arm-neon-intrinsics-convert-d-64-bit-register-to-low-half-of-q-128-bit-regis
					tmpData2 = vextq_u8(dataD, nextData, 2);
				}
				uint8x16_t cmpCrA = vceqq_u8(dataA, vdupq_n_u8('\r'));
				uint8x16_t cmpCrB = vceqq_u8(dataB, vdupq_n_u8('\r'));
				uint8x16_t cmpCrC = vceqq_u8(dataC, vdupq_n_u8('\r'));
				uint8x16_t cmpCrD = vceqq_u8(dataD, vdupq_n_u8('\r'));
				uint8x16_t match2EqA, match2DotA;
				uint8x16_t match2EqB, match2DotB;
				uint8x16_t match2EqC, match2DotC;
				uint8x16_t match2EqD, match2DotD;
				if(searchEnd) {
					match2EqA = vextq_u8(cmpEqA, cmpEqB, 2);
					match2EqB = vextq_u8(cmpEqB, cmpEqC, 2);
					match2EqC = vextq_u8(cmpEqC, cmpEqD, 2);
					match2EqD = vceqq_u8(tmpData2, vdupq_n_u8('='));
				}
				if(isRaw) {
					match2DotA = vceqq_u8(vextq_u8(dataA, dataB, 2), vdupq_n_u8('.'));
					match2DotB = vceqq_u8(vextq_u8(dataB, dataC, 2), vdupq_n_u8('.'));
					match2DotC = vceqq_u8(vextq_u8(dataC, dataD, 2), vdupq_n_u8('.'));
					match2DotD = vceqq_u8(tmpData2, vdupq_n_u8('.'));
				}
				
				// find patterns of \r_.
				if(isRaw && LIKELIHOOD(0.001, neon_vect_is_nonzero(vorrq_u8(
					vorrq_u8(
						vandq_u8(cmpCrA, match2DotA),
						vandq_u8(cmpCrB, match2DotB)
					), vorrq_u8(
						vandq_u8(cmpCrC, match2DotC),
						vandq_u8(cmpCrD, match2DotD)
					)
				)))) {
					uint8x16_t match1LfA = vceqq_u8(vextq_u8(dataA, dataB, 1), vdupq_n_u8('\n'));
					uint8x16_t match1LfB = vceqq_u8(vextq_u8(dataB, dataC, 1), vdupq_n_u8('\n'));
					uint8x16_t match1LfC = vceqq_u8(vextq_u8(dataC, dataD, 1), vdupq_n_u8('\n'));
					uint8x16_t match1LfD;
					if(searchEnd)
						match1LfD = vceqq_u8(vextq_u8(dataD, nextData, 1), vdupq_n_u8('\n'));
					else
						match1LfD = vceqq_u8(vld1q_u8(src+i + 1+sizeof(uint8x16_t)*3), vdupq_n_u8('\n'));
					uint8x16_t match1NlA = vandq_u8(match1LfA, cmpCrA);
					uint8x16_t match1NlB = vandq_u8(match1LfB, cmpCrB);
					uint8x16_t match1NlC = vandq_u8(match1LfC, cmpCrC);
					uint8x16_t match1NlD = vandq_u8(match1LfD, cmpCrD);
					// merge matches of \r\n with those for .
					uint8x16_t match2NlDotA = vandq_u8(match2DotA, match1NlA);
					uint8x16_t match2NlDotB = vandq_u8(match2DotB, match1NlB);
					uint8x16_t match2NlDotC = vandq_u8(match2DotC, match1NlC);
					uint8x16_t match2NlDotD = vandq_u8(match2DotD, match1NlD);
					if(searchEnd) {
						uint8x16_t tmpData3 = vextq_u8(dataD, nextData, 3);
						uint8x16_t tmpData4 = vextq_u8(dataD, nextData, 4);
						// match instances of \r\n.\r\n and \r\n.=y
						uint8x16_t match3CrA = vextq_u8(cmpCrA, cmpCrB, 3);
						uint8x16_t match3CrB = vextq_u8(cmpCrB, cmpCrC, 3);
						uint8x16_t match3CrC = vextq_u8(cmpCrC, cmpCrD, 3);
						uint8x16_t match3CrD = vceqq_u8(tmpData3, vdupq_n_u8('\r'));
						uint8x16_t match4LfA = vextq_u8(match1LfA, match1LfB, 3);
						uint8x16_t match4LfB = vextq_u8(match1LfB, match1LfC, 3);
						uint8x16_t match4LfC = vextq_u8(match1LfC, match1LfD, 3);
						uint8x16_t match4LfD = vceqq_u8(tmpData4, vdupq_n_u8('\n'));
						uint8x16_t match4EqYA = vreinterpretq_u8_u16(vceqq_u16(vreinterpretq_u16_u8(vextq_u8(dataA, dataB, 4)), vdupq_n_u16(0x793d))); // =y
						uint8x16_t match4EqYB = vreinterpretq_u8_u16(vceqq_u16(vreinterpretq_u16_u8(vextq_u8(dataB, dataC, 4)), vdupq_n_u16(0x793d))); // =y
						uint8x16_t match4EqYC = vreinterpretq_u8_u16(vceqq_u16(vreinterpretq_u16_u8(vextq_u8(dataC, dataD, 4)), vdupq_n_u16(0x793d))); // =y
						uint8x16_t match4EqYD = vreinterpretq_u8_u16(vceqq_u16(vreinterpretq_u16_u8(tmpData4), vdupq_n_u16(0x793d))); // =y
						
						uint8x16_t match3EqYA = vandq_u8(match2EqA, vceqq_u8(vextq_u8(dataA, dataB, 3), vdupq_n_u8('y')));
						uint8x16_t match3EqYB = vandq_u8(match2EqB, vceqq_u8(vextq_u8(dataB, dataC, 3), vdupq_n_u8('y')));
						uint8x16_t match3EqYC = vandq_u8(match2EqC, vceqq_u8(vextq_u8(dataC, dataD, 3), vdupq_n_u8('y')));
						uint8x16_t match3EqYD = vandq_u8(match2EqD, vceqq_u8(tmpData3, vdupq_n_u8('y')));
						match4EqYA = vreinterpretq_u8_u16(vshlq_n_u16(vreinterpretq_u16_u8(match4EqYA), 8));
						match4EqYB = vreinterpretq_u8_u16(vshlq_n_u16(vreinterpretq_u16_u8(match4EqYB), 8));
						match4EqYC = vreinterpretq_u8_u16(vshlq_n_u16(vreinterpretq_u16_u8(match4EqYC), 8));
						match4EqYD = vreinterpretq_u8_u16(vshlq_n_u16(vreinterpretq_u16_u8(match4EqYD), 8));
						// merge \r\n and =y matches for tmpData4
						uint8x16_t match4EndA = vorrq_u8(
							vandq_u8(match3CrA, match4LfA),
							vreinterpretq_u8_u16(vsriq_n_u16(vreinterpretq_u16_u8(match4EqYA), vreinterpretq_u16_u8(match3EqYA), 8))
						);
						uint8x16_t match4EndB = vorrq_u8(
							vandq_u8(match3CrB, match4LfB),
							vreinterpretq_u8_u16(vsriq_n_u16(vreinterpretq_u16_u8(match4EqYB), vreinterpretq_u16_u8(match3EqYB), 8))
						);
						uint8x16_t match4EndC = vorrq_u8(
							vandq_u8(match3CrC, match4LfC),
							vreinterpretq_u8_u16(vsriq_n_u16(vreinterpretq_u16_u8(match4EqYC), vreinterpretq_u16_u8(match3EqYC), 8))
						);
						uint8x16_t match4EndD = vorrq_u8(
							vandq_u8(match3CrD, match4LfD),
							vreinterpretq_u8_u16(vsriq_n_u16(vreinterpretq_u16_u8(match4EqYD), vreinterpretq_u16_u8(match3EqYD), 8))
						);
						// merge with \r\n.
						match4EndA = vandq_u8(match4EndA, match2NlDotA);
						match4EndB = vandq_u8(match4EndB, match2NlDotB);
						match4EndC = vandq_u8(match4EndC, match2NlDotC);
						match4EndD = vandq_u8(match4EndD, match2NlDotD);
						// match \r\n=y
						uint8x16_t match3EndA = vandq_u8(match3EqYA, match1NlA);
						uint8x16_t match3EndB = vandq_u8(match3EqYB, match1NlB);
						uint8x16_t match3EndC = vandq_u8(match3EqYC, match1NlC);
						uint8x16_t match3EndD = vandq_u8(match3EqYD, match1NlD);
						// combine match sequences
						uint8x16_t matchEnd = vorrq_u8(
							vorrq_u8(
								vorrq_u8(match4EndA, match3EndA),
								vorrq_u8(match4EndB, match3EndB)
							),
							vorrq_u8(
								vorrq_u8(match4EndC, match3EndC),
								vorrq_u8(match4EndD, match3EndD)
							)
						);
						if(LIKELIHOOD(0.001, neon_vect_is_nonzero(matchEnd))) {
							// terminator found
							// there's probably faster ways to do this, but reverting to scalar code should be good enough
							len += i;
							break;
						}
					}
					uint8x16_t mergeKillDots = vpaddq_u8(
						vpaddq_u8(
							vandq_u8(match2NlDotA, (uint8x16_t){1,2,4,8,16,32,64,128, 1,2,4,8,16,32,64,128}),
							vandq_u8(match2NlDotB, (uint8x16_t){1,2,4,8,16,32,64,128, 1,2,4,8,16,32,64,128})
						),
						vpaddq_u8(
							vandq_u8(match2NlDotC, (uint8x16_t){1,2,4,8,16,32,64,128, 1,2,4,8,16,32,64,128}),
							vandq_u8(match2NlDotD, (uint8x16_t){1,2,4,8,16,32,64,128, 1,2,4,8,16,32,64,128})
						)
					);
					uint8x8_t mergeKillDots2 = vpadd_u8(vget_low_u8(mergeKillDots), vget_high_u8(mergeKillDots));
					uint64_t killDots = vget_lane_u64(vreinterpret_u64_u8(mergeKillDots2), 0);
					mask |= (killDots << 2) & 0xffffffffffffffffULL;
					cmpCombined = vreinterpretq_u8_u64(vcombine_u64(vmov_n_u64(mask), vdup_n_u64(0)));
					nextMask = killDots >> (64-2);
				} else if(searchEnd) {
					uint8x16_t match3EqYA = vandq_u8(match2EqA, vceqq_u8(vextq_u8(dataA, dataB, 3), vdupq_n_u8('y')));
					uint8x16_t match3EqYB = vandq_u8(match2EqB, vceqq_u8(vextq_u8(dataB, dataC, 3), vdupq_n_u8('y')));
					uint8x16_t match3EqYC = vandq_u8(match2EqC, vceqq_u8(vextq_u8(dataC, dataD, 3), vdupq_n_u8('y')));
					uint8x16_t match3EqYD = vandq_u8(match2EqD, vceqq_u8(vextq_u8(dataD, nextData, 3), vdupq_n_u8('y')));
					if(LIKELIHOOD(0.001, neon_vect_is_nonzero(vorrq_u8(
						vorrq_u8(match3EqYA, match3EqYB),
						vorrq_u8(match3EqYC, match3EqYD)
					)))) {
						uint8x16_t match1LfA = vceqq_u8(vextq_u8(dataA, dataB, 1), vdupq_n_u8('\n'));
						uint8x16_t match1LfB = vceqq_u8(vextq_u8(dataB, dataC, 1), vdupq_n_u8('\n'));
						uint8x16_t match1LfC = vceqq_u8(vextq_u8(dataC, dataD, 1), vdupq_n_u8('\n'));
						uint8x16_t match1LfD = vceqq_u8(vextq_u8(dataD, nextData, 1), vdupq_n_u8('\n'));
						uint8x16_t matchEnd = vorrq_u8(
							vorrq_u8(
								vandq_u8(match3EqYA, vandq_u8(match1LfA, cmpCrA)),
								vandq_u8(match3EqYB, vandq_u8(match1LfB, cmpCrB))
							),
							vorrq_u8(
								vandq_u8(match3EqYC, vandq_u8(match1LfC, cmpCrC)),
								vandq_u8(match3EqYD, vandq_u8(match1LfD, cmpCrD))
							)
						);
						if(LIKELIHOOD(0.001, neon_vect_is_nonzero(matchEnd))) {
							len += i;
							break;
						}
					}
					if(isRaw)
						nextMask = 0;
				} else if(isRaw) // no \r_. found
					nextMask = 0;
			}
			
			// a spec compliant encoder should never generate sequences: ==, =\n and =\r, but we'll handle them to be spec compliant
			// the yEnc specification requires any character following = to be unescaped, not skipped over, so we'll deal with that
			// firstly, check for invalid sequences of = (we assume that these are rare, as a spec compliant yEnc encoder should not generate these)
			if(LIKELIHOOD(0.0001, (mask & ((maskEq << 1) | escFirst)) != 0)) {
				uint8_t tmp = eqFixLUT[(maskEq&0xff) & ~escFirst];
				uint64_t maskEq2 = tmp;
				for(int j=8; j<64; j+=8) {
					tmp = eqFixLUT[((maskEq>>j)&0xff) & ~(tmp>>7)];
					maskEq2 |= ((uint64_t)tmp)<<j;
				}
				maskEq = maskEq2;
				
				// next, eliminate anything following a `=` from the special char mask; this eliminates cases of `=\r` so that they aren't removed
				maskEq = (maskEq<<1) | escFirst;
				mask &= ~maskEq;
				cmpCombined = vreinterpretq_u8_u64(vcombine_u64(vmov_n_u64(mask), vdup_n_u64(0)));
				escFirst = tmp>>7;
				
				// unescape chars following `=`
				dataA = vaddq_u8(
					dataA,
					vcombine_u8(
						vld1_u8((uint8_t*)(eqAddLUT + (maskEq&0xff))),
						vld1_u8((uint8_t*)(eqAddLUT + ((maskEq>>8)&0xff)))
					)
				);
				dataB = vaddq_u8(
					dataB,
					vcombine_u8(
						vld1_u8((uint8_t*)(eqAddLUT + ((maskEq>>16)&0xff))),
						vld1_u8((uint8_t*)(eqAddLUT + ((maskEq>>24)&0xff)))
					)
				);
				dataC = vaddq_u8(
					dataC,
					vcombine_u8(
						vld1_u8((uint8_t*)(eqAddLUT + ((maskEq>>32)&0xff))),
						vld1_u8((uint8_t*)(eqAddLUT + ((maskEq>>40)&0xff)))
					)
				);
				dataD = vaddq_u8(
					dataD,
					vcombine_u8(
						vld1_u8((uint8_t*)(eqAddLUT + ((maskEq>>48)&0xff))),
						vld1_u8((uint8_t*)(eqAddLUT + ((maskEq>>56)&0xff)))
					)
				);
			} else {
				// no invalid = sequences found - we can cut out some things from above
				// this code path is a shortened version of above; it's here because it's faster, and what we'll be dealing with most of the time
				escFirst = (maskEq >> 63);
				
				dataA = vsubq_u8(
					dataA,
					vbslq_u8(
						vextq_u8(vdupq_n_u8(0), cmpEqA, 15),
						vdupq_n_u8(64+42),
						yencOffset
					)
				);
				dataB = vsubq_u8(
					dataB,
					vbslq_u8(
						vextq_u8(cmpEqA, cmpEqB, 15),
						vdupq_n_u8(64+42),
						vdupq_n_u8(42)
					)
				);
				dataC = vsubq_u8(
					dataC,
					vbslq_u8(
						vextq_u8(cmpEqB, cmpEqC, 15),
						vdupq_n_u8(64+42),
						vdupq_n_u8(42)
					)
				);
				dataD = vsubq_u8(
					dataD,
					vbslq_u8(
						vextq_u8(cmpEqC, cmpEqD, 15),
						vdupq_n_u8(64+42),
						vdupq_n_u8(42)
					)
				);
			}
			yencOffset[0] = (escFirst << 6) | 42;
			
			// all that's left is to 'compress' the data (skip over masked chars)
			uint8x8_t vCounts = vsub_u8(
				vdup_n_u8(8),
				vcnt_u8(vget_low_u8(cmpCombined))
			);
			// TODO: scalar add is probably faster than VPADD
			vCounts = vpadd_u8(vCounts, vCounts);
			uint32_t counts = vget_lane_u32(vreinterpret_u32_u8(vCounts), 0);
			
			vst1q_u8(p, vqtbl1q_u8(
				dataA,
				vld1q_u8((uint8_t*)(unshufLUTBig + (mask&0x7fff)))
			));
			p += counts & 0xff;
			mask >>= 16;
			vst1q_u8(p, vqtbl1q_u8(
				dataB,
				vld1q_u8((uint8_t*)(unshufLUTBig + (mask&0x7fff)))
			));
			p += (counts>>8) & 0xff;
			mask >>= 16;
			vst1q_u8(p, vqtbl1q_u8(
				dataC,
				vld1q_u8((uint8_t*)(unshufLUTBig + (mask&0x7fff)))
			));
			p += (counts>>16) & 0xff;
			mask >>= 16;
			vst1q_u8(p, vqtbl1q_u8(
				dataD,
				vld1q_u8((uint8_t*)(unshufLUTBig + (mask&0x7fff)))
			));
			p += (counts>>24) & 0xff;
		} else {
			dataA = vsubq_u8(dataA, yencOffset);
			dataB = vsubq_u8(dataB, vdupq_n_u8(42));
			dataC = vsubq_u8(dataC, vdupq_n_u8(42));
			dataD = vsubq_u8(dataD, vdupq_n_u8(42));
			vst1q_u8(p, dataA);
			vst1q_u8(p+sizeof(uint8x16_t), dataB);
			vst1q_u8(p+sizeof(uint8x16_t)*2, dataC);
			vst1q_u8(p+sizeof(uint8x16_t)*3, dataD);
			p += sizeof(uint8x16_t)*4;
			escFirst = 0;
			yencOffset = vdupq_n_u8(42);
		}
	}
}

void decoder_set_neon_funcs() {
	decoder_init_lut();
	_do_decode = &do_decode_simd<false, false, sizeof(uint8x16_t)*4, do_decode_neon<false, false> >;
	_do_decode_raw = &do_decode_simd<true, false, sizeof(uint8x16_t)*4, do_decode_neon<true, false> >;
	_do_decode_end = &do_decode_simd<false, true, sizeof(uint8x16_t)*4, do_decode_neon<false, true> >;
	_do_decode_end_raw = &do_decode_simd<true, true, sizeof(uint8x16_t)*4, do_decode_neon<true, true> >;
}
#else
void decoder_set_neon_funcs() {}
#endif
