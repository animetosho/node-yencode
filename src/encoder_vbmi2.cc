#include "common.h"

extern const bool encoder_has_avx10;
#if !defined(__EVEX512__) && (defined(__AVX10_1__) || defined(__EVEX256__)) && defined(__AVX512VL__) && defined(__AVX512VBMI2__) && defined(__AVX512BW__)
const bool encoder_has_avx10 = true;
#else
const bool encoder_has_avx10 = false;
#endif

#if defined(__AVX512VL__) && defined(__AVX512VBMI2__) && defined(__AVX512BW__)
# ifndef YENC_DISABLE_AVX256
#  include "encoder_avx_base.h"

void encoder_vbmi2_init() {
	_do_encode = &do_encode_simd< do_encode_avx2<ISA_LEVEL_VBMI2> >;
	encoder_avx2_lut<ISA_LEVEL_VBMI2>();
	_encode_isa = ISA_LEVEL_VBMI2;
}
# else
#  include "encoder_sse_base.h"
void encoder_vbmi2_init() {
	_do_encode = &do_encode_simd< do_encode_sse<ISA_LEVEL_VBMI2> >;
	encoder_sse_lut<ISA_LEVEL_VBMI2>();
	_encode_isa = ISA_LEVEL_VBMI2;
}
# endif
#else
void encoder_avx2_init();
void encoder_vbmi2_init() {
	encoder_avx2_init();
}
#endif
