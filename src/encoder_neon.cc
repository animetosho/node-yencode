#include "common.h"

#ifdef __ARM_NEON
#include "encoder.h"
#include "encoder_common.h"

// Clang wrongly assumes alignment on vst1q_u8_x2, and ARMv7 GCC doesn't support the function, so effectively, it can only be used in ARMv8 compilers
#if defined(__aarch64__) && (defined(__clang__) || (defined(__GNUC__) && __GNUC__ >= 9))
# define vst1q_u8_x2_unaligned vst1q_u8_x2
#else
HEDLEY_ALWAYS_INLINE void vst1q_u8_x2_unaligned(uint8_t* p, uint8x16x2_t data) {
	vst1q_u8(p, data.val[0]);
	vst1q_u8(p+16, data.val[1]);
}
#endif


// whether to skew the fast/slow path towards the fast path
// this enables the fast path to handle single character escapes, which means the fast path becomes slower, but it chosen more frequently
#define YENC_NEON_FAST_ONECHAR_MAIN 1
// also apply the same for EOL handling; oddly, I haven't seen this be faster anywhere
//#define YENC_NEON_FAST_ONECHAR_EOL 1


#define _X(n) \
	_X2(n,0), _X2(n,16), _X2(n,1), _X2(n,17), _X2(n,2), _X2(n,18), _X2(n,3), _X2(n,19), \
	_X2(n,4), _X2(n,20), _X2(n,5), _X2(n,21), _X2(n,6), _X2(n,22), _X2(n,7), _X2(n,23), \
	_X2(n,8), _X2(n,24), _X2(n,9), _X2(n,25), _X2(n,10), _X2(n,26), _X2(n,11), _X2(n,27), \
	_X2(n,12), _X2(n,28), _X2(n,13), _X2(n,29), _X2(n,14), _X2(n,30), _X2(n,15), _X2(n,31)
#ifdef __aarch64__
# ifdef YENC_NEON_FAST_ONECHAR_EOL
static const uint8_t ALIGN_TO(16, shufNlIndexLUT[65*32]) = {
#  define _X2(n, k) ((n==1+k)? '=' : 1+k-(1+k>n) - (k>15 ? 16 : 0))
	// need dummy entries for bits that can never be set
	_X(31), _X(30), _X(29), _X(28),
		_X(32),_X(32),_X(32),_X(32),_X(32),_X(32),_X(32),_X(32),
	_X(27), _X(26), _X(25), _X(24),
	_X(23), _X(22), _X(21), _X(20),
		_X(32),_X(32),_X(32),_X(32),_X(32),_X(32),_X(32),_X(32),
	_X(19), _X(18), _X(17), _X(16),
	_X(15), _X(14), _X(13), _X(12),
		_X(32),_X(32),_X(32),_X(32),_X(32),_X(32),_X(32),_X(32),
	_X(11), _X(10), _X( 9), _X( 8),
	_X( 7), _X( 6), _X( 5), _X( 4),
		_X(32),_X(32),_X(32),_X(32),_X(32),_X(32),_X(32),_X(32),
	_X( 3), _X( 2), _X( 1), _X( 0),
	_X(33)
#  undef _X2
};
# endif
#else
# ifdef YENC_NEON_FAST_ONECHAR_EOL
static const uint8_t ALIGN_TO(16, shufNlIndexLUT[33*32]) = {
#  define _X2(n, k) (((n==1+k)? '=' : 1+k-(1+k>n) - (k&~7)))
	_X(31), _X(30), _X(29), _X(28), _X(27), _X(26), _X(25), _X(24),
	_X(23), _X(22), _X(21), _X(20), _X(19), _X(18), _X(17), _X(16),
	_X(15), _X(14), _X(13), _X(12), _X(11), _X(10), _X( 9), _X( 8),
	_X( 7), _X( 6), _X( 5), _X( 4), _X( 3), _X( 2), _X( 1), _X( 0),
	_X(33)
#  undef _X2
};
# endif
#endif
# undef _X

uint8x16_t ALIGN_TO(16, shufLUT[256]);
uint8x16_t ALIGN_TO(16, nlShufLUT[256]);
uint16_t expandLUT[256];

static HEDLEY_ALWAYS_INLINE void encode_eol_handle_pre(const uint8_t* HEDLEY_RESTRICT es, long& i, uint8_t*& p, int& col, int lineSizeOffset) {
	uint8x16_t oDataA = vld1q_u8(es + i);
	uint8x16_t oDataB = vld1q_u8(es + i + sizeof(uint8x16_t));
	uint8x16_t dataA = oDataA;
	uint8x16_t dataB = oDataB;
#ifdef __aarch64__
	uint8x16_t cmpA = vreinterpretq_u8_s8(vqtbx2q_s8(
		vdupq_n_s8('='-42),
		(int8x16x2_t){'\0'-42,-128,-128,'\0'-42,'\t'-42,'\n'-42,'\r'-42,'\t'-42,'\n'-42,'\r'-42,-128,-128,'\0'-42,-128,-128,-128, ' '-42,'\n'-42,'\r'-42,' '-42,-128,-128,-128,-128,-128,-128,'.'-42,-128,-128,-128,'='-42,-128},
		vreinterpretq_u8_s8(vhaddq_s8(vreinterpretq_s8_u8(dataA), (int8x16_t){42,48,66,66, 66,66,66,66, 66,66,66,66, 66,66,66,66}))
	));
	cmpA = vceqq_u8(cmpA, dataA);
	
	dataB = vaddq_u8(oDataB, vdupq_n_u8(42));
	uint8x16_t cmpB = vqtbx1q_u8(
		vceqq_u8(oDataB, vdupq_n_u8('='-42)),
		//            \0                    \n      \r
		(uint8x16_t){255,0,0,0,0,0,0,0,0,0,255,0,0,255,0,0},
		dataB
	);
# ifdef YENC_NEON_FAST_ONECHAR_EOL
	dataA = vaddq_u8(dataA, vbslq_u8(cmpA, vdupq_n_u8(64+42), vdupq_n_u8(42)));
	dataB = vorrq_u8(dataB, vandq_u8(cmpB, vdupq_n_u8(64)));
# endif
#else
	uint8x16_t cmpA = vorrq_u8(
		vorrq_u8(
			vceqq_u8(oDataA, vdupq_n_u8(-42)),
			vceqq_u8(oDataA, vdupq_n_u8('='-42))
		),
		vorrq_u8(
			vceqq_u8(oDataA, vdupq_n_u8('\r'-42)),
			vceqq_u8(oDataA, vdupq_n_u8('\n'-42))
		)
	);
	uint8x16_t cmpB = vorrq_u8(
		vorrq_u8(
			vceqq_u8(oDataB, vdupq_n_u8(-42)),
			vceqq_u8(oDataB, vdupq_n_u8('='-42))
		),
		vorrq_u8(
			vceqq_u8(oDataB, vdupq_n_u8('\r'-42)),
			vceqq_u8(oDataB, vdupq_n_u8('\n'-42))
		)
	);
	
	// dup low 2 bytes & compare
	uint8x8_t firstTwoChars = vreinterpret_u8_u16(vdup_lane_u16(vreinterpret_u16_u8(vget_low_u8(oDataA)), 0));
	uint8x8_t cmpNl = vceq_u8(firstTwoChars, vreinterpret_u8_s8((int8x8_t){
		' '-42,' '-42,'\t'-42,'\t'-42,'\r'-42,'.'-42,'='-42,'='-42
	}));
	// use padd to merge comparisons
	uint16x4_t cmpNl2 = vreinterpret_u16_u8(cmpNl);
	cmpNl2 = vpadd_u16(cmpNl2, vdup_n_u16(0));
	cmpNl2 = vpadd_u16(cmpNl2, vdup_n_u16(0));
	cmpA = vcombine_u8(
		vorr_u8(vget_low_u8(cmpA), vreinterpret_u8_u16(cmpNl2)),
		vget_high_u8(cmpA)
	);
# ifdef YENC_NEON_FAST_ONECHAR_EOL
	dataA = vsubq_u8(dataA, vbslq_u8(cmpA, vdupq_n_u8(-64-42), vdupq_n_u8(-42)));
	dataB = vsubq_u8(dataB, vbslq_u8(cmpB, vdupq_n_u8(-64-42), vdupq_n_u8(-42)));
# endif
#endif
	
	
	uint8x16_t cmpAMasked = vandq_u8(cmpA, (uint8x16_t){1,2,4,8,16,32,64,128, 1,2,4,8,16,32,64,128});
	uint8x16_t cmpBMasked = vandq_u8(cmpB, (uint8x16_t){1,2,4,8,16,32,64,128, 1,2,4,8,16,32,64,128});
#ifdef __aarch64__
# ifdef YENC_NEON_FAST_ONECHAR_EOL
	uint8x16_t cmpMerge = vpaddq_u8(cmpAMasked, cmpBMasked);
	cmpMerge = vpaddq_u8(cmpMerge, cmpMerge);
	uint64_t mask = vgetq_lane_u64(vreinterpretq_u64_u8(cmpMerge), 0);
	if(LIKELIHOOD(0.09, (mask & (mask-1)) != 0 || mask==1)) {
		mask |= mask >> 8;
		uint8x8_t cmpPacked = vpadd_u8(vget_low_u8(cmpMerge), vget_low_u8(cmpMerge));
		uint8_t m1 = (mask & 0xff);
		uint8_t m2 = ((mask >> 16) & 0xff);
		uint8_t m3 = ((mask >> 32) & 0xff);
		uint8_t m4 = ((mask >> 48) & 0xff);
# else
	if (LIKELIHOOD(0.4, vget_lane_u64(vreinterpret_u64_u32(vqmovn_u64(vreinterpretq_u64_u8(
		vorrq_u8(cmpA, cmpB)
	))), 0)!=0)) {
		uint8x16_t cmpMerge = vpaddq_u8(cmpAMasked, cmpBMasked);
		cmpMerge = vpaddq_u8(cmpMerge, cmpMerge);
		uint8x8_t cmpPacked = vpadd_u8(vget_low_u8(cmpMerge), vget_low_u8(cmpMerge));
		uint32_t mask = vget_lane_u32(vreinterpret_u32_u8(cmpPacked), 0);
		uint8_t m1 = (mask & 0xff);
		uint8_t m2 = ((mask >> 8) & 0xff);
		uint8_t m3 = ((mask >> 16) & 0xff);
		uint8_t m4 = ((mask >> 24) & 0xff);
# endif
#else
	// no vpaddq_u8 in ARMv7, so need extra 64-bit VPADD
	uint8x8_t cmpPacked = vpadd_u8(
		vpadd_u8(
			vget_low_u8(cmpAMasked), vget_high_u8(cmpAMasked)
		),
		vpadd_u8(
			vget_low_u8(cmpBMasked), vget_high_u8(cmpBMasked)
		)
	);
	cmpPacked = vpadd_u8(cmpPacked, cmpPacked);
	uint32_t mask = vget_lane_u32(vreinterpret_u32_u8(cmpPacked), 0);
# ifdef YENC_NEON_FAST_ONECHAR_EOL
	if(LIKELIHOOD(0.09, (mask & (mask-1)) != 0 || mask==1))
# else
	if(LIKELIHOOD(0.4, mask != 0))
# endif
	{
		uint8_t m1 = (mask & 0xff);
		uint8_t m2 = ((mask >> 8) & 0xff);
		uint8_t m3 = ((mask >> 16) & 0xff);
		uint8_t m4 = ((mask >> 24) & 0xff);
#endif
		
		// perform lookup for shuffle mask
		uint8x16_t shuf2 = vld1q_u8((uint8_t*)(shufLUT + m2));
		uint8x16_t shuf3 = vld1q_u8((uint8_t*)(shufLUT + m3));
		uint8x16_t shuf4 = vld1q_u8((uint8_t*)(shufLUT + m4));
		
		uint8x16_t shuf1 = vld1q_u8((uint8_t*)(nlShufLUT + m1));
		uint8x16_t data1A = vqsubq_u8(shuf1, vdupq_n_u8(64));
#ifdef __aarch64__
# ifndef YENC_NEON_FAST_ONECHAR_EOL
		dataA = vaddq_u8(dataA, vbslq_u8(cmpA, vdupq_n_u8(64+42), vdupq_n_u8(42)));
		dataB = vorrq_u8(dataB, vandq_u8(cmpB, vdupq_n_u8(64)));
# endif
		data1A = vqtbx1q_u8(data1A, dataA, shuf1);
		uint8x16_t data2A = vqtbx1q_u8(shuf2, vextq_u8(dataA, dataA, 8), shuf2);
		uint8x16_t data1B = vqtbx1q_u8(shuf3, dataB, shuf3);
		uint8x16_t data2B = vqtbx1q_u8(shuf4, vextq_u8(dataB, dataB, 8), shuf4);
#else
# ifndef YENC_NEON_FAST_ONECHAR_EOL
		dataA = vsubq_u8(dataA, vbslq_u8(cmpA, vdupq_n_u8(-64-42), vdupq_n_u8(-42)));
		dataB = vsubq_u8(dataB, vbslq_u8(cmpB, vdupq_n_u8(-64-42), vdupq_n_u8(-42)));
# endif
		data1A = vcombine_u8(vtbx1_u8(vget_low_u8(data1A),  vget_low_u8(dataA), vget_low_u8(shuf1)),
		                     vtbx1_u8(vget_high_u8(data1A), vget_low_u8(dataA), vget_high_u8(shuf1)));
		uint8x8_t shuf2l = vget_low_u8(shuf2);
		uint8x8_t shuf2h = vget_high_u8(shuf2);
		uint8x8_t shuf3l = vget_low_u8(shuf3);
		uint8x8_t shuf3h = vget_high_u8(shuf3);
		uint8x8_t shuf4l = vget_low_u8(shuf4);
		uint8x8_t shuf4h = vget_high_u8(shuf4);
		uint8x16_t data2A = vcombine_u8(vtbx1_u8(shuf2l, vget_high_u8(dataA), shuf2l),
		                                vtbx1_u8(shuf2h, vget_high_u8(dataA), shuf2h));
		uint8x16_t data1B = vcombine_u8(vtbx1_u8(shuf3l, vget_low_u8(dataB), shuf3l),
		                                vtbx1_u8(shuf3h, vget_low_u8(dataB), shuf3h));
		uint8x16_t data2B = vcombine_u8(vtbx1_u8(shuf4l, vget_high_u8(dataB), shuf4l),
		                                vtbx1_u8(shuf4h, vget_high_u8(dataB), shuf4h));
#endif
		uint32_t counts = vget_lane_u32(vreinterpret_u32_u8(vcnt_u8(cmpPacked)), 0);
		counts += 0x0808080A;
		
		unsigned char shuf1Len = counts & 0xff;
		unsigned char shuf2Len = (counts>>8) & 0xff;
		unsigned char shuf3Len = (counts>>16) & 0xff;
		unsigned char shuf4Len = (counts>>24) & 0xff;
		uint16_t shufTotalLen = (counts>>16) + counts;
		shufTotalLen += shufTotalLen>>8;
		shufTotalLen &= 0xff;
		
		if(LIKELIHOOD(0.001, shuf1Len > sizeof(uint8x16_t))) {
			// unlikely special case, which would cause vectors to be overflowed
			// we'll just handle this by only dealing with the first 2 characters, and let main loop handle the rest
			// at least one of the first 2 chars is guaranteed to need escaping
			vst1_u8(p, vget_low_u8(data1A));
			col = lineSizeOffset + ((m1 & 2)>>1) - 32+2;
			p += 4 + (m1 & 1) + ((m1 & 2)>>1);
			i += 2;
			return;
		}
		
		vst1q_u8(p, data1A);
		p += shuf1Len;
		vst1q_u8(p, data2A);
		p += shuf2Len;
		vst1q_u8(p, data1B);
		p += shuf3Len;
		vst1q_u8(p, data2B);
		p += shuf4Len;
		col = shufTotalLen-2 - (m1&1) + lineSizeOffset-32;
	} else {
#ifdef YENC_NEON_FAST_ONECHAR_EOL
		// write out first char + newline
		uint32_t firstChar = vgetq_lane_u8(dataA, 0);
		firstChar |= 0x0a0d00;
		memcpy(p, &firstChar, sizeof(firstChar));
		p += 3;
		
		// shuffle stuff up
# ifdef __aarch64__
#  ifdef _MSC_VER
		long bitIndex;
		if(_BitScanReverse64(&bitIndex, mask))
			bitIndex ^= 63;
		else
			bitIndex = 64;
#  else
		long bitIndex = __builtin_clzll(mask);
#  endif
# else
#  ifdef __GNUC__
		long bitIndex = __builtin_clz(mask); // TODO: is the 'undefined if 0' case problematic here?
#  elif defined(_MSC_VER)
		long bitIndex = _arm_clz(mask);
#  else
		long bitIndex = __clz(mask); // ARM compiler?
#  endif
# endif
		
		
		uint8x16x2_t shuf = vld2q_u8(shufNlIndexLUT + bitIndex*32);
		
# ifdef __aarch64__
		uint8x16_t shufA = shuf.val[0];
		uint8x16_t shufB = shuf.val[1];
		dataA = vqtbx2q_u8(shufA, {dataA, dataB}, shufA);
		//dataA = vqtbx1q_u8(shufA, vextq_u8(dataA, dataB, 1), shufA); // TODO: appears to be faster, so make this work
		dataB = vqtbx1q_u8(shufB, dataB, shufB);
# else
		uint8x8_t shufAl = vget_low_u8(shuf.val[0]);
		uint8x8_t shufAh = vget_high_u8(shuf.val[0]);
		uint8x8_t shufBl = vget_low_u8(shuf.val[1]);
		uint8x8_t shufBh = vget_high_u8(shuf.val[1]);
		dataA = vcombine_u8(
			vtbx2_u8(shufAl, {vget_low_u8(dataA), vget_high_u8(dataA)}, shufAl),
			vtbx2_u8(shufAh, {vget_high_u8(dataA), vget_low_u8(dataB)}, shufAh)
		);
		dataB = vcombine_u8(
			vtbx2_u8(shufBl, {vget_low_u8(dataB), vget_high_u8(dataB)}, shufBl),
			vtbx1_u8(shufBh, vget_high_u8(dataB), shufBh)
		);
# endif
		vst1q_u8_x2_unaligned(p, ((uint8x16x2_t){dataA, dataB}));
		p += sizeof(uint8x16_t)*2 - 1;
		p += (mask != 0);
		col = lineSizeOffset + (mask != 0);
#else
		uint8x16_t data1;
# ifdef __aarch64__
		dataA = vaddq_u8(dataA, vdupq_n_u8(42));
		data1 = vqtbx1q_u8((uint8x16_t){0,'\r','\n',0,0,0,0,0,0,0,0,0,0,0,0,0}, dataA, (uint8x16_t){0,16,16,1,2,3,4,5,6,7,8,9,10,11,12,13});
# else
		dataA = vsubq_u8(dataA, vdupq_n_u8(-42));
		dataB = vsubq_u8(dataB, vdupq_n_u8(-42));
		data1 = vcombine_u8(
			vtbx1_u8((uint8x8_t){0,'\r','\n',0,0,0,0,0}, vget_low_u8(dataA), (uint8x8_t){0,16,16,1,2,3,4,5}),
			vext_u8(vget_low_u8(dataA), vget_high_u8(dataA), 6)
		);
# endif
		vst1q_u8(p, data1);
		p += sizeof(uint8x16_t);
		uint16_t v = vgetq_lane_u16(vreinterpretq_u16_u8(dataA), 7);
		memcpy(p, &v, sizeof(v));
		p += sizeof(v);
		vst1q_u8(p, dataB);
		p += sizeof(uint8x16_t);
		col = lineSizeOffset;
#endif
	}
	
	i += sizeof(uint8x16_t)*2;
	// TODO: check col >= 0 if we want to support short lines
}


HEDLEY_ALWAYS_INLINE void do_encode_neon(int line_size, int* colOffset, const uint8_t* HEDLEY_RESTRICT srcEnd, uint8_t* HEDLEY_RESTRICT& dest, size_t& len) {
	if(len < sizeof(uint8x16_t)*4 || line_size < (int)sizeof(uint8x16_t)*4) return;
	
	uint8_t *p = dest; // destination pointer
	long i = -(long)len; // input position
	int lineSizeOffset = -line_size +32; // line size plus vector length
	int col = *colOffset - line_size +1;
	
	// offset position to enable simpler loop condition checking
	const int INPUT_OFFSET = sizeof(uint8x16_t)*4 -1; // extra chars for EOL handling, -1 to change <= to <
	i += INPUT_OFFSET;
	const uint8_t* es = srcEnd - INPUT_OFFSET;
	
	if (LIKELIHOOD(0.999, col == -line_size+1)) {
		uint8_t c = es[i++];
		if (LIKELIHOOD(0.0273, escapedLUT[c] != 0)) {
			memcpy(p, escapedLUT + c, 2);
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
				uint32_t v = UINT32_16_PACK(UINT16_PACK('\r', '\n'), (uint32_t)escapedLUT[c]);
				memcpy(p, &v, sizeof(v));
				p += 4;
				col = 2-line_size + 1;
			} else {
				uint32_t v = UINT32_PACK('\r', '\n', (uint32_t)(c+42), 0);
				memcpy(p, &v, sizeof(v));
				p += 3;
				col = 2-line_size;
			}
		}
	}
	while(i < 0) {
		// for unaligned loads, separate loads seem to be faster than vld1q_u8_x2 on Cortex A53; unsure if this applies elsewhere
		uint8x16_t dataA = vld1q_u8(es + i);
		uint8x16_t dataB = vld1q_u8(es + i + sizeof(uint8x16_t));
		i += sizeof(uint8x16_t)*2;
		// search for special chars
#ifdef __aarch64__
		uint8x16_t cmpEqA = vceqq_u8(dataA, vdupq_n_u8('='-42));
		uint8x16_t cmpEqB = vceqq_u8(dataB, vdupq_n_u8('='-42));
		dataA = vaddq_u8(dataA, vdupq_n_u8(42));
		dataB = vaddq_u8(dataB, vdupq_n_u8(42));
		uint8x16_t cmpA = vqtbx1q_u8(
			cmpEqA,
			//            \0                    \n      \r
			(uint8x16_t){255,0,0,0,0,0,0,0,0,0,255,0,0,255,0,0},
			dataA
		);
		uint8x16_t cmpB = vqtbx1q_u8(
			cmpEqB,
			//            \0                    \n      \r
			(uint8x16_t){255,0,0,0,0,0,0,0,0,0,255,0,0,255,0,0},
			dataB
		);
		
# ifdef YENC_NEON_FAST_ONECHAR_MAIN
		dataA = vorrq_u8(dataA, vandq_u8(cmpA, vdupq_n_u8(64)));
		dataB = vorrq_u8(dataB, vandq_u8(cmpB, vdupq_n_u8(64)));
# endif
#else
		// the ARMv8 strategy may be worth it here with 2x vtbx2's, but both GCC-9 and Clang-9 generate poor assembly for it, so it performs worse than the following
		uint8x16_t cmpA = vorrq_u8(
			vorrq_u8(
				vceqq_u8(dataA, vdupq_n_u8(-42)),
				vceqq_u8(dataA, vdupq_n_u8('='-42))
			),
			vorrq_u8(
				vceqq_u8(dataA, vdupq_n_u8('\r'-42)),
				vceqq_u8(dataA, vdupq_n_u8('\n'-42))
			)
		);
		uint8x16_t cmpB = vorrq_u8(
			vorrq_u8(
				vceqq_u8(dataB, vdupq_n_u8(-42)),
				vceqq_u8(dataB, vdupq_n_u8('='-42))
			),
			vorrq_u8(
				vceqq_u8(dataB, vdupq_n_u8('\r'-42)),
				vceqq_u8(dataB, vdupq_n_u8('\n'-42))
			)
		);
		
# ifdef YENC_NEON_FAST_ONECHAR_MAIN
		dataA = vsubq_u8(dataA, vbslq_u8(cmpA, vdupq_n_u8(-64-42), vdupq_n_u8(-42)));
		dataB = vsubq_u8(dataB, vbslq_u8(cmpB, vdupq_n_u8(-64-42), vdupq_n_u8(-42)));
# endif
#endif
		
		
#ifdef YENC_NEON_FAST_ONECHAR_MAIN
		long bitIndex; // prevent compiler whining
#endif
		uint8x16_t cmpAMasked = vandq_u8(cmpA, (uint8x16_t){1,2,4,8,16,32,64,128, 1,2,4,8,16,32,64,128});
		uint8x16_t cmpBMasked = vandq_u8(cmpB, (uint8x16_t){1,2,4,8,16,32,64,128, 1,2,4,8,16,32,64,128});
#ifdef __aarch64__
# ifdef YENC_NEON_FAST_ONECHAR_MAIN
		uint8x16_t cmpMerge = vpaddq_u8(cmpAMasked, cmpBMasked);
		cmpMerge = vpaddq_u8(cmpMerge, cmpMerge);
		uint64_t mask = vgetq_lane_u64(vreinterpretq_u64_u8(cmpMerge), 0);
		if(LIKELIHOOD(0.09, (mask & (mask-1)) != 0)) {
			mask |= mask >> 8;
			uint8x8_t cmpPacked = vpadd_u8(vget_low_u8(cmpMerge), vget_low_u8(cmpMerge));
			uint8_t m1 = (mask & 0xff);
			uint8_t m2 = ((mask >> 16) & 0xff);
			uint8_t m3 = ((mask >> 32) & 0xff);
			uint8_t m4 = ((mask >> 48) & 0xff);
# else
		if (LIKELIHOOD(0.4, vget_lane_u64(vreinterpret_u64_u32(vqmovn_u64(vreinterpretq_u64_u8(
			vorrq_u8(cmpA, cmpB)
		))), 0)!=0)) {
			uint8x16_t cmpMerge = vpaddq_u8(cmpAMasked, cmpBMasked);
			uint8x8_t cmpPacked = vget_low_u8(vpaddq_u8(cmpMerge, cmpMerge));
			cmpPacked = vpadd_u8(cmpPacked, cmpPacked);
			uint32_t mask = vget_lane_u32(vreinterpret_u32_u8(cmpPacked), 0);
			uint8_t m1 = (mask & 0xff);
			uint8_t m2 = ((mask >> 8) & 0xff);
			uint8_t m3 = ((mask >> 16) & 0xff);
			uint8_t m4 = ((mask >> 24) & 0xff);
# endif
#else
		// no vpaddq_u8 in ARMv7, so need extra 64-bit VPADD
		uint8x8_t cmpPacked = vpadd_u8(
			vpadd_u8(
				vget_low_u8(cmpAMasked), vget_high_u8(cmpAMasked)
			),
			vpadd_u8(
				vget_low_u8(cmpBMasked), vget_high_u8(cmpBMasked)
			)
		);
		cmpPacked = vpadd_u8(cmpPacked, cmpPacked);
		uint32_t mask = vget_lane_u32(vreinterpret_u32_u8(cmpPacked), 0);
# ifdef YENC_NEON_FAST_ONECHAR_MAIN
		if(LIKELIHOOD(0.09, (mask & (mask-1)) != 0))
# else
		if(LIKELIHOOD(0.4, mask != 0))
# endif
		{
			uint8_t m1 = (mask & 0xff);
			uint8_t m2 = ((mask >> 8) & 0xff);
			uint8_t m3 = ((mask >> 16) & 0xff);
			uint8_t m4 = ((mask >> 24) & 0xff);
#endif
			
			// perform lookup for shuffle mask
			uint8x16_t shuf1 = vld1q_u8((uint8_t*)(shufLUT + m1));
			uint8x16_t shuf2 = vld1q_u8((uint8_t*)(shufLUT + m2));
			uint8x16_t shuf3 = vld1q_u8((uint8_t*)(shufLUT + m3));
			uint8x16_t shuf4 = vld1q_u8((uint8_t*)(shufLUT + m4));
			
			// expand halves
#ifdef __aarch64__
# ifndef YENC_NEON_FAST_ONECHAR_MAIN
			dataA = vorrq_u8(dataA, vandq_u8(cmpA, vdupq_n_u8(64)));
			dataB = vorrq_u8(dataB, vandq_u8(cmpB, vdupq_n_u8(64)));
# endif
			uint8x16_t data1A = vqtbx1q_u8(shuf1, dataA, shuf1);
			uint8x16_t data2A = vqtbx1q_u8(shuf2, vextq_u8(dataA, dataA, 8), shuf2);
			uint8x16_t data1B = vqtbx1q_u8(shuf3, dataB, shuf3);
			uint8x16_t data2B = vqtbx1q_u8(shuf4, vextq_u8(dataB, dataB, 8), shuf4);
#else
# ifndef YENC_NEON_FAST_ONECHAR_MAIN
			dataA = vsubq_u8(dataA, vbslq_u8(cmpA, vdupq_n_u8(-64-42), vdupq_n_u8(-42)));
			dataB = vsubq_u8(dataB, vbslq_u8(cmpB, vdupq_n_u8(-64-42), vdupq_n_u8(-42)));
# endif
			uint8x8_t shuf1l = vget_low_u8(shuf1);
			uint8x8_t shuf1h = vget_high_u8(shuf1);
			uint8x8_t shuf2l = vget_low_u8(shuf2);
			uint8x8_t shuf2h = vget_high_u8(shuf2);
			uint8x8_t shuf3l = vget_low_u8(shuf3);
			uint8x8_t shuf3h = vget_high_u8(shuf3);
			uint8x8_t shuf4l = vget_low_u8(shuf4);
			uint8x8_t shuf4h = vget_high_u8(shuf4);
			uint8x16_t data1A = vcombine_u8(vtbx1_u8(shuf1l, vget_low_u8(dataA), shuf1l),
			                                vtbx1_u8(shuf1h, vget_low_u8(dataA), shuf1h));
			uint8x16_t data2A = vcombine_u8(vtbx1_u8(shuf2l, vget_high_u8(dataA), shuf2l),
			                                vtbx1_u8(shuf2h, vget_high_u8(dataA), shuf2h));
			uint8x16_t data1B = vcombine_u8(vtbx1_u8(shuf3l, vget_low_u8(dataB), shuf3l),
			                                vtbx1_u8(shuf3h, vget_low_u8(dataB), shuf3h));
			uint8x16_t data2B = vcombine_u8(vtbx1_u8(shuf4l, vget_high_u8(dataB), shuf4l),
			                                vtbx1_u8(shuf4h, vget_high_u8(dataB), shuf4h));
#endif
			
			// store out
			uint32_t counts = vget_lane_u32(vreinterpret_u32_u8(vcnt_u8(cmpPacked)), 0);
			counts += 0x08080808;
			
			unsigned char shuf1Len = counts & 0xff;
			unsigned char shuf2Len = (counts>>8) & 0xff;
			unsigned char shuf3Len = (counts>>16) & 0xff;
			unsigned char shuf4Len = (counts>>24) & 0xff;
			uint16_t shufTotalLen = (counts>>16) + counts;
			shufTotalLen += shufTotalLen>>8;
			shufTotalLen &= 0xff;
			
			vst1q_u8(p, data1A);
			p += shuf1Len;
			vst1q_u8(p, data2A);
			p += shuf2Len;
			vst1q_u8(p, data1B);
			p += shuf3Len;
			vst1q_u8(p, data2B);
			p += shuf4Len;
			col += shufTotalLen;
			
			if(LIKELIHOOD(0.3, col >= 0)) {
				// we overflowed - find correct position to revert back to
				long revert = col;
				long len2ndHalf = shuf3Len+shuf4Len;
				long pastMid = col - len2ndHalf;
				uint32_t eqMaskHalf;
				if(HEDLEY_UNLIKELY(pastMid >= 0)) {
					eqMaskHalf = (expandLUT[m2] << shuf1Len) | expandLUT[m1];
					eqMaskHalf >>= shufTotalLen - col -1;
					i += len2ndHalf - 16;
				} else {
					eqMaskHalf = (expandLUT[m4] << shuf3Len) | expandLUT[m3];
					eqMaskHalf >>= -pastMid -1; // == ~pastMid
				}
				revert += eqMaskHalf & 1;
				
				// count bits in eqMask
				uint8x8_t vCnt = vcnt_u8(vreinterpret_u8_u32(vmov_n_u32(eqMaskHalf)));
				uint32_t cnt = vget_lane_u32(vreinterpret_u32_u8(vCnt), 0);
				cnt += cnt >> 16;
				cnt += cnt >> 8;
				cnt &= 0xff;
				i += cnt;
				
				p -= revert;
				i -= revert;
				goto _encode_eol_handle_pre;
			}
		} else {
			{
#ifdef YENC_NEON_FAST_ONECHAR_MAIN
# ifdef __aarch64__
#  ifdef _MSC_VER
				// does this work?
				if(_BitScanReverse64(&bitIndex, mask))
					bitIndex ^= 63;
				else
					bitIndex = 64;
#  else
				bitIndex = __builtin_clzll(mask); // TODO: is the 'undefined if 0' case problematic here?
#  endif
# else
#  ifdef __GNUC__
				bitIndex = __builtin_clz(mask);
#  elif defined(_MSC_VER)
				bitIndex = _arm_clz(mask);
#  else
				bitIndex = __clz(mask); // ARM compiler?
#  endif
# endif
				
				uint8x16_t vClz = vdupq_n_u8(bitIndex & ~(sizeof(mask)*8));
# ifdef __aarch64__
				uint8x16_t blendA = vcgeq_u8((uint8x16_t){63,62,61,60,51,50,49,48,47,46,45,44,35,34,33,32}, vClz);
				uint8x16_t blendB = vcgeq_u8((uint8x16_t){31,30,29,28,19,18,17,16,15,14,13,12, 3, 2, 1, 0}, vClz);
# else
				uint8x16_t blendA = vcgeq_u8((uint8x16_t){31,30,29,28,27,26,25,24,23,22,21,20,19,18,17,16}, vClz);
				uint8x16_t blendB = vcgeq_u8((uint8x16_t){15,14,13,12,11,10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0}, vClz);
# endif
				uint8x16_t dataAShifted = vextq_u8(dataA, dataA, 15);
				uint8x16_t dataBShifted = vextq_u8(dataA, dataB, 15);
				dataA = vbslq_u8(cmpA, vdupq_n_u8('='), dataA);
				uint8x16_t outDataB = vbslq_u8(cmpB, vdupq_n_u8('='), dataB);
				dataA = vbslq_u8(blendA, dataA, dataAShifted);
				outDataB = vbslq_u8(blendB, outDataB, dataBShifted);
				
				vst1q_u8_x2_unaligned(p, ((uint8x16x2_t){dataA, outDataB}));
				p += sizeof(uint8x16_t)*2;
				// write last byte
				*p = vgetq_lane_u8(dataB, 15);
				p += (mask != 0);
				col += (mask != 0);
#else
# ifndef __aarch64__
				dataA = vsubq_u8(dataA, vdupq_n_u8(-42));
				dataB = vsubq_u8(dataB, vdupq_n_u8(-42));
# endif
				vst1q_u8_x2_unaligned(p, ((uint8x16x2_t){dataA, dataB}));
				p += sizeof(uint8x16_t)*2;
#endif
				col += sizeof(uint8x16_t)*2;
			}
			
			if(HEDLEY_UNLIKELY(col >= 0)) {
#ifdef YENC_NEON_FAST_ONECHAR_MAIN
# ifdef __aarch64__
				// fixup bitIndex
				bitIndex -= ((bitIndex+4)>>4)<<3;
# endif
				bitIndex = bitIndex +1;
				if(HEDLEY_UNLIKELY(col == bitIndex)) {
					// this is an escape character, so line will need to overflow
					p--;
				} else {
					i += (col > bitIndex);
				}
#endif
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
				res[j+p] = '=';
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
