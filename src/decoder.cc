#include "common.h"

#include "decoder_common.h"

int (*_do_decode)(const unsigned char*HEDLEY_RESTRICT*, unsigned char*HEDLEY_RESTRICT*, size_t, YencDecoderState*) = &do_decode_scalar<false, false>;
int (*_do_decode_raw)(const unsigned char*HEDLEY_RESTRICT*, unsigned char*HEDLEY_RESTRICT*, size_t, YencDecoderState*) = &do_decode_scalar<true, false>;
int (*_do_decode_end)(const unsigned char*HEDLEY_RESTRICT*, unsigned char*HEDLEY_RESTRICT*, size_t, YencDecoderState*) = &do_decode_end_scalar<false>;
int (*_do_decode_end_raw)(const unsigned char*HEDLEY_RESTRICT*, unsigned char*HEDLEY_RESTRICT*, size_t, YencDecoderState*) = &do_decode_end_scalar<true>;

void decoder_set_sse2_funcs();
void decoder_set_ssse3_funcs();
void decoder_set_avx_funcs();
void decoder_set_avx2_funcs();
void decoder_set_avx3_funcs();
void decoder_set_neon_funcs();

void decoder_init() {
#ifdef PLATFORM_X86
	int use_isa = cpu_supports_isa();
	if(use_isa >= ISA_LEVEL_AVX3)
		decoder_set_avx3_funcs();
	else if(use_isa >= ISA_LEVEL_AVX2)
		decoder_set_avx2_funcs();
	else if(use_isa >= ISA_LEVEL_AVX)
		decoder_set_avx_funcs();
	else if(use_isa >= ISA_LEVEL_SSSE3)
		decoder_set_ssse3_funcs();
	else
		decoder_set_sse2_funcs();
#endif
#ifdef PLATFORM_ARM
	if(cpu_supports_neon())
		decoder_set_neon_funcs();
#endif
}
