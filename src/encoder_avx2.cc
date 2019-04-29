#include "common.h"

#if defined(__AVX2__) && defined(YENC_ENABLE_AVX256) && YENC_ENABLE_AVX256!=0
#include "encoder_avx_base.h"

void encoder_avx2_init(const unsigned char* _escapeLUT, const uint16_t* _escapedLUT) {
	escapeLUT = _escapeLUT;
	escapedLUT = _escapedLUT;
	
	_do_encode = &do_encode_avx2<ISA_LEVEL_AVX>;
	encoder_avx2_lut();
}
#else
void encoder_avx_init(const unsigned char*, const uint16_t*);
void encoder_avx2_init(const unsigned char* _escapeLUT, const uint16_t* _escapedLUT) {
	encoder_avx_init(_escapeLUT, _escapedLUT);
}
#endif

