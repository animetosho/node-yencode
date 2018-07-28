#include "common.h"

// ugly dummy declarations
static uint8_t* eqFixLUT;
static uint8_t* eqAddLUT;
static uint8_t* unshufLUT;

#include "decoder_common.h"

int (*_do_decode)(const unsigned char**, unsigned char**, size_t, YencDecoderState*) = &do_decode_scalar<false, false>;
int (*_do_decode_raw)(const unsigned char**, unsigned char**, size_t, YencDecoderState*) = &do_decode_scalar<true, false>;
int (*_do_decode_end)(const unsigned char**, unsigned char**, size_t, YencDecoderState*) = &do_decode_end_scalar<false>;
int (*_do_decode_end_raw)(const unsigned char**, unsigned char**, size_t, YencDecoderState*) = &do_decode_end_scalar<true>;

void decoder_set_sse2_funcs();
void decoder_set_ssse3_funcs();
void decoder_set_avx_funcs();
void decoder_set_avx3_funcs();
void decoder_set_neon_funcs();

void decoder_init() {
#ifdef PLATFORM_X86
	if((cpu_flags() & 0x200) == 0x200) {
		if((cpu_flags() & 0x10800000) == 0x10800000) { // POPCNT + AVX
			int cpuInfo[4];
			_cpuidX(cpuInfo, 7, 0);
			if((cpuInfo[1] & 0x40010000) == 0x40010000) // AVX512BW + AVX512VL
				decoder_set_avx3_funcs();
			else
				decoder_set_avx_funcs();
		} else
			decoder_set_ssse3_funcs();
	} else
		decoder_set_sse2_funcs();
#endif
#ifdef PLATFORM_ARM7
	if(cpu_supports_neon())
		decoder_set_neon_funcs();
#endif
}
