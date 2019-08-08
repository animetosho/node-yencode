#include "common.h"

#ifdef __SSE2__
#include "encoder_sse_base.h"

void encoder_sse2_init(const unsigned char* _escapeLUT, const uint16_t* _escapedLUT) {
	escapeLUT = _escapeLUT;
	escapedLUT = _escapedLUT;
	_do_encode = &do_encode_sse<ISA_LEVEL_SSE2>;
	encoder_sse_lut();
}
#else
void encoder_sse2_init(const unsigned char*, const uint16_t*) {}
#endif

