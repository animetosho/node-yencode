#include "common.h"
#include "encoder_common.h"
#include "encoder.h"

size_t do_encode_generic(int line_size, int* colOffset, const unsigned char* HEDLEY_RESTRICT src, unsigned char* HEDLEY_RESTRICT dest, size_t len, int doEnd) {
	unsigned char* es = (unsigned char*)src + len;
	unsigned char *p = dest; // destination pointer
	long i = -(long)len; // input position
	unsigned char c, escaped; // input character; escaped input character
	int col = *colOffset;
	
	if (col == 0) {
		c = es[i++];
		if (escapedLUT[c]) {
			memcpy(p, &escapedLUT[c], sizeof(uint16_t));
			p += 2;
			col = 2;
		} else {
			*(p++) = c + 42;
			col = 1;
		}
	}
	while(i < 0) {
		// main line
		unsigned char* sp = NULL;
		while (i < -1-8 && line_size-col-1 > 8) {
			// 8 cycle unrolled version
			sp = p;
			#define DO_THING(n) \
				c = es[i+n], escaped = escapeLUT[c]; \
				if (escaped) \
					*(p++) = escaped; \
				else { \
					memcpy(p, &escapedLUT[c], sizeof(uint16_t)); \
					p += 2; \
				}
			DO_THING(0);
			DO_THING(1);
			DO_THING(2);
			DO_THING(3);
			DO_THING(4);
			DO_THING(5);
			DO_THING(6);
			DO_THING(7);
			
			i += 8;
			col += (int)(p - sp);
		}
		if(sp && col >= line_size-1) {
			// TODO: consider revert optimisation from SIMD code
			// we overflowed - need to revert and use slower method :(
			col -= (int)(p - sp);
			p = sp;
			i -= 8;
		}
		// handle remaining chars
		while(col < line_size-1) {
			c = es[i++], escaped = escapeLUT[c];
			if (escaped) {
				*(p++) = escaped;
				col++;
			}
			else {
				memcpy(p, &escapedLUT[c], sizeof(uint16_t));
				p += 2;
				col += 2;
			}
			/* experimental branchless version 
			*p = '=';
			c = (es[i++] + 42) & 0xFF;
			int cond = (c=='\0' || c=='=' || c=='\r' || c=='\n');
			*(p+cond) = c + (cond << 6);
			p += 1+cond;
			col += 1+cond;
			*/
			if (i >= 0) goto end;
		}
		
		// last line char
		if(col < line_size) { // this can only be false if the last character was an escape sequence (or line_size is horribly small), in which case, we don't need to handle space/tab cases
			c = es[i++];
			if (escapedLUT[c] && c != '.'-42) {
				memcpy(p, &escapedLUT[c], sizeof(uint16_t));
				p += 2;
			} else {
				*(p++) = c + 42;
			}
		}
		
		if (i >= 0) break;
		
		c = es[i++];
		if (escapedLUT[c]) {
			uint32_t w = UINT32_16_PACK(UINT16_PACK('\r', '\n'), (uint32_t)escapedLUT[c]);
			memcpy(p, &w, sizeof(w));
			p += 4;
			col = 2;
		} else {
			// another option may be to just write the EOL and let the first char be handled by the faster methods above, but it appears that writing the extra byte here is generally faster...
			uint32_t w = UINT32_PACK('\r', '\n', (uint32_t)(c+42), 0);
			memcpy(p, &w, sizeof(w));
			p += 3;
			col = 1;
		}
	}
	
	end:
	if(doEnd) {
		// special case: if the last character is a space/tab, it needs to be escaped as it's the final character on the line
		unsigned char lc = *(p-1);
		if(lc == '\t' || lc == ' ') {
			*(p-1) = '=';
			*p = lc+64;
			p++;
			col++;
		}
	}
	*colOffset = col;
	return p - dest;
}


extern "C" {
	size_t (*_do_encode)(int, int*, const unsigned char* HEDLEY_RESTRICT, unsigned char* HEDLEY_RESTRICT, size_t, int) = &do_encode_generic;
}

void encoder_sse2_init();
void encoder_ssse3_init();
void encoder_avx_init();
void encoder_avx2_init();
void encoder_vbmi2_init();
void encoder_neon_init();

#if defined(PLATFORM_X86) && defined(YENC_BUILD_NATIVE) && YENC_BUILD_NATIVE!=0
# if defined(__AVX2__) && !defined(YENC_DISABLE_AVX256)
#  include "encoder_avx_base.h"
static inline void encoder_native_init() {
	_do_encode = &do_encode_simd< do_encode_avx2<ISA_NATIVE> >;
	encoder_avx2_lut<ISA_NATIVE>();
}
# else
#  include "encoder_sse_base.h"
static inline void encoder_native_init() {
	_do_encode = &do_encode_simd< do_encode_sse<ISA_NATIVE> >;
	encoder_sse_lut<ISA_NATIVE>();
}
# endif
#endif


void encoder_init() {
#ifdef PLATFORM_X86
# if defined(YENC_BUILD_NATIVE) && YENC_BUILD_NATIVE!=0
	encoder_native_init();
# else
	int use_isa = cpu_supports_isa();
	if(use_isa >= ISA_LEVEL_VBMI2)
		encoder_vbmi2_init();
	else if(use_isa >= ISA_LEVEL_AVX2)
		encoder_avx2_init();
	else if(use_isa >= ISA_LEVEL_AVX)
		encoder_avx_init();
	else if(use_isa >= ISA_LEVEL_SSSE3)
		encoder_ssse3_init();
	else
		encoder_sse2_init();
# endif
#endif
#ifdef PLATFORM_ARM
	if(cpu_supports_neon())
		encoder_neon_init();
#endif
}
