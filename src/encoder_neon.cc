#include "common.h"

#ifdef __ARM_NEON
#include "encoder.h"

struct TShufMix {
	uint8x16_t shuf, mix;
};
ALIGN_32(struct TShufMix shufMixLUT[256]);

static const unsigned char* escapeLUT;
static const uint16_t* escapedLUT;

static size_t do_encode_neon(int line_size, int* colOffset, const unsigned char* src, unsigned char* dest, size_t len) {
	unsigned char* es = (unsigned char*)src + len;
	unsigned char *p = dest; // destination pointer
	long i = -len; // input position
	unsigned char c, escaped; // input character; escaped input character
	int col = *colOffset;
	
	if (col == 0) {
		c = es[i++];
		if (escapedLUT[c]) {
			*(uint16_t*)p = escapedLUT[c];
			p += 2;
			col = 2;
		} else {
			*(p++) = c + 42;
			col = 1;
		}
	}
	while(i < 0) {
		// main line
		while (i < -1-(long)sizeof(uint8x16_t) && col < line_size-1) {
			uint8x16_t data = vaddq_u8(
				vld1q_u8(es + i),
				vdupq_n_u8(42)
			);
			i += sizeof(uint8x16_t);
			// search for special chars
			uint8x16_t cmp = vorrq_u8(
				vorrq_u8(
#ifdef __aarch64__
					vceqzq_u8(data),
#else
					vceqq_u8(data, vdupq_n_u8(0)),
#endif
					vceqq_u8(data, vdupq_n_u8('\n'))
				),
				vorrq_u8(
					vceqq_u8(data, vdupq_n_u8('\r')),
					vceqq_u8(data, vdupq_n_u8('='))
				)
			);
			
#ifdef __aarch64__
			if (vget_lane_u64(vreinterpret_u64_u32(vqmovn_u64(vreinterpretq_u64_u8(cmp))), 0)) {
				cmp = vandq_u8(cmp, (uint8x16_t){1,2,4,8,16,32,64,128, 1,2,4,8,16,32,64,128});
				uint8_t m1 = vaddv_u8(vget_low_u8(cmp));
				uint8_t m2 = vaddv_u8(vget_high_u8(cmp));
#else
			cmp = vandq_u8(cmp, (uint8x16_t){1,2,4,8,16,32,64,128, 1,2,4,8,16,32,64,128});
			uint8x8_t cmpPacked = vpadd_u8(vget_low_u8(cmp), vget_high_u8(cmp));
			cmpPacked = vpadd_u8(cmpPacked, cmpPacked);
			uint32_t mask2 = vget_lane_u32(vreinterpret_u32_u8(cmpPacked), 0);
			if(mask2 != 0) {
				mask2 += (mask2 & 0xff00ff00) >> 8;
				uint8_t m1 = (mask2 & 0xff);
				uint8_t m2 = ((mask2 & 0xff0000) >> 16);
#endif
				
				// perform lookup for shuffle mask
				uint8x16_t shufMA = vld1q_u8((uint8_t*)(&(shufMixLUT[m1].shuf)));
				uint8x16_t shufMB = vld1q_u8((uint8_t*)(&(shufMixLUT[m2].shuf)));
				
				
				// expand halves
#ifdef __aarch64__
				// second mask processes on second half, so add to the offsets
				shufMB = vaddq_u8(shufMB, vdupq_n_u8(8));
				
				uint8x16_t data2 = vqtbl1q_u8(data, shufMB);
				data = vqtbl1q_u8(data, shufMA);
#else
				uint8x16_t data2 = vcombine_u8(vtbl1_u8(vget_high_u8(data), vget_low_u8(shufMB)),
				                               vtbl1_u8(vget_high_u8(data), vget_high_u8(shufMB)));
				data = vcombine_u8(vtbl1_u8(vget_low_u8(data), vget_low_u8(shufMA)),
				                   vtbl1_u8(vget_low_u8(data), vget_high_u8(shufMA)));
#endif
				
				// add in escaped chars
				uint8x16_t shufMixMA = vld1q_u8((uint8_t*)(&(shufMixLUT[m1].mix)));
				uint8x16_t shufMixMB = vld1q_u8((uint8_t*)(&(shufMixLUT[m2].mix)));
				data = vaddq_u8(data, shufMixMA);
				data2 = vaddq_u8(data2, shufMixMB);
				// store out
				unsigned char shufALen = BitsSetTable256[m1] + 8;
				unsigned char shufBLen = BitsSetTable256[m2] + 8;
				vst1q_u8(p, data);
				p += shufALen;
				vst1q_u8(p, data2);
				p += shufBLen;
				col += shufALen + shufBLen;
				
				int ovrflowAmt = col - (line_size-1);
				if(ovrflowAmt > 0) {
					// we overflowed - find correct position to revert back to
					p -= ovrflowAmt;
					if(ovrflowAmt == shufBLen) {
						i -= 8;
						goto last_char_fast;
					} else {
						int isEsc;
						uint16_t tst;
						int midPointOffset = ovrflowAmt - shufBLen +1;
						if(ovrflowAmt > shufBLen) {
							tst = *(uint16_t*)((char*)(&(shufMixLUT[m1].shuf)) + shufALen - midPointOffset);
							i -= 8;
						} else {
							tst = *(uint16_t*)((char*)(&(shufMixLUT[m2].shuf)) - midPointOffset);
						}
						isEsc = (0xf0 == (tst&0xF0));
						p += isEsc;
						i -= 8 - ((tst>>8)&0xf) - isEsc;
						//col = line_size-1 + isEsc; // doesn't need to be set, since it's never read again
						if(isEsc)
							goto after_last_char_fast;
						else
							goto last_char_fast;
					}
				}
			} else {
				vst1q_u8(p, data);
				p += sizeof(uint8x16_t);
				col += sizeof(uint8x16_t);
				if(col > line_size-1) {
					p -= col - (line_size-1);
					i -= col - (line_size-1);
					//col = line_size-1; // doesn't need to be set, since it's never read again
					goto last_char_fast;
				}
			}
		}
		// handle remaining chars
		while(col < line_size-1) {
			c = es[i++], escaped = escapeLUT[c];
			if (escaped) {
				*(p++) = escaped;
				col++;
			}
			else {
				*(uint16_t*)p = escapedLUT[c];
				p += 2;
				col += 2;
			}
			if (i >= 0) goto end;
		}
		
		// last line char
		if(col < line_size) { // this can only be false if the last character was an escape sequence (or line_size is horribly small), in which case, we don't need to handle space/tab cases
			last_char_fast:
			c = es[i++];
			if (escapedLUT[c] && c != '.'-42) {
				*(uint16_t*)p = escapedLUT[c];
				p += 2;
			} else {
				*(p++) = c + 42;
			}
		}
		
		after_last_char_fast:
		if (i >= 0) break;
		
		c = es[i++];
		if (escapedLUT[c]) {
			*(uint32_t*)p = UINT32_16_PACK(UINT16_PACK('\r', '\n'), (uint32_t)escapedLUT[c]);
			p += 4;
			col = 2;
		} else {
			*(uint32_t*)p = UINT32_PACK('\r', '\n', (uint32_t)(c+42), 0);
			p += 3;
			col = 1;
		}
	}
	
	end:
	// special case: if the last character is a space/tab, it needs to be escaped as it's the final character on the line
	unsigned char lc = *(p-1);
	if(lc == '\t' || lc == ' ') {
		*(uint16_t*)(p-1) = UINT16_PACK('=', lc+64);
		p++;
		col++;
	}
	*colOffset = col;
	return p - dest;
}

void encoder_neon_init(const unsigned char* _escapeLUT, const uint16_t* _escapedLUT) {
	escapeLUT = _escapeLUT;
	escapedLUT = _escapedLUT;
	
	_do_encode = &do_encode_neon;
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
		vst1q_u8((uint8_t*)(&(shufMixLUT[i].shuf)), shuf);
		
		// calculate add mask for mixing escape chars in
		uint8x16_t maskEsc = vceqq_u8(vandq_u8(shuf, vdupq_n_u8(0xf0)), vdupq_n_u8(0xf0));
		uint8x16_t addMask = vandq_u8(vextq_u8(vdupq_n_u8(0), maskEsc, 15), vdupq_n_u8(64));
		addMask = vorrq_u8(addMask, vandq_u8(maskEsc, vdupq_n_u8('=')));
		
		vst1q_u8((uint8_t*)(&(shufMixLUT[i].mix)), addMask);
	}
}
#else
void encoder_neon_init(const unsigned char*, const uint16_t*) {}
#endif /* defined(__ARM_NEON) */
