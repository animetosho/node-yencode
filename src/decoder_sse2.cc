#include "common.h"

#ifdef __SSE2__
#include "decoder_sse_base.h"
#include "decoder_common.h"

void decoder_set_sse2_funcs() {
	decoder_init_lut();
	_do_decode = &do_decode_simd<false, false, sizeof(__m128i), do_decode_sse<false, false, false> >;
	_do_decode_raw = &do_decode_simd<true, false, sizeof(__m128i), do_decode_sse<true, false, false> >;
	_do_decode_end = &do_decode_simd<false, true, sizeof(__m128i), do_decode_sse<false, true, false> >;
	_do_decode_end_raw = &do_decode_simd<true, true, sizeof(__m128i), do_decode_sse<true, true, false> >;
}
#else
void decoder_set_sse2_funcs() {}
#endif
