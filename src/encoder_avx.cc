#include "common.h"

#if defined(__AVX__) && defined(__POPCNT__)
#include "encoder_sse_base.h"

void encoder_avx_init() {
	_do_encode = &do_encode_simd< do_encode_sse<ISA_LEVEL_SSE4_POPCNT> >;
	encoder_sse_lut<ISA_LEVEL_SSE4_POPCNT>();
	_encode_isa = ISA_LEVEL_AVX;
}
#else
void encoder_ssse3_init();
void encoder_avx_init() {
	encoder_ssse3_init();
}
#endif

