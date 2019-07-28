#include "decoder.h"

// TODO: need to support max output length somehow
// TODO: add branch probabilities

// 8 - popcnt(byte) for decoder
static const unsigned char BitsSetTable256inv[256] = 
{
#   define B2(n) 8-(n),     7-(n),     7-(n),     6-(n)
#   define B4(n) B2(n), B2(n+1), B2(n+1), B2(n+2)
#   define B6(n) B4(n), B4(n+1), B4(n+1), B4(n+2)
    B6(0), B6(1), B6(1), B6(2)
#undef B2
#undef B4
#undef B6
};


// state var: refers to the previous state - only used for incremental processing
template<bool isRaw>
size_t do_decode_noend_scalar(const unsigned char* src, unsigned char* dest, size_t len, YencDecoderState* state) {
	const unsigned char *es = src + len; // end source pointer
	unsigned char *p = dest; // destination pointer
	long i = -(long)len; // input position
	unsigned char c; // input character
	
	if(len < 1) return 0;
	
	if(isRaw) {
		
		if(state) switch(*state) {
			case YDEC_STATE_EQ:
				c = es[i];
				*p++ = c - 42 - 64;
				i++;
				if(c == '\r') {
					*state = YDEC_STATE_CR;
					if(i >= 0) return 0;
				} else {
					*state = YDEC_STATE_NONE;
					break;
				}
				// fall-thru
			case YDEC_STATE_CR:
				if(es[i] != '\n') break;
				i++;
				*state = YDEC_STATE_CRLF;
				if(i >= 0) return 0;
				// Else fall-thru
			case YDEC_STATE_CRLF:
				// skip past first dot
				if(es[i] == '.') i++;
				// fall-thru
			default: break; // silence compiler warnings
		} else // treat as YDEC_STATE_CRLF
			if(es[i] == '.') i++;
		
		for(; i < -2; i++) {
			c = es[i];
			switch(c) {
				case '\r':
					// skip past \r\n. sequences
					//i += (es[i+1] == '\n' && es[i+2] == '.') << 1;
					if(es[i+1] == '\n' && es[i+2] == '.')
						i += 2;
					// fall-thru
				case '\n':
					continue;
				case '=':
					c = es[i+1];
					*p++ = c - 42 - 64;
					i += (c != '\r'); // if we have a \r, reprocess character to deal with \r\n. case
					continue;
				default:
					*p++ = c - 42;
			}
		}
		if(state) *state = YDEC_STATE_NONE;
		
		if(i == -2) { // 2nd last char
			c = es[i];
			switch(c) {
				case '\r':
					if(state && es[i+1] == '\n') {
						*state = YDEC_STATE_CRLF;
						return p - dest;
					}
					// Else fall-thru
				case '\n':
					break;
				case '=':
					c = es[i+1];
					*p++ = c - 42 - 64;
					i += (c != '\r');
					break;
				default:
					*p++ = c - 42;
			}
			i++;
		}
		
		// do final char; we process this separately to prevent an overflow if the final char is '='
		if(i == -1) {
			c = es[i];
			if(c != '\n' && c != '\r' && c != '=') {
				*p++ = c - 42;
			} else if(state) {
				if(c == '=') *state = YDEC_STATE_EQ;
				else if(c == '\r') *state = YDEC_STATE_CR;
				else *state = YDEC_STATE_NONE;
			}
		}
		
	} else {
		
		if(state && *state == YDEC_STATE_EQ) {
			*p++ = es[i] - 42 - 64;
			i++;
			*state = YDEC_STATE_NONE;
		}
		
		/*for(i = 0; i < len - 1; i++) {
			c = src[i];
			if(c == '\n' || c == '\r') continue;
			unsigned char isEquals = (c == '=');
			i += isEquals;
			*p++ = src[i] - (42 + (isEquals << 6));
		}*/
		for(; i < -1; i++) {
			c = es[i];
			switch(c) {
				case '\n': case '\r': continue;
				case '=':
					i++;
					c = es[i] - 64;
			}
			*p++ = c - 42;
		}
		if(state) *state = YDEC_STATE_NONE;
		// do final char; we process this separately to prevent an overflow if the final char is '='
		if(i == -1) {
			c = es[i];
			if(c != '\n' && c != '\r' && c != '=') {
				*p++ = c - 42;
			} else
				if(state) *state = (c == '=' ? YDEC_STATE_EQ : YDEC_STATE_NONE);
		}
		
	}
	
	return p - dest;
}

// return values:
// - 0: no end sequence found
// - 1: \r\n=y sequence found, src points to byte after 'y'
// - 2: \r\n.\r\n sequence found, src points to byte after last '\n'
template<bool isRaw>
int do_decode_end_scalar(const unsigned char** src, unsigned char** dest, size_t len, YencDecoderState* state) {
	const unsigned char *es = (*src) + len; // end source pointer
	unsigned char *p = *dest; // destination pointer
	long i = -(long)len; // input position
	unsigned char c; // input character
	
	if(len < 1) return 0;
	
#define YDEC_CHECK_END(s) if(i == 0) { \
	*state = s; \
	*src = es; \
	*dest = p; \
	return 0; \
}
	if(state) switch(*state) {
		case YDEC_STATE_CRLFEQ: do_decode_endable_scalar_ceq:
			if(es[i] == 'y') {
				*state = YDEC_STATE_NONE;
				*src = es+i+1;
				*dest = p;
				return 1;
			} // Else fall-thru
		case YDEC_STATE_EQ:
			c = es[i];
			*p++ = c - 42 - 64;
			i++;
			if(c != '\r') break;
			YDEC_CHECK_END(YDEC_STATE_CR)
			// fall-through
		case YDEC_STATE_CR:
			if(es[i] != '\n') break;
			i++;
			YDEC_CHECK_END(YDEC_STATE_CRLF)
			// fall-through
		case YDEC_STATE_CRLF: do_decode_endable_scalar_c0:
			if(es[i] == '.' && isRaw) {
				i++;
				YDEC_CHECK_END(YDEC_STATE_CRLFDT)
				// fall-through
			} else if(es[i] == '=') {
				i++;
				YDEC_CHECK_END(YDEC_STATE_CRLFEQ)
				goto do_decode_endable_scalar_ceq;
			} else
				break;
		case YDEC_STATE_CRLFDT:
			if(isRaw && es[i] == '\r') {
				i++;
				YDEC_CHECK_END(YDEC_STATE_CRLFDTCR)
				// fall-through
			} else if(isRaw && es[i] == '=') { // check for dot-stuffed ending: \r\n.=y
				i++;
				YDEC_CHECK_END(YDEC_STATE_CRLFEQ)
				goto do_decode_endable_scalar_ceq;
			} else
				break;
		case YDEC_STATE_CRLFDTCR:
			if(es[i] == '\n') {
				if(isRaw) {
					*state = YDEC_STATE_CRLF;
					*src = es + i + 1;
					*dest = p;
					return 2;
				} else {
					i++;
					YDEC_CHECK_END(YDEC_STATE_CRLF)
					goto do_decode_endable_scalar_c0; // handle as CRLF
				}
			} else
				break;
		case YDEC_STATE_NONE: break; // silence compiler warning
	} else // treat as YDEC_STATE_CRLF
		goto do_decode_endable_scalar_c0;
	
	for(; i < -2; i++) {
		c = es[i];
		switch(c) {
			case '\r': if(es[i+1] == '\n') {
				if(isRaw && es[i+2] == '.') {
					// skip past \r\n. sequences
					i += 3;
					YDEC_CHECK_END(YDEC_STATE_CRLFDT)
					// check for end
					if(es[i] == '\r') {
						i++;
						YDEC_CHECK_END(YDEC_STATE_CRLFDTCR)
						if(es[i] == '\n') {
							*src = es + i + 1;
							*dest = p;
							*state = YDEC_STATE_CRLF;
							return 2;
						} else i--;
					} else if(es[i] == '=') {
						i++;
						YDEC_CHECK_END(YDEC_STATE_CRLFEQ)
						if(es[i] == 'y') {
							*src = es + i + 1;
							*dest = p;
							*state = YDEC_STATE_NONE;
							return 1;
						} else {
							// escape char & continue
							c = es[i];
							*p++ = c - 42 - 64;
							i -= (c == '\r');
						}
					} else i--;
				}
				else if(es[i+2] == '=') {
					i += 3;
					YDEC_CHECK_END(YDEC_STATE_CRLFEQ)
					if(es[i] == 'y') {
						// ended
						*src = es + i + 1;
						*dest = p;
						*state = YDEC_STATE_NONE;
						return 1;
					} else {
						// escape char & continue
						c = es[i];
						*p++ = c - 42 - 64;
						i -= (c == '\r');
					}
				}
			} // fall-thru
			case '\n':
				continue;
			case '=':
				c = es[i+1];
				*p++ = c - 42 - 64;
				i += (c != '\r'); // if we have a \r, reprocess character to deal with \r\n. case
				continue;
			default:
				*p++ = c - 42;
		}
	}
	if(state) *state = YDEC_STATE_NONE;
	
	if(i == -2) { // 2nd last char
		c = es[i];
		switch(c) {
			case '\r':
				if(state && es[i+1] == '\n') {
					*state = YDEC_STATE_CRLF;
					*src = es;
					*dest = p;
					return 0;
				}
				// Else fall-thru
			case '\n':
				break;
			case '=':
				c = es[i+1];
				*p++ = c - 42 - 64;
				i += (c != '\r');
				break;
			default:
				*p++ = c - 42;
		}
		i++;
	}
	
	// do final char; we process this separately to prevent an overflow if the final char is '='
	if(i == -1) {
		c = es[i];
		if(c != '\n' && c != '\r' && c != '=') {
			*p++ = c - 42;
		} else if(state) {
			if(c == '=') *state = YDEC_STATE_EQ;
			else if(c == '\r') *state = YDEC_STATE_CR;
			else *state = YDEC_STATE_NONE;
		}
	}
#undef YDEC_CHECK_END
	
	*src = es;
	*dest = p;
	return 0;
}

template<bool isRaw, bool searchEnd>
int do_decode_scalar(const unsigned char** src, unsigned char** dest, size_t len, YencDecoderState* state) {
	if(searchEnd)
		return do_decode_end_scalar<isRaw>(src, dest, len, state);
	*dest += do_decode_noend_scalar<isRaw>(*src, *dest, len, state);
	*src += len;
	return 0;
}



template<bool isRaw, bool searchEnd, int width, void(&kernel)(const uint8_t*, long&, unsigned char*&, unsigned char&, uint16_t&)>
int do_decode_simd(const unsigned char** src, unsigned char** dest, size_t len, YencDecoderState* state) {
	if(len <= width*2) return do_decode_scalar<isRaw, searchEnd>(src, dest, len, state);
	
	YencDecoderState tState = YDEC_STATE_CRLF;
	YencDecoderState* pState = state ? state : &tState;
	if((uintptr_t)(*src) & ((width-1))) {
		// find source memory alignment
		unsigned char* aSrc = (unsigned char*)(((uintptr_t)(*src) + (width-1)) & ~(width-1));
		int amount = aSrc - *src;
		len -= amount;
		int ended = do_decode_scalar<isRaw, searchEnd>(src, dest, amount, pState);
		if(ended) return ended;
	}
	
	size_t lenBuffer = width -1;
	if(searchEnd) lenBuffer += 3 + (isRaw?1:0);
	else if(isRaw) lenBuffer += 2;
	
	if(len > lenBuffer) {
		unsigned char *p = *dest; // destination pointer
		unsigned char escFirst = 0; // input character; first char needs escaping
		uint16_t nextMask = 0;
		// handle finicky case of special sequences straddled across initial boundary
		switch(*pState) {
			case YDEC_STATE_CRLF:
				if(isRaw && **src == '.') {
					nextMask = 1;
					if(searchEnd && *(uint16_t*)(*src +1) == UINT16_PACK('\r','\n')) {
						(*src) += 3;
						*pState = YDEC_STATE_CRLF;
						return 2;
					}
					if(searchEnd && *(uint16_t*)(*src +1) == UINT16_PACK('=','y')) {
						(*src) += 3;
						*pState = YDEC_STATE_NONE;
						return 1;
					}
				}
				else if(searchEnd && *(uint16_t*)(*src) == UINT16_PACK('=','y')) {
					(*src) += 2;
					*pState = YDEC_STATE_NONE;
					return 1;
				}
				break;
			case YDEC_STATE_CR:
				if(isRaw && *(uint16_t*)(*src) == UINT16_PACK('\n','.')) {
					nextMask = 2;
					if(searchEnd && *(uint16_t*)(*src +2) == UINT16_PACK('\r','\n')) {
						(*src) += 4;
						*pState = YDEC_STATE_CRLF;
						return 2;
					}
					if(searchEnd && *(uint16_t*)(*src +2) == UINT16_PACK('=','y')) {
						(*src) += 4;
						*pState = YDEC_STATE_NONE;
						return 1;
					}
				}
				else if(searchEnd && (*(uint32_t*)(*src) & 0xffffff) == UINT32_PACK('\n','=','y',0)) {
					(*src) += 3;
					*pState = YDEC_STATE_NONE;
					return 1;
				}
				break;
			case YDEC_STATE_CRLFDT:
				if(searchEnd && isRaw && *(uint16_t*)(*src) == UINT16_PACK('\r','\n')) {
					(*src) += 2;
					*pState = YDEC_STATE_CRLF;
					return 2;
				}
				if(searchEnd && isRaw && *(uint16_t*)(*src) == UINT16_PACK('=','y')) {
					(*src) += 2;
					*pState = YDEC_STATE_NONE;
					return 1;
				}
				break;
			case YDEC_STATE_CRLFDTCR:
				if(searchEnd && isRaw && **src == '\n') {
					(*src) += 1;
					*pState = YDEC_STATE_CRLF;
					return 2;
				}
				break;
			case YDEC_STATE_CRLFEQ:
				if(searchEnd && **src == 'y') {
					(*src) += 1;
					*pState = YDEC_STATE_NONE;
					return 1;
				}
				break;
			default: break; // silence compiler warning
		}
		escFirst = (*pState == YDEC_STATE_EQ || *pState == YDEC_STATE_CRLFEQ);
		
		// our algorithm may perform an aligned load on the next part, of which we consider 2 bytes (for \r\n. sequence checking)
		long dLen = len - lenBuffer;
		dLen = (dLen + (width-1)) & ~(width-1);
		
		kernel((const uint8_t*)(*src) + dLen, dLen, p, escFirst, nextMask);
		
		if(escFirst) *pState = YDEC_STATE_EQ; // escape next character
		else if(nextMask == 1) *pState = YDEC_STATE_CRLF; // next character is '.', where previous two were \r\n
		else if(nextMask == 2) *pState = YDEC_STATE_CR; // next characters are '\n.', previous is \r
		else *pState = YDEC_STATE_NONE;
		
		*src += dLen;
		len -= dLen;
		*dest = p;
	}
	
	// end alignment
	if(len)
		return do_decode_scalar<isRaw, searchEnd>(src, dest, len, pState);
	/** for debugging: ensure that the SIMD routine doesn't exit early
	if(len) {
		const uint8_t* s = *src;
		unsigned char* p = *dest;
		int ended = do_decode_scalar<isRaw, searchEnd>(src, dest, len, pState);
		if(*src - s > width*2) {
			// this shouldn't happen, corrupt some data to fail the test
			while(p < *dest)
				*p++ = 0;
		}
		return ended;
	}
	*/
	return 0;
}

static uint8_t eqFixLUT[256];
ALIGN_32(static uint64_t eqAddLUT[256]);
#ifdef YENC_DEC_USE_THINTABLE
ALIGN_32(static uint64_t unshufLUT[256]);
#else
ALIGN_32(static struct { char bytes[16]; } unshufLUTBig[32768]);
#endif

static inline void decoder_init_lut() {
	for(int i=0; i<256; i++) {
		int k = i;
		uint8_t* res;
		int p = 0;
		
		// fix LUT
		k = i;
		p = 0;
		for(int j=0; j<8; j++) {
			k = i >> j;
			if(k & 1) {
				p |= 1 << j;
				j++;
			}
		}
		eqFixLUT[i] = p;
		
		// sub LUT
		res = (uint8_t*)(eqAddLUT + i);
		k = i;
		for(int j=0; j<8; j++) {
			res[j] = (k & 1) ? 192 /* == -64 */ : 0;
			k >>= 1;
		}
		
		#ifdef YENC_DEC_USE_THINTABLE
		res = (uint8_t*)(unshufLUT + i);
		k = i;
		p = 0;
		for(int j=0; j<8; j++) {
			if(!(k & 1)) {
				res[p++] = j;
			}
			k >>= 1;
		}
		for(; p<8; p++)
			res[p] = 0x80;
		#endif
	}
	#ifndef YENC_DEC_USE_THINTABLE
	for(int i=0; i<32768; i++) {
		int k = i;
		uint8_t* res = (uint8_t*)(unshufLUTBig + i);
		int p = 0;
		
		for(int j=0; j<16; j++) {
			if(!(k & 1)) {
				res[p++] = j;
			}
			k >>= 1;
		}
		for(; p<16; p++)
			res[p] = 0x80;
	}
	#endif
}

