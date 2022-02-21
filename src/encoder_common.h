#ifndef __YENC_ENCODER_COMMON
#define __YENC_ENCODER_COMMON

// lookup tables for scalar processing
#define _B1(n) _B(n), _B(n+1), _B(n+2), _B(n+3)
#define _B2(n) _B1(n), _B1(n+4), _B1(n+8), _B1(n+12)
#define _B3(n) _B2(n), _B2(n+16), _B2(n+32), _B2(n+48)
#define _BX _B3(0), _B3(64), _B3(128), _B3(192)

static const unsigned char escapeLUT[256] = { // whether or not the character is critical
#define _B(n) ((n == 214 || n == '\r'+214 || n == '\n'+214 || n == '='-42) ? 0 : (n+42) & 0xff)
	_BX
#undef _B
};
static const uint16_t escapedLUT[256] = { // escaped sequences for characters that need escaping
#define _B(n) ((n == 214 || n == 214+'\r' || n == 214+'\n' || n == '='-42 || n == 214+'\t' || n == 214+' ' || n == '.'-42) ? UINT16_PACK('=', ((n+42+64)&0xff)) : 0)
	_BX
#undef _B
};

#undef _B1
#undef _B2
#undef _B3
#undef _BX


size_t do_encode_generic(int line_size, int* colOffset, const unsigned char* HEDLEY_RESTRICT src, unsigned char* HEDLEY_RESTRICT dest, size_t len, int doEnd);

template<void(&kernel)(int, int*, const uint8_t* HEDLEY_RESTRICT, uint8_t* HEDLEY_RESTRICT&, size_t&)>
static size_t do_encode_simd(int line_size, int* colOffset, const uint8_t* HEDLEY_RESTRICT src, uint8_t* HEDLEY_RESTRICT dest, size_t len, int doEnd) {
	if(len < 1) return 0;
	if(line_size < 12) { // short lines probably not worth processing in a SIMD way
		// we assume at least the first and last char exist in the line, and since the first char could be escaped, and SIMD encoder assumes at least one non-first/last char, assumption means that line size has to be >= 4
		return do_encode_generic(line_size, colOffset, src, dest, len, doEnd);
	}
	
	const uint8_t* es = src + len;
	uint8_t* p = dest;
	
	if(*colOffset < 0) *colOffset = 0; // sanity check
	
	kernel(line_size, colOffset, es, p, len);
	
	// scalar loop to process remaining
	long i = -(long)len;
	if(*colOffset == 0 && i < 0) {
		uint8_t c = es[i++];
		if (LIKELIHOOD(0.0273, escapedLUT[c] != 0)) {
			memcpy(p, escapedLUT + c, 2);
			p += 2;
			*colOffset = 2;
		} else {
			*(p++) = c + 42;
			*colOffset = 1;
		}
	}
	while(i < 0) {
		uint8_t c = es[i++];
		if(*colOffset < line_size-1) {
			if(!escapeLUT[c]) {
				p[0] = '=';
				p[1] = c+42+64;
				p += 2;
				(*colOffset) += 2;
			} else {
				*(p++) = escapeLUT[c];
				(*colOffset) += 1;
			}
		} else {
			if(*colOffset < line_size) {
				if (escapedLUT[c] && c != '.'-42) {
					memcpy(p, escapedLUT + c, 2);
					p += 2;
				} else {
					*(p++) = c + 42;
				}
				if(i == 0) break;
				c = es[i++];
			}
			
			// handle EOL
			if (escapedLUT[c]) {
				uint32_t w = UINT32_16_PACK(UINT16_PACK('\r', '\n'), (uint32_t)escapedLUT[c]);
				memcpy(p, &w, sizeof(w));
				p += 4;
				*colOffset = 2;
			} else {
				uint32_t w = UINT32_PACK('\r', '\n', (uint32_t)(c+42), 0);
				memcpy(p, &w, sizeof(w));
				p += 3;
				*colOffset = 1;
			}
		}
	}
	
	if(doEnd) {
		// special case: if the last character is a space/tab, it needs to be escaped as it's the final character on the line
		unsigned char lc = *(p-1);
		if(lc == '\t' || lc == ' ') {
			p[-1] = '=';
			*p = lc+64;
			p++;
			(*colOffset)++;
		}
	}
	return p - dest;
}

#endif /* __YENC_ENCODER_COMMON */
