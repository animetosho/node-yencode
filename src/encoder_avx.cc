#include "common.h"

#if defined(__AVX__) && defined(__POPCNT__)
#include "encoder_sse_base.h"

void encoder_avx_init(const unsigned char* _escapeLUT, const uint16_t* _escapedLUT) {
	escapeLUT = _escapeLUT;
	escapedLUT = _escapedLUT;
	
	_do_encode = &do_encode_sse<ISA_LEVEL_AVX>;
	encoder_ssse3_lut();
}
#else
void encoder_ssse3_init(const unsigned char*, const uint16_t*);
void encoder_avx_init(const unsigned char* _escapeLUT, const uint16_t* _escapedLUT) {
	encoder_ssse3_init(_escapeLUT, _escapedLUT);
}
#endif

