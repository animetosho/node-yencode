#include "common.h"

#ifdef __ARM_NEON
#include "encoder.h"
#include "encoder_common.h"

uint8x16_t ALIGN_TO(16, shufLUT[256]);

#if defined(_MSC_VER) && !defined(__aarch64__)
# include <armintr.h>
# define UADD8 _arm_uadd8
#elif defined(__ARM_FEATURE_SIMD32)
# if defined(__GNUC__) && !defined(__clang__)
// from https://stackoverflow.com/questions/19034275/rvct-to-arm-gcc-porting-uadd8
__attribute__( ( always_inline ) ) static __inline__ uint32_t _UADD8(uint32_t op1, uint32_t op2)
{
  uint32_t result;
  __asm__ ("uadd8 %0, %1, %2" : "=r" (result) : "r" (op1), "r" (op2) );
  return(result);
}
#  define UADD8 _UADD8
# else
#  include <arm_acle.h>
#  define UADD8 __uadd8
# endif
#endif

static HEDLEY_ALWAYS_INLINE void encode_eol_handle_post(const uint8_t* HEDLEY_RESTRICT es, long& i, uint8_t*& p, int& col) {
	uint8_t c = es[i++];
	if (LIKELIHOOD(0.0273, escapedLUT[c]!=0)) {
		*(uint32_t*)p = UINT32_16_PACK(UINT16_PACK('\r', '\n'), (uint32_t)escapedLUT[c]);
		p += 4;
		col = 2;
	} else {
		*(uint32_t*)p = UINT32_PACK('\r', '\n', (uint32_t)(c+42), 0);
		p += 3;
		col = 1;
	}
}
static HEDLEY_ALWAYS_INLINE void encode_eol_handle_pre(const uint8_t* HEDLEY_RESTRICT es, long& i, uint8_t*& p, int& col) {
	uint8_t c = es[i], c2 = es[i+1];
	if (LIKELIHOOD(0.0234, escapedLUT[c] && c != '.'-42)) {
		*(uint16_t*)p = escapedLUT[c];
		p += 2;
		if (LIKELIHOOD(0.0273, escapedLUT[c2]!=0)) {
			*(uint32_t*)p = UINT32_16_PACK(UINT16_PACK('\r', '\n'), (uint32_t)escapedLUT[c2]);
			p += 4;
			col = 2;
		} else {
			*(uint32_t*)p = UINT32_PACK('\r', '\n', (uint32_t)(c2+42), 0);
			p += 3;
			col = 1;
		}
	}
	else if (LIKELIHOOD(0.0273, escapedLUT[c2]!=0)) {
		*(p++) = c + 42;
		*(uint32_t*)p = UINT32_16_PACK(UINT16_PACK('\r', '\n'), (uint32_t)escapedLUT[c2]);
		p += 4;
		col = 2;
	} else {
		uint32_t data = *(uint16_t*)(es+i);
		data = data | (data<<16);
		data &= 0xff0000ff;
#ifdef UADD8
		data = UADD8(data, 0x2a0a0d2a);
#else
		data += 0x2a00002a;
		data &= 0xff0000ff;
		data |= 0x000a0d00;
#endif
		*(uint32_t*)p = data;
		col = 1;
		p += 4;
	}
	
	i += 2;
}


static HEDLEY_ALWAYS_INLINE void do_encode_neon(int line_size, int* colOffset, const uint8_t* HEDLEY_RESTRICT srcEnd, uint8_t* HEDLEY_RESTRICT& dest, size_t& len) {
	if(len < 2 || line_size < 4) return;
	
	uint8_t *p = dest; // destination pointer
	long i = -(long)len; // input position
	int col = *colOffset;
	int lineSizeSub1 = line_size - 1;
	
	// offset position to enable simpler loop condition checking
	const int INPUT_OFFSET = sizeof(uint8x16_t) + 2 -1; // extra 2 chars for EOL handling, -1 to change <= to <
	i += INPUT_OFFSET;
	const uint8_t* es = srcEnd - INPUT_OFFSET;
	
	if (LIKELIHOOD(0.999, col == 0)) {
		uint8_t c = es[i++];
		if (LIKELIHOOD(0.0273, escapedLUT[c] != 0)) {
			*(uint16_t*)p = escapedLUT[c];
			p += 2;
			col = 2;
		} else {
			*(p++) = c + 42;
			col = 1;
		}
	}
	if(LIKELIHOOD(0.001, col >= lineSizeSub1)) {
		if(col == lineSizeSub1)
			encode_eol_handle_pre(es, i, p, col);
		else
			encode_eol_handle_post(es, i, p, col);
	}
	while(i < 0) {
		uint8x16_t oData = vld1q_u8(es + i);
		uint8x16_t data = vaddq_u8(oData, vdupq_n_u8(42));
		i += sizeof(uint8x16_t);
		// search for special chars
#ifdef __aarch64__
		uint8x16_t cmp = vqtbx1q_u8(
			vceqq_u8(oData, vdupq_n_u8('='-42)),
			//            \0                    \n      \r
			(uint8x16_t){255,0,0,0,0,0,0,0,0,0,255,0,0,255,0,0},
			data
		);
#else
		uint8x16_t cmp = vorrq_u8(
			vorrq_u8(
				vceqq_u8(data, vdupq_n_u8(0)),
				vceqq_u8(oData, vdupq_n_u8('='-42))
			),
			vorrq_u8(
				vceqq_u8(oData, vdupq_n_u8('\r'-42)),
				vceqq_u8(oData, vdupq_n_u8('\n'-42))
			)
		);
#endif
		
		
#ifdef __aarch64__
		if (LIKELIHOOD(0.2227, vget_lane_u64(vreinterpret_u64_u32(vqmovn_u64(vreinterpretq_u64_u8(cmp))), 0)!=0)) {
			uint8x16_t cmpPacked = vandq_u8(cmp, (uint8x16_t){1,2,4,8,16,32,64,128, 1,2,4,8,16,32,64,128});
			uint8_t m1 = vaddv_u8(vget_low_u8(cmpPacked));
			uint8_t m2 = vaddv_u8(vget_high_u8(cmpPacked));
#else
		uint8x16_t cmpPacked = vandq_u8(cmp, (uint8x16_t){1,2,4,8,16,32,64,128, 1,2,4,8,16,32,64,128});
		uint8x8_t cmpPackedHalf = vpadd_u8(vget_low_u8(cmpPacked), vget_high_u8(cmpPacked));
		cmpPackedHalf = vpadd_u8(cmpPackedHalf, cmpPackedHalf);
		uint32_t mask2 = vget_lane_u32(vreinterpret_u32_u8(cmpPackedHalf), 0);
		if(LIKELIHOOD(0.2227, mask2 != 0)) {
			mask2 += mask2 >> 8;
			uint8_t m1 = (mask2 & 0xff);
			uint8_t m2 = ((mask2 >> 16) & 0xff);
#endif
			
			// perform lookup for shuffle mask
			uint8x16_t shufMA = vld1q_u8((uint8_t*)(shufLUT + m1));
			uint8x16_t shufMB = vld1q_u8((uint8_t*)(shufLUT + m2));
			
			data = vaddq_u8(data, vandq_u8(cmp, vdupq_n_u8(64)));
			
			// expand halves
#ifdef __aarch64__
			// second mask processes on second half, so add to the offsets
			shufMB = vorrq_u8(shufMB, vdupq_n_u8(8));
			
			uint8x16_t data2 = vqtbx1q_u8(vdupq_n_u8('='), data, shufMB);
			data = vqtbx1q_u8(vdupq_n_u8('='), data, shufMA);
#else
			uint8x16_t data2 = vcombine_u8(vtbx1_u8(vdup_n_u8('='), vget_high_u8(data), vget_low_u8(shufMB)),
			                               vtbx1_u8(vdup_n_u8('='), vget_high_u8(data), vget_high_u8(shufMB)));
			data = vcombine_u8(vtbx1_u8(vdup_n_u8('='), vget_low_u8(data), vget_low_u8(shufMA)),
			                   vtbx1_u8(vdup_n_u8('='), vget_low_u8(data), vget_high_u8(shufMA)));
#endif
			
			// store out
			unsigned char shufALen = BitsSetTable256plus8[m1];
			unsigned char shufBLen = BitsSetTable256plus8[m2];
			vst1q_u8(p, data);
			p += shufALen;
			vst1q_u8(p, data2);
			p += shufBLen;
			col += shufALen + shufBLen;
			
			long ovrflowAmt = col - lineSizeSub1;
			if(LIKELIHOOD(0.15, ovrflowAmt >= 0)) {
				// we overflowed - find correct position to revert back to
				p -= ovrflowAmt;
				if(ovrflowAmt == shufBLen) {
					i -= 8;
				} else if(ovrflowAmt != 0) {
					int isEsc;
					uint16_t tst;
					int midPointOffset = ovrflowAmt - shufBLen +1;
					if(ovrflowAmt > shufBLen) {
						tst = *(uint16_t*)((char*)(shufLUT + m1) + shufALen - midPointOffset);
						i -= 8;
					} else {
						tst = *(uint16_t*)((char*)(shufLUT + m2) - midPointOffset);
					}
					isEsc = (0xf0 == (tst&0xF0));
					p += isEsc;
					i -= 8 - ((tst>>8)&0xf) - isEsc;
					if(isEsc) {
						encode_eol_handle_post(es, i, p, col);
						continue;
					}
				}
				encode_eol_handle_pre(es, i, p, col);
			}
		} else {
			vst1q_u8(p, data);
			p += sizeof(uint8x16_t);
			col += sizeof(uint8x16_t);
			long ovrflowAmt = col - lineSizeSub1;
			if(ovrflowAmt >= 0) {
				p -= ovrflowAmt;
				i -= ovrflowAmt;
				encode_eol_handle_pre(es, i, p, col);
			}
		}
	}
	
	*colOffset = col;
	dest = p;
	len = -(i - INPUT_OFFSET);
}

void encoder_neon_init() {
	_do_encode = &do_encode_simd<do_encode_neon>;
	// generate shuf LUT
	for(int i=0; i<256; i++) {
		int k = i;
		uint8_t res[16];
		int p = 0;
		for(int j=0; j<8; j++) {
			if(k & 1) {
				res[j+p] = 0xf0 + j;
				p++;
			}
			res[j+p] = j;
			k >>= 1;
		}
		for(; p<8; p++)
			res[8+p] = 8+p +0x80; // +0x80 => 0 discarded entries; has no effect other than to ease debugging
		
		uint8x16_t shuf = vld1q_u8(res);
		vst1q_u8((uint8_t*)(shufLUT + i), shuf);
	}
}
#else
void encoder_neon_init() {}
#endif /* defined(__ARM_NEON) */
