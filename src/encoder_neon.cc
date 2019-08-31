#include "common.h"

#ifdef __ARM_NEON
#include "encoder.h"
#include "encoder_common.h"

uint8x16_t ALIGN_TO(16, shufLUT[256]);
static uint16_t expandLUT[256];

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

static HEDLEY_ALWAYS_INLINE void encode_eol_handle_post(const uint8_t* HEDLEY_RESTRICT es, long& i, uint8_t*& p, int& col, int lineSizeOffset) {
	uint8_t c = es[i++];
	if (LIKELIHOOD(0.0273, escapedLUT[c]!=0)) {
		*(uint32_t*)p = UINT32_16_PACK(UINT16_PACK('\r', '\n'), (uint32_t)escapedLUT[c]);
		p += 4;
		col = 1+lineSizeOffset;
	} else {
		*(uint32_t*)p = UINT32_PACK('\r', '\n', (uint32_t)(c+42), 0);
		p += 3;
		col = lineSizeOffset;
	}
}
static HEDLEY_ALWAYS_INLINE void encode_eol_handle_pre(const uint8_t* HEDLEY_RESTRICT es, long& i, uint8_t*& p, int& col, int lineSizeOffset) {
	uint8_t c = es[i], c2 = es[i+1];
	if (LIKELIHOOD(0.0234, escapedLUT[c] && c != '.'-42)) {
		*(uint16_t*)p = escapedLUT[c];
		p += 2;
		if (LIKELIHOOD(0.0273, escapedLUT[c2]!=0)) {
			*(uint32_t*)p = UINT32_16_PACK(UINT16_PACK('\r', '\n'), (uint32_t)escapedLUT[c2]);
			p += 4;
			col = 1+lineSizeOffset;
		} else {
			*(uint32_t*)p = UINT32_PACK('\r', '\n', (uint32_t)(c2+42), 0);
			p += 3;
			col = lineSizeOffset;
		}
	}
	else if (LIKELIHOOD(0.0273, escapedLUT[c2]!=0)) {
		*(p++) = c + 42;
		*(uint32_t*)p = UINT32_16_PACK(UINT16_PACK('\r', '\n'), (uint32_t)escapedLUT[c2]);
		p += 4;
		col = 1+lineSizeOffset;
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
		col = lineSizeOffset;
		p += 4;
	}
	
	i += 2;
}


static HEDLEY_ALWAYS_INLINE void do_encode_neon(int line_size, int* colOffset, const uint8_t* HEDLEY_RESTRICT srcEnd, uint8_t* HEDLEY_RESTRICT& dest, size_t& len) {
	if(len < 2 || line_size < 4) return;
	
	uint8_t *p = dest; // destination pointer
	long i = -(long)len; // input position
	int lineSizeOffset = -line_size +2; // line size excluding first/last char
	int col = *colOffset + lineSizeOffset -1;
	
	// offset position to enable simpler loop condition checking
	const int INPUT_OFFSET = sizeof(uint8x16_t) + 2 -1; // extra 2 chars for EOL handling, -1 to change <= to <
	i += INPUT_OFFSET;
	const uint8_t* es = srcEnd - INPUT_OFFSET;
	
	if (LIKELIHOOD(0.999, col == lineSizeOffset -1)) {
		uint8_t c = es[i++];
		if (LIKELIHOOD(0.0273, escapedLUT[c] != 0)) {
			*(uint16_t*)p = escapedLUT[c];
			p += 2;
			col += 2;
		} else {
			*(p++) = c + 42;
			col += 1;
		}
	}
	if(LIKELIHOOD(0.001, col >= 0)) {
		if(col == 0)
			encode_eol_handle_pre(es, i, p, col, lineSizeOffset);
		else
			encode_eol_handle_post(es, i, p, col, lineSizeOffset);
	}
	while(i < 0) {
		uint8x16_t data = vld1q_u8(es + i);
		i += sizeof(uint8x16_t);
		// search for special chars
#ifdef __aarch64__
		uint8x16_t cmpEq = vceqq_u8(data, vdupq_n_u8('='-42));
		data = vaddq_u8(data, vdupq_n_u8(42));
		uint8x16_t cmp = vqtbx1q_u8(
			cmpEq,
			//            \0                    \n      \r
			(uint8x16_t){255,0,0,0,0,0,0,0,0,0,255,0,0,255,0,0},
			data
		);
#else
		uint8x16_t cmp = vorrq_u8(
			vorrq_u8(
				vceqq_u8(data, vdupq_n_u8(-42)),
				vceqq_u8(data, vdupq_n_u8('='-42))
			),
			vorrq_u8(
				vceqq_u8(data, vdupq_n_u8('\r'-42)),
				vceqq_u8(data, vdupq_n_u8('\n'-42))
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
			
			// expand halves
#ifdef __aarch64__
			data = vaddq_u8(data, vandq_u8(cmp, vdupq_n_u8(64)));
			
			// second mask processes on second half, so add to the offsets
			shufMB = vorrq_u8(shufMB, vdupq_n_u8(8));
			
			uint8x16_t data2 = vqtbx1q_u8(vdupq_n_u8('='), data, shufMB);
			data = vqtbx1q_u8(vdupq_n_u8('='), data, shufMA);
#else
			data = vsubq_u8(data, vbslq_u8(cmp, vdupq_n_u8(-64-42), vdupq_n_u8(-42)));
			
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
			
			if(LIKELIHOOD(0.15, col >= 0)) {
				// we overflowed - find correct position to revert back to
				uint32_t eqMask = (expandLUT[m2] << shufALen) | expandLUT[m1];
				eqMask >>= shufBLen+shufALen - col -1;
				i -= col;
				
				// count bits in eqMask; this VCNT approach seems to be about as fast as a 8-bit LUT on Cortex A53
				uint32x2_t vCnt;
				vCnt = vset_lane_u32(eqMask, vCnt, 0);
				vCnt = vreinterpret_u32_u8(vcnt_u8(vreinterpret_u8_u32(vCnt)));
				uint32_t cnt = vget_lane_u32(vCnt, 0);
				cnt += cnt >> 16;
				cnt += cnt >> 8;
				i += cnt & 0xff;
				
				p -= col;
				if(eqMask & 1) {
					p++;
					encode_eol_handle_post(es, i, p, col, lineSizeOffset);
				} else
					encode_eol_handle_pre(es, i, p, col, lineSizeOffset);
			}
		} else {
#ifndef __aarch64__
			data = vsubq_u8(data, vdupq_n_u8(-42));
#endif
			vst1q_u8(p, data);
			p += sizeof(uint8x16_t);
			col += sizeof(uint8x16_t);
			if(col >= 0) {
				p -= col;
				i -= col;
				encode_eol_handle_pre(es, i, p, col, lineSizeOffset);
			}
		}
	}
	
	*colOffset = col - lineSizeOffset +1;
	dest = p;
	len = -(i - INPUT_OFFSET);
}

void encoder_neon_init() {
	_do_encode = &do_encode_simd<do_encode_neon>;
	// generate shuf LUT
	for(int i=0; i<256; i++) {
		int k = i;
		uint16_t expand = 0;
		uint8_t res[16];
		int p = 0;
		for(int j=0; j<8; j++) {
			if(k & 1) {
				res[j+p] = 0xf0 + j;
				expand |= 1<<(j+p);
				p++;
			}
			res[j+p] = j;
			k >>= 1;
		}
		for(; p<8; p++)
			res[8+p] = 8+p +0x80; // +0x80 => 0 discarded entries; has no effect other than to ease debugging
		
		uint8x16_t shuf = vld1q_u8(res);
		vst1q_u8((uint8_t*)(shufLUT + i), shuf);
		
		expandLUT[i] = expand;
	}
}
#else
void encoder_neon_init() {}
#endif /* defined(__ARM_NEON) */
