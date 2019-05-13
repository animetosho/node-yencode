#include "common.h"

// ugly dummy declarations (needed for reference in decoder_common.h; perhaps consider fixing this)
static uint8_t* eqFixLUT;
static uint8_t* eqAddLUT;
static uint8_t* unshufLUT;
static uint8_t* unshufLUTBig;

#include "decoder_common.h"

int (*_do_decode)(const unsigned char**, unsigned char**, size_t, YencDecoderState*) = &do_decode_scalar<false, false>;
int (*_do_decode_raw)(const unsigned char**, unsigned char**, size_t, YencDecoderState*) = &do_decode_scalar<true, false>;
int (*_do_decode_end)(const unsigned char**, unsigned char**, size_t, YencDecoderState*) = &do_decode_end_scalar<false>;
int (*_do_decode_end_raw)(const unsigned char**, unsigned char**, size_t, YencDecoderState*) = &do_decode_end_scalar<true>;

void decoder_set_sse2_funcs();
void decoder_set_ssse3_funcs();
void decoder_set_avx_funcs();
void decoder_set_avx2_funcs();
void decoder_set_avx3_funcs();
void decoder_set_neon_funcs();

void decoder_init() {
#ifdef PLATFORM_X86
	int flags = cpu_flags();
	if((flags & 0x200) == 0x200) {
		if((flags & 0x18800000) == 0x18800000) { // POPCNT + OSXSAVE + AVX
			int xcr = _GET_XCR() & 0xff; // ignore unused bits
			if((xcr & 6) == 6) { // AVX enabled
				int cpuInfo[4];
				_cpuidX(cpuInfo, 7, 0);
				if(((xcr & 0xE0) == 0xE0) && (cpuInfo[1] & 0x40010000) == 0x40010000) // AVX512BW + AVX512VL
					decoder_set_avx3_funcs();
				else if((cpuInfo[1] & 0x20) == 0x20)
					decoder_set_avx2_funcs();
				else
					decoder_set_avx_funcs();
			} else
				decoder_set_ssse3_funcs();
		} else
			decoder_set_ssse3_funcs();
	} else
		decoder_set_sse2_funcs();
#endif
#ifdef PLATFORM_ARM
	if(cpu_supports_neon())
		decoder_set_neon_funcs();
#endif
}
