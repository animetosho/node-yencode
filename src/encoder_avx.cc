#include "common.h"

#if defined(__AVX__) && defined(__POPCNT__)
#include "encoder_sse_base.h"

void encoder_avx_init() {
	_do_encode = &do_encode_simd< do_encode_sse<ISA_LEVEL_AVX> >;
	encoder_sse_lut();
}
#else
void encoder_ssse3_init();
void encoder_avx_init() {
	encoder_ssse3_init();
}
#endif

