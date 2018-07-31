#include "common.h"

#ifdef __SSSE3__
#include "decoder_sse_base.h"
#include "decoder_common.h"
void decoder_set_ssse3_funcs() {
	decoder_init_lut();
	_do_decode = &do_decode_simd<false, false, sizeof(__m128i), do_decode_sse<false, false, ISA_LEVEL_SSSE3> >;
	_do_decode_raw = &do_decode_simd<true, false, sizeof(__m128i), do_decode_sse<true, false, ISA_LEVEL_SSSE3> >;
	_do_decode_end = &do_decode_simd<false, true, sizeof(__m128i), do_decode_sse<false, true, ISA_LEVEL_SSSE3> >;
	_do_decode_end_raw = &do_decode_simd<true, true, sizeof(__m128i), do_decode_sse<true, true, ISA_LEVEL_SSSE3> >;
}
#else
void decoder_set_sse2_funcs();
void decoder_set_ssse3_funcs() {
	decoder_set_sse2_funcs();
}
#endif
