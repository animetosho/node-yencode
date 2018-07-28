#include "common.h"

// slightly faster version which improves the worst case scenario significantly; since worst case doesn't happen often, overall speedup is relatively minor
// requires PSHUFB (SSSE3) instruction, but will use POPCNT (SSE4.2 (or AMD's ABM, but Phenom doesn't support SSSE3 so doesn't matter)) if available (these only seem to give minor speedups, so considered optional)
#ifdef __SSSE3__
#include "encoder_sse_base.h"

void encoder_ssse3_init(const unsigned char* _escapeLUT, const uint16_t* _escapedLUT) {
	escapeLUT = _escapeLUT;
	escapedLUT = _escapedLUT;
	
	_do_encode = &do_encode_sse<ISA_LEVEL_SSSE3>;
	// generate shuf LUT
	for(int i=0; i<256; i++) {
		int k = i;
		uint8_t res[16];
		uint16_t expand = 0;
		int p = 0;
		for(int j=0; j<8; j++) {
			if(k & 1) {
				res[j+p] = 0xf0 + j;
				p++;
			}
			res[j+p] = j;
			expand |= 1<<(j+p);
			k >>= 1;
		}
		for(; p<8; p++)
			res[8+p] = 8+p +0x80; // +0x80 causes PSHUFB to 0 discarded entries; has no effect other than to ease debugging
		
		__m128i shuf = _mm_loadu_si128((__m128i*)res);
		_mm_store_si128(shufLUT + i, shuf);
		expandLUT[i] = expand;
		
		// calculate add mask for mixing escape chars in
		__m128i maskEsc = _mm_cmpeq_epi8(_mm_and_si128(shuf, _mm_set1_epi8(0xf0)), _mm_set1_epi8(0xf0));
		__m128i addMask = _mm_and_si128(_mm_slli_si128(maskEsc, 1), _mm_set1_epi8(64));
		addMask = _mm_or_si128(addMask, _mm_and_si128(maskEsc, _mm_set1_epi8('=')));
		
		_mm_store_si128(shufMixLUT + i, addMask);
	}
	// underflow guard entries; this may occur when checking for escaped characters, when the shufLUT[0] and shufLUT[-1] are used for testing
	_mm_store_si128(_shufLUT +0, _mm_set1_epi8(0xFF));
	_mm_store_si128(_shufLUT +1, _mm_set1_epi8(0xFF));
}
#else
void encoder_sse2_init(const unsigned char*, const uint16_t*);
void encoder_ssse3_init(const unsigned char*, const uint16_t*) {
	encoder_sse2_init();
}
#endif

