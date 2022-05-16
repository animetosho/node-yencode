#include "common.h"

#include "decoder_common.h"
#include "decoder.h"

extern "C" {
	YencDecoderEnd (*_do_decode)(const unsigned char*HEDLEY_RESTRICT*, unsigned char*HEDLEY_RESTRICT*, size_t, YencDecoderState*) = &do_decode_scalar<false, false>;
	YencDecoderEnd (*_do_decode_raw)(const unsigned char*HEDLEY_RESTRICT*, unsigned char*HEDLEY_RESTRICT*, size_t, YencDecoderState*) = &do_decode_scalar<true, false>;
	YencDecoderEnd (*_do_decode_end_raw)(const unsigned char*HEDLEY_RESTRICT*, unsigned char*HEDLEY_RESTRICT*, size_t, YencDecoderState*) = &do_decode_end_scalar<true>;
}

void decoder_set_sse2_funcs();
void decoder_set_ssse3_funcs();
void decoder_set_avx_funcs();
void decoder_set_avx2_funcs();
void decoder_set_vbmi2_funcs();
void decoder_set_neon_funcs();


#if defined(PLATFORM_X86) && defined(YENC_BUILD_NATIVE) && YENC_BUILD_NATIVE!=0
# if defined(__AVX2__) && !defined(YENC_DISABLE_AVX256)
#  include "decoder_avx2_base.h"
static inline void decoder_set_native_funcs() {
	ALIGN_ALLOC(lookups, sizeof(*lookups), 16);
	decoder_init_lut(lookups->eqFix, lookups->compact);
	_do_decode = &do_decode_simd<false, false, sizeof(__m256i)*2, do_decode_avx2<false, false, ISA_NATIVE> >;
	_do_decode_raw = &do_decode_simd<true, false, sizeof(__m256i)*2, do_decode_avx2<true, false, ISA_NATIVE> >;
	_do_decode_end_raw = &do_decode_simd<true, true, sizeof(__m256i)*2, do_decode_avx2<true, true, ISA_NATIVE> >;
}
# else
#  include "decoder_sse_base.h"
static inline void decoder_set_native_funcs() {
	decoder_sse_init();
	decoder_init_lut(lookups->eqFix, lookups->compact);
	_do_decode = &do_decode_simd<false, false, sizeof(__m128i)*2, do_decode_sse<false, false, ISA_NATIVE> >;
	_do_decode_raw = &do_decode_simd<true, false, sizeof(__m128i)*2, do_decode_sse<true, false, ISA_NATIVE> >;
	_do_decode_end_raw = &do_decode_simd<true, true, sizeof(__m128i)*2, do_decode_sse<true, true, ISA_NATIVE> >;
}
# endif
#endif

void decoder_init() {
#ifdef PLATFORM_X86
# if defined(YENC_BUILD_NATIVE) && YENC_BUILD_NATIVE!=0
	decoder_set_native_funcs();
# else
	int use_isa = cpu_supports_isa();
	if(use_isa >= ISA_LEVEL_VBMI2)
		decoder_set_vbmi2_funcs();
	else if(use_isa >= ISA_LEVEL_AVX2)
		decoder_set_avx2_funcs();
	else if(use_isa >= ISA_LEVEL_AVX)
		decoder_set_avx_funcs();
	else if(use_isa >= ISA_LEVEL_SSSE3)
		decoder_set_ssse3_funcs();
	else
		decoder_set_sse2_funcs();
# endif
#endif
#ifdef PLATFORM_ARM
	if(cpu_supports_neon())
		decoder_set_neon_funcs();
#endif
}
