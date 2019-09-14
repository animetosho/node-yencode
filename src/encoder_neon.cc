#include "common.h"

#ifdef __ARM_NEON
#include "encoder.h"
#include "encoder_common.h"

uint8x16_t ALIGN_TO(16, shufLUT[256]);
uint8x16_t ALIGN_TO(16, nlShufLUT[256]);
static uint16_t expandLUT[256];

static HEDLEY_ALWAYS_INLINE void encode_eol_handle_pre(const uint8_t* HEDLEY_RESTRICT es, long& i, uint8_t*& p, int& col, int lineSizeOffset) {
	uint8x16_t oData = vld1q_u8(es + i);
	uint8x16_t data = oData;
#ifdef __aarch64__
	data = vaddq_u8(oData, vdupq_n_u8(42));
	uint8x16_t cmp = vqtbx1q_u8(
		vceqq_u8(oData, vdupq_n_u8('='-42)),
		//          \0                \t\n    \r
		(uint8x16_t){3,0,0,0,0,0,0,0,0,2,3,0,0,3,0,0},
		data
	);
	cmp = vorrq_u8(cmp, vqtbl1q_u8( // ORR+TBL seems to work better than TBX, maybe due to long TBL/X latency?
		//          \s                           .  
		(uint8x16_t){2,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0},
		vaddq_u8(oData, vdupq_n_u8(42-32))
	));
	cmp = vcgtq_u8(cmp, (uint8x16_t){1,0,2,2,2,2,2,2,2,2,2,2,2,2,2,2});
#else
	uint8x16_t cmp = vorrq_u8(
		vorrq_u8(
			vceqq_u8(oData, vdupq_n_u8(-42)),
			vceqq_u8(oData, vdupq_n_u8('='-42))
		),
		vorrq_u8(
			vceqq_u8(oData, vdupq_n_u8('\r'-42)),
			vceqq_u8(oData, vdupq_n_u8('\n'-42))
		)
	);
	
	// dup low 2 bytes & compare
	uint8x8_t firstTwoChars = vreinterpret_u8_u16(vdup_lane_u16(vreinterpret_u16_u8(vget_low_u8(oData)), 0));
	uint8x8_t cmpNl = vceq_u8(firstTwoChars, vreinterpret_u8_s8((int8x8_t){
		' '-42,' '-42,'\t'-42,'\t'-42,'\r'-42,'.'-42,'='-42,'='-42
	}));
	// use padd to merge comparisons
	uint16x4_t cmpNl2 = vreinterpret_u16_u8(cmpNl);
	cmpNl2 = vpadd_u16(cmpNl2, vdup_n_u16(0));
	cmpNl2 = vpadd_u16(cmpNl2, vdup_n_u16(0));
	cmp = vcombine_u8(
		vorr_u8(vget_low_u8(cmp), vreinterpret_u8_u16(cmpNl2)),
		vget_high_u8(cmp)
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
		
		uint8x16_t shufMA = vld1q_u8((uint8_t*)(nlShufLUT + m1));
		uint8x16_t data1 = vqsubq_u8(shufMA, vdupq_n_u8(64));
#ifdef __aarch64__
		data = vaddq_u8(data, vandq_u8(cmp, vdupq_n_u8(64)));
		data1 = vqtbx1q_u8(data1, data, shufMA);
#else
		data = vsubq_u8(data, vbslq_u8(cmp, vdupq_n_u8(-64-42), vdupq_n_u8(-42)));
		data1 = vcombine_u8(vtbx1_u8(vget_low_u8(data1), vget_low_u8(data), vget_low_u8(shufMA)),
		                              vtbx1_u8(vget_high_u8(data1), vget_low_u8(data), vget_high_u8(shufMA)));
#endif
		unsigned char shufALen = BitsSetTable256plus8[m1] +2;
		if(LIKELIHOOD(0.001, shufALen > sizeof(uint8x16_t))) {
			// unlikely special case, which would cause vectors to be overflowed
			// we'll just handle this by only dealing with the first 2 characters, and let main loop handle the rest
			// at least one of the first 2 chars is guaranteed to need escaping
			vst1_u8(p, vget_low_u8(data1));
			col = lineSizeOffset + ((m1 & 2)>>1) - 16+2;
			p += 4 + (m1 & 1) + ((m1 & 2)>>1);
			i += 2;
			return;
		}
		
		uint8x16_t shufMB = vld1q_u8((uint8_t*)(shufLUT + m2));
#ifdef __aarch64__
		shufMB = vorrq_u8(shufMB, vdupq_n_u8(8));
		uint8x16_t data2 = vqtbx1q_u8(vdupq_n_u8('='), data, shufMB);
#else
		uint8x16_t data2 = vcombine_u8(vtbx1_u8(vdup_n_u8('='), vget_high_u8(data), vget_low_u8(shufMB)),
		                               vtbx1_u8(vdup_n_u8('='), vget_high_u8(data), vget_high_u8(shufMB)));
#endif
		unsigned char shufTotalLen = shufALen + BitsSetTable256plus8[m2];
		vst1q_u8(p, data1);
		vst1q_u8(p+shufALen, data2);
		p += shufTotalLen;
		col = shufTotalLen-2 - (m1&1) + lineSizeOffset-16;
	} else {
		uint8x16_t data1;
#ifdef __aarch64__
		data1 = vqtbx1q_u8((uint8x16_t){0,'\r','\n',0,0,0,0,0,0,0,0,0,0,0,0,0}, data, (uint8x16_t){0,16,16,1,2,3,4,5,6,7,8,9,10,11,12,13});
#else
		data = vsubq_u8(data, vdupq_n_u8(-42));
		data1 = vcombine_u8(
			vtbx1_u8((uint8x8_t){0,'\r','\n',0,0,0,0,0}, vget_low_u8(data), (uint8x8_t){0,16,16,1,2,3,4,5}),
			vext_u8(vget_low_u8(data), vget_high_u8(data), 6)
		);
#endif
		vst1q_u8(p, data1);
		p += sizeof(uint8x16_t);
		*(uint16_t*)p = vgetq_lane_u16(vreinterpretq_u16_u8(data), 7);
		p += 2;
		col = lineSizeOffset;
	}
	
	i += sizeof(uint8x16_t);
	// TODO: check col >= 0 if we want to support short lines
}


static HEDLEY_ALWAYS_INLINE void do_encode_neon(int line_size, int* colOffset, const uint8_t* HEDLEY_RESTRICT srcEnd, uint8_t* HEDLEY_RESTRICT& dest, size_t& len) {
	if(len < sizeof(uint8x16_t)*2 || line_size < (int)sizeof(uint8x16_t)*2) return;
	
	uint8_t *p = dest; // destination pointer
	long i = -(long)len; // input position
	int lineSizeOffset = -line_size +16; // line size plus vector length
	int col = *colOffset - line_size +1;
	
	// offset position to enable simpler loop condition checking
	const int INPUT_OFFSET = sizeof(uint8x16_t) + sizeof(uint8x16_t) -1; // extra chars for EOL handling, -1 to change <= to <
	i += INPUT_OFFSET;
	const uint8_t* es = srcEnd - INPUT_OFFSET;
	
	if (LIKELIHOOD(0.999, col == -line_size+1)) {
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
		else {
			uint8_t c = es[i++];
			if (LIKELIHOOD(0.0273, escapedLUT[c]!=0)) {
				*(uint32_t*)p = UINT32_16_PACK(UINT16_PACK('\r', '\n'), (uint32_t)escapedLUT[c]);
				p += 4;
				col = 2-line_size + 1;
			} else {
				*(uint32_t*)p = UINT32_PACK('\r', '\n', (uint32_t)(c+42), 0);
				p += 3;
				col = 2-line_size;
			}
		}
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
			unsigned char shufTotalLen = shufALen + BitsSetTable256plus8[m2];
			vst1q_u8(p, data);
			vst1q_u8(p+shufALen, data2);
			p += shufTotalLen;
			col += shufTotalLen;
			
			if(LIKELIHOOD(0.15, col >= 0)) {
				// we overflowed - find correct position to revert back to
				uint32_t eqMask = (expandLUT[m2] << shufALen) | expandLUT[m1];
				eqMask >>= shufTotalLen - col -1;
				
				// count bits in eqMask; this VCNT approach seems to be about as fast as a 8-bit LUT on Cortex A53
				uint32x2_t vCnt;
				vCnt = vset_lane_u32(eqMask, vCnt, 0);
				vCnt = vreinterpret_u32_u8(vcnt_u8(vreinterpret_u8_u32(vCnt)));
				uint32_t cnt = vget_lane_u32(vCnt, 0);
				cnt += cnt >> 16;
				cnt += cnt >> 8;
				i += cnt & 0xff;
				
				long revert = col + (eqMask & 1);
				p -= revert;
				i -= revert;
				goto _encode_eol_handle_pre;
			}
		} else {
#ifndef __aarch64__
			data = vsubq_u8(data, vdupq_n_u8(-42));
#endif
			vst1q_u8(p, data);
			p += sizeof(uint8x16_t);
			col += sizeof(uint8x16_t);
			if(HEDLEY_UNLIKELY(col >= 0)) {
				p -= col;
				i -= col;
				
				_encode_eol_handle_pre:
				encode_eol_handle_pre(es, i, p, col, lineSizeOffset);
			}
		}
	}
	
	*colOffset = col + line_size -1;
	dest = p;
	len = -(i - INPUT_OFFSET);
}

void encoder_neon_init() {
	_do_encode = &do_encode_simd<do_encode_neon>;
	// generate shuf LUT
	for(int i=0; i<256; i++) {
		int k = i;
		uint16_t expand = 0;
		uint8_t* res = (uint8_t*)(shufLUT + i);
		uint8_t* nlShuf = (uint8_t*)(nlShufLUT + i);
		int p = 0, pNl = 0;
		for(int j=0; j<8; j++) {
			if(k & 1) {
				res[j+p] = 0xf0 + j;
				expand |= 1<<(j+p);
				p++;
				if(j+pNl < 16) {
					nlShuf[j+pNl] = '='+64;
					pNl++;
				}
			}
			res[j+p] = j;
			if(j+pNl < 16) nlShuf[j+pNl] = j;
			k >>= 1;
			
			if(j == 0) {
				// insert newline
				nlShuf[pNl+1] = '\r'+64;
				nlShuf[pNl+2] = '\n'+64;
				pNl += 2;
			}
		}
		for(; p<8; p++)
			res[8+p] = 8+p +0x80; // +0x80 => 0 discarded entries; has no effect other than to ease debugging
		
		expandLUT[i] = expand;
	}
}
#else
void encoder_neon_init() {}
#endif /* defined(__ARM_NEON) */
