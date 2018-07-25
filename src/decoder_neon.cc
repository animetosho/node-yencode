#include "common.h"
#ifdef __ARM_NEON
static uint8_t eqFixLUT[256];
ALIGN_32(static uint8x8_t eqAddLUT[256]);
ALIGN_32(static uint8x8_t unshufLUT[256]);
ALIGN_32(static const uint8_t pshufb_combine_table[272]) = {
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

template<bool isRaw, bool searchEnd>
inline void do_decode_neon(const uint8_t* src, long& len, unsigned char*& p, unsigned char& escFirst, uint16_t& nextMask) {
	for(long i = -len; i; i += sizeof(uint8x16_t)) {
		uint8x16_t data = vld1q_u8(src+i);
		
		// search for special chars
		uint8x16_t cmpEq = vceqq_u8(data, vdupq_n_u8('=')),
		cmp = vorrq_u8(
			vorrq_u8(
				vceqq_u8(data, vreinterpretq_u8_u16(vdupq_n_u16(0x0a0d))), // \r\n
				vceqq_u8(data, vreinterpretq_u8_u16(vdupq_n_u16(0x0d0a)))  // \n\r
			),
			cmpEq
		);
		uint16_t mask = neon_movemask(cmp); // not the most accurate mask if we have invalid sequences; we fix this up later
		
		uint8x16_t oData;
		if(escFirst) { // rarely hit branch: seems to be faster to use 'if' than a lookup table, possibly due to values being able to be held in registers?
			// first byte needs escaping due to preceeding = in last loop iteration
			oData = vsubq_u8(data, (uint8x16_t){42+64,42,42,42,42,42,42,42,42,42,42,42,42,42,42,42});
			mask &= ~1;
		} else {
			oData = vsubq_u8(data, vdupq_n_u8(42));
		}
		if(isRaw) mask |= nextMask;
		
		if (mask != 0) {
			// a spec compliant encoder should never generate sequences: ==, =\n and =\r, but we'll handle them to be spec compliant
			// the yEnc specification requires any character following = to be unescaped, not skipped over, so we'll deal with that
			
			// firstly, resolve invalid sequences of = to deal with cases like '===='
			uint16_t maskEq = neon_movemask(cmpEq);
			uint16_t tmp = eqFixLUT[(maskEq&0xff) & ~escFirst];
			maskEq = (eqFixLUT[(maskEq>>8) & ~(tmp>>7)] << 8) | tmp;
			
			unsigned char oldEscFirst = escFirst;
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
			
			// handle \r\n. sequences
			// RFC3977 requires the first dot on a line to be stripped, due to dot-stuffing
			if(isRaw || searchEnd) {
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
				uint16_t killDots;
				if(isRaw) {
					matchDots = vceqq_u8(tmpData2, vdupq_n_u8('.'));
					// merge preparation (for non-raw, it doesn't matter if this is shifted or not)
					matchNl1 = vextq_u8(matchNl1, vdupq_n_u8(0), 1);
					
					// merge matches of \r\n with those for .
					matchNlDots = vandq_u8(matchDots, vorrq_u8(matchNl1, matchNl2));
					killDots = neon_movemask(matchNlDots);
				}
				
				if(searchEnd) {
					uint8x16_t cmpB1 = vreinterpretq_u8_u16(vceqq_u16(vreinterpretq_u16_u8(tmpData2), vdupq_n_u16(0x793d))); // "=y"
					uint8x16_t cmpB2 = vreinterpretq_u8_u16(vceqq_u16(vreinterpretq_u16_u8(tmpData3), vdupq_n_u16(0x793d)));
					if(isRaw && killDots) {
						// match instances of \r\n.\r\n and \r\n.=y
						uint8x16_t cmpC1 = vreinterpretq_u8_u16(vceqq_u16(vreinterpretq_u16_u8(tmpData3), vdupq_n_u16(0x0a0d)));
						uint8x16_t cmpC2 = vreinterpretq_u8_u16(vceqq_u16(vreinterpretq_u16_u8(tmpData4), vdupq_n_u16(0x0a0d)));
						cmpC1 = vorrq_u8(cmpC1, cmpB2);
						cmpC2 = vorrq_u8(cmpC2, vreinterpretq_u8_u16(vceqq_u16(vreinterpretq_u16_u8(tmpData4), vdupq_n_u16(0x793d))));
						cmpC2 = vextq_u8(vdupq_n_u8(0), cmpC2, 15);
						cmpC1 = vorrq_u8(cmpC1, cmpC2);
						
						// and w/ dots
						cmpC1 = vandq_u8(cmpC1, matchNlDots);
						// then merge w/ cmpB
						cmpB1 = vandq_u8(cmpB1, matchNl1);
						cmpB2 = vandq_u8(cmpB2, matchNl2);
						
						cmpB1 = vorrq_u8(cmpC1, vorrq_u8(
							cmpB1, cmpB2
						));
					} else {
						cmpB1 = vorrq_u8(
							vandq_u8(cmpB1, matchNl1),
							vandq_u8(cmpB2, matchNl2)
						);
					}
#ifdef __aarch64__
					if(vget_lane_u64(vreinterpret_u64_u32(vqmovn_u64(vreinterpretq_u64_u8(cmpB1))), 0))
#else
					uint32x4_t tmp1 = vreinterpretq_u32_u8(cmpB1);
					uint32x2_t tmp2 = vorr_u32(vget_low_u32(tmp1), vget_high_u32(tmp1));
					if(vget_lane_u32(vpmax_u32(tmp2, tmp2), 0))
#endif
					{
						// terminator found
						// there's probably faster ways to do this, but reverting to scalar code should be good enough
						escFirst = oldEscFirst;
						len += i;
						break;
					}
				}
				if(isRaw) {
					mask |= (killDots << 2) & 0xffff;
					nextMask = killDots >> (sizeof(uint8x16_t)-2);
				}
			}
			
			// all that's left is to 'compress' the data (skip over masked chars)
			unsigned char skipped = BitsSetTable256[mask & 0xff];
			// lookup compress masks and shuffle
			oData = vcombine_u8(
				vtbl1_u8(vget_low_u8(oData),  vld1_u8((uint8_t*)(unshufLUT + (mask&0xff)))),
				vtbl1_u8(vget_high_u8(oData), vld1_u8((uint8_t*)(unshufLUT + (mask>>8))))
			);
			// compact down
			uint8x16_t compact = vld1q_u8(pshufb_combine_table + skipped*sizeof(uint8x16_t));
# ifdef __aarch64__
			oData = vqtbl1q_u8(oData, compact);
# else
			uint8x8x2_t dataH = {vget_low_u8(oData), vget_high_u8(oData)};
			oData = vcombine_u8(vtbl2_u8(dataH, vget_low_u8(compact)),
			                    vtbl2_u8(dataH, vget_high_u8(compact)));
# endif
			vst1q_u8(p, oData);
			
			// increment output position
			p += sizeof(uint8x16_t) - skipped - BitsSetTable256[mask >> 8];
			
		} else {
			vst1q_u8(p, oData);
			p += sizeof(uint8x16_t);
			escFirst = 0;
			if(isRaw) nextMask = 0;
		}
	}
}

#include "decoder_common.h"
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
