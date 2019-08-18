#include "common.h"

#if defined(__AVX2__) && defined(YENC_ENABLE_AVX256) && YENC_ENABLE_AVX256!=0
#include "encoder_avx_base.h"

void encoder_avx2_init() {
	_do_encode = &do_encode_simd< do_encode_avx2<ISA_LEVEL_AVX> >;
	encoder_avx2_lut();
}
#else
void encoder_avx_init();
void encoder_avx2_init() {
	encoder_avx_init();
}
#endif

