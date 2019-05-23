#include "common.h"
#ifdef __ARM_NEON
#include "decoder_common.h"

static uint16_t neon_movemask(uint8x16_t in) {
	uint8x16_t mask = vandq_u8(in, (uint8x16_t){1,2,4,8,16,32,64,128, 1,2,4,8,16,32,64,128});
# if defined(__aarch64__)
	/* if VADD is slow, can save one using 
	mask = vzip1q_u8(mask, vextq_u8(mask, mask, 8));
	return vaddvq_u16(vreinterpretq_u16_u8(mask));
	*/
	return (vaddv_u8(vget_high_u8(mask)) << 8) | vaddv_u8(vget_low_u8(mask));
# else
	uint8x8_t res = vpadd_u8(vget_low_u8(mask), vget_high_u8(mask));
	res = vpadd_u8(res, res);
	res = vpadd_u8(res, res);
	return vget_lane_u16(vreinterpret_u16_u8(res), 0);
# endif
}
static bool neon_vect_is_nonzero(uint8x16_t v) {
# ifdef __aarch64__
	return !!(vget_lane_u64(vreinterpret_u64_u32(vqmovn_u64(vreinterpretq_u64_u8(v))), 0));
# else
	uint32x4_t tmp1 = vreinterpretq_u32_u8(v);
	uint32x2_t tmp2 = vorr_u32(vget_low_u32(tmp1), vget_high_u32(tmp1));
	return !!(vget_lane_u32(vpmax_u32(tmp2, tmp2), 0));
# endif
}


template<bool isRaw, bool searchEnd>
inline void do_decode_neon(const uint8_t* src, long& len, unsigned char*& p, unsigned char& escFirst, uint16_t& nextMask) {
	for(long i = -len; i; i += sizeof(uint8x16_t)) {
		uint8x16_t data = vld1q_u8(src+i);
		
		// search for special chars
		uint8x16_t cmpEq = vceqq_u8(data, vdupq_n_u8('=')),
#ifdef __aarch64__
		cmp = vqtbx1q_u8(
			cmpEq,
			//                                \n      \r
			(uint8x16_t){0,0,0,0,0,0,0,0,0,0,255,0,0,255,0,0},
			data
		);
#else
		cmp = vorrq_u8(
			vorrq_u8(
				vceqq_u8(data, vreinterpretq_u8_u16(vdupq_n_u16(0x0a0d))), // \r\n
				vceqq_u8(data, vreinterpretq_u8_u16(vdupq_n_u16(0x0d0a)))  // \n\r
			),
			cmpEq
		);
#endif
		
		
		uint8x16_t oData;
		if(LIKELIHOOD(0.01 /* guess */, escFirst!=0)) { // rarely hit branch: seems to be faster to use 'if' than a lookup table, possibly due to values being able to be held in registers?
			// first byte needs escaping due to preceeding = in last loop iteration
			oData = vsubq_u8(data, (uint8x16_t){42+64,42,42,42,42,42,42,42,42,42,42,42,42,42,42,42});
			cmp = vandq_u8(cmp, (uint8x16_t){0,2,4,8,16,32,64,128, 1,2,4,8,16,32,64,128});
		} else {
			oData = vsubq_u8(data, vdupq_n_u8(42));
			cmp = vandq_u8(cmp, (uint8x16_t){1,2,4,8,16,32,64,128, 1,2,4,8,16,32,64,128});
		}
		
#ifdef __aarch64__
		if (LIKELIHOOD(0.25 /*guess*/, neon_vect_is_nonzero(cmp)) || (isRaw && LIKELIHOOD(0.001, nextMask!=0))) {
			/* for if CPU has fast VADD?
			uint16_t mask = (vaddv_u8(vget_high_u8(cmp)) << 8) | vaddv_u8(vget_low_u8(cmp));
			uint16_t maskEq = neon_movemask(cmpEq);
			*/
			
			uint8x16_t cmpEqMasked = vandq_u8(cmpEq, (uint8x16_t){1,2,4,8,16,32,64,128, 1,2,4,8,16,32,64,128});
			uint8x16_t cmpCombined = vpaddq_u8(cmp, cmpEqMasked);
			uint8x8_t cmpPacked = vpadd_u8(vget_low_u8(cmpCombined), vget_high_u8(cmpCombined));
			cmpPacked = vpadd_u8(cmpPacked, cmpPacked);
			uint16_t mask = vget_lane_u16(vreinterpret_u16_u8(cmpPacked), 0);
			uint16_t maskEq = vget_lane_u16(vreinterpret_u16_u8(cmpPacked), 1);
			
#else
		uint8x8_t cmpPacked = vpadd_u8(vget_low_u8(cmp), vget_high_u8(cmp));
		cmpPacked = vpadd_u8(cmpPacked, cmpPacked);
		if(LIKELIHOOD(0.25, vget_lane_u32(vreinterpret_u32_u8(cmpPacked), 0) != 0) || (isRaw && LIKELIHOOD(0.001, nextMask!=0))) {
			uint8x16_t cmpEqMasked = vandq_u8(cmpEq, (uint8x16_t){1,2,4,8,16,32,64,128, 1,2,4,8,16,32,64,128});
			uint8x8_t cmpEqPacked = vpadd_u8(vget_low_u8(cmpEqMasked), vget_high_u8(cmpEqMasked));
			cmpEqPacked = vpadd_u8(cmpEqPacked, cmpEqPacked);
			
			cmpPacked = vpadd_u8(cmpPacked, cmpEqPacked);
			uint16_t mask = vget_lane_u16(vreinterpret_u16_u8(cmpPacked), 0);
			uint16_t maskEq = vget_lane_u16(vreinterpret_u16_u8(cmpPacked), 2);
#endif
			if(isRaw) mask |= nextMask;
			
			bool checkNewlines = (isRaw || searchEnd) && LIKELIHOOD(0.15, mask != maskEq);
			
			// a spec compliant encoder should never generate sequences: ==, =\n and =\r, but we'll handle them to be spec compliant
			// the yEnc specification requires any character following = to be unescaped, not skipped over, so we'll deal with that
			// firstly, check for invalid sequences of = (we assume that these are rare, as a spec compliant yEnc encoder should not generate these)
			unsigned char oldEscFirst = escFirst;
			if(LIKELIHOOD(0.0001, (maskEq & ((maskEq << 1) | escFirst)) != 0)) {
				uint16_t tmp = eqFixLUT[(maskEq&0xff) & ~escFirst];
				maskEq = (eqFixLUT[(maskEq>>8) & ~(tmp>>7)] << 8) | tmp;
				
				escFirst = (maskEq >> (sizeof(uint8x16_t)-1));
				// next, eliminate anything following a `=` from the special char mask; this eliminates cases of `=\r` so that they aren't removed
				maskEq <<= 1;
				mask &= ~maskEq;
				
				// unescape chars following `=`
				oData = vaddq_u8(
					oData,
					vcombine_u8(
						vld1_u8((uint8_t*)(eqAddLUT + (maskEq&0xff))),
						vld1_u8((uint8_t*)(eqAddLUT + ((maskEq>>8)&0xff)))
					)
				);
			} else {
				// no invalid = sequences found - we can cut out some things from above
				// this code path is a shortened version of above; it's here because it's faster, and what we'll be dealing with most of the time
				escFirst = (maskEq >> (sizeof(uint8x16_t)-1));
				maskEq <<= 1;
				mask &= ~maskEq;
				
				oData = vaddq_u8(
					oData,
					vandq_u8(
						vextq_u8(vdupq_n_u8(0), cmpEq, 15),
						vdupq_n_u8(-64)
					)
				);
			}
			
			// handle \r\n. sequences
			// RFC3977 requires the first dot on a line to be stripped, due to dot-stuffing
			if(checkNewlines) {
				// find instances of \r\n
				uint8x16_t tmpData1, tmpData2, tmpData3, tmpData4;
				uint8x16_t nextData = vld1q_u8(src+i + sizeof(uint8x16_t)); // only 32-bits needed, but there doesn't appear a nice way to do this via intrinsics: https://stackoverflow.com/questions/46910799/arm-neon-intrinsics-convert-d-64-bit-register-to-low-half-of-q-128-bit-regis
				tmpData1 = vextq_u8(data, nextData, 1);
				tmpData2 = vextq_u8(data, nextData, 2);
				if(searchEnd) {
					tmpData3 = vextq_u8(data, nextData, 3);
					if(isRaw) tmpData4 = vextq_u8(data, nextData, 4);
				}
				uint8x16_t matchNl1 = vreinterpretq_u8_u16(vceqq_u16(vreinterpretq_u16_u8(data), vdupq_n_u16(0x0a0d)));
				uint8x16_t matchNl2 = vreinterpretq_u8_u16(vceqq_u16(vreinterpretq_u16_u8(tmpData1), vdupq_n_u16(0x0a0d)));
				
				uint8x16_t matchDots, matchNlDots;
				bool hasDots;
				if(isRaw) {
					matchDots = vceqq_u8(tmpData2, vdupq_n_u8('.'));
					// merge preparation (for non-raw, it doesn't matter if this is shifted or not)
					matchNl1 = vbicq_u8(matchNl1, vreinterpretq_u8_u16(vdupq_n_u16(0xff00)));
					
					// merge matches of \r\n with those for .
					matchNlDots = vandq_u8(matchDots, vorrq_u8(matchNl1, matchNl2));
					hasDots = neon_vect_is_nonzero(matchNlDots);
				}
				
				if(searchEnd) {
					uint8x16_t cmpB1 = vreinterpretq_u8_u16(vceqq_u16(vreinterpretq_u16_u8(tmpData2), vdupq_n_u16(0x793d))); // "=y"
					uint8x16_t cmpB2 = vreinterpretq_u8_u16(vceqq_u16(vreinterpretq_u16_u8(tmpData3), vdupq_n_u16(0x793d)));
					if(isRaw && LIKELIHOOD(0.001, hasDots)) {
						// match instances of \r\n.\r\n and \r\n.=y
						uint8x16_t cmpC1 = vreinterpretq_u8_u16(vceqq_u16(vreinterpretq_u16_u8(tmpData3), vdupq_n_u16(0x0a0d)));
						uint8x16_t cmpC2 = vreinterpretq_u8_u16(vceqq_u16(vreinterpretq_u16_u8(tmpData4), vdupq_n_u16(0x0a0d)));
						cmpC1 = vorrq_u8(cmpC1, cmpB2);
						cmpC2 = vorrq_u8(cmpC2, vreinterpretq_u8_u16(vceqq_u16(vreinterpretq_u16_u8(tmpData4), vdupq_n_u16(0x793d))));
						cmpC2 = vandq_u8(cmpC2, vreinterpretq_u8_u16(vdupq_n_u16(0xff00)));
						cmpC1 = vorrq_u8(cmpC1, cmpC2);
						
						// and w/ dots
						cmpC1 = vandq_u8(cmpC1, matchNlDots);
						// then merge w/ cmpB
						cmpB1 = vandq_u8(cmpB1, matchNl1);
						cmpB2 = vandq_u8(cmpB2, matchNl2);
						
						cmpB1 = vorrq_u8(cmpC1, vorrq_u8(
							cmpB1, cmpB2
						));
						if(LIKELIHOOD(0.001, neon_vect_is_nonzero(cmpB1))) {
							// terminator found
							// there's probably faster ways to do this, but reverting to scalar code should be good enough
							escFirst = oldEscFirst;
							len += i;
							break;
						}
					} else {
						if(LIKELIHOOD(0.001, neon_vect_is_nonzero(
							vorrq_u8(cmpB1, cmpB2)
						))) {
							cmpB1 = vorrq_u8(
								vandq_u8(cmpB1, matchNl1),
								vandq_u8(cmpB2, matchNl2)
							);
							if(LIKELIHOOD(0.001, neon_vect_is_nonzero(cmpB1))) {
								escFirst = oldEscFirst;
								len += i;
								break;
							}
						}
					}
				}
				if(isRaw) {
					if(LIKELIHOOD(0.001, hasDots)) {
						uint16_t killDots = neon_movemask(matchNlDots);
						mask |= (killDots << 2) & 0xffff;
						nextMask = killDots >> (sizeof(uint8x16_t)-2);
					} else
						nextMask = 0;
				}
			}
			
			// all that's left is to 'compress' the data (skip over masked chars)
# ifdef __aarch64__
			vst1q_u8(p, vqtbl1q_u8(
				oData,
				vld1q_u8((uint8_t*)(unshufLUTBig + (mask&0x7fff)))
			));
			p += BitsSetTable256inv[mask & 0xff] + BitsSetTable256inv[mask >> 8];
#else
			// lookup compress masks and shuffle
			vst1_u8(p, vtbl1_u8(
				vget_low_u8(oData),
				vld1_u8((uint8_t*)(unshufLUT + (mask&0xff)))
			));
			p += BitsSetTable256inv[mask & 0xff];
			vst1_u8(p, vtbl1_u8(
				vget_high_u8(oData),
				vld1_u8((uint8_t*)(unshufLUT + (mask>>8)))
			));
			p += BitsSetTable256inv[mask >> 8];
			
# endif
			
		} else {
			vst1q_u8(p, oData);
			p += sizeof(uint8x16_t);
			escFirst = 0;
			if(isRaw) nextMask = 0;
		}
	}
}

void decoder_set_neon_funcs() {
	decoder_init_lut();
	_do_decode = &do_decode_simd<false, false, sizeof(uint8x16_t), do_decode_neon<false, false> >;
	_do_decode_raw = &do_decode_simd<true, false, sizeof(uint8x16_t), do_decode_neon<true, false> >;
	_do_decode_end = &do_decode_simd<false, true, sizeof(uint8x16_t), do_decode_neon<false, true> >;
	_do_decode_end_raw = &do_decode_simd<true, true, sizeof(uint8x16_t), do_decode_neon<true, true> >;
}
#else
void decoder_set_neon_funcs() {}
#endif
