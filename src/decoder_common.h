#include "decoder.h"

#if defined(PLATFORM_ARM) && !defined(__aarch64__)
#define YENC_DEC_USE_THINTABLE 1
#endif

// TODO: need to support max output length somehow
// TODO: add branch probabilities


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

template<bool isRaw>
YencDecoderEnd do_decode_end_scalar(const unsigned char** src, unsigned char** dest, size_t len, YencDecoderState* state) {
	const unsigned char *es = (*src) + len; // end source pointer
	unsigned char *p = *dest; // destination pointer
	long i = -(long)len; // input position
	unsigned char c; // input character
	
	if(len < 1) return YDEC_END_NONE;
	
#define YDEC_CHECK_END(s) if(i == 0) { \
	*state = s; \
	*src = es; \
	*dest = p; \
	return YDEC_END_NONE; \
}
	if(state) switch(*state) {
		case YDEC_STATE_CRLFEQ: do_decode_endable_scalar_ceq:
			if(es[i] == 'y') {
				*state = YDEC_STATE_NONE;
				*src = es+i+1;
				*dest = p;
				return YDEC_END_CONTROL;
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
			} else if(es[i] == '=') {
				i++;
				YDEC_CHECK_END(YDEC_STATE_CRLFEQ)
				goto do_decode_endable_scalar_ceq;
			} else
				break;
			// fall-through
		case YDEC_STATE_CRLFDT:
			if(isRaw && es[i] == '\r') {
				i++;
				YDEC_CHECK_END(YDEC_STATE_CRLFDTCR)
			} else if(isRaw && es[i] == '=') { // check for dot-stuffed ending: \r\n.=y
				i++;
				YDEC_CHECK_END(YDEC_STATE_CRLFEQ)
				goto do_decode_endable_scalar_ceq;
			} else
				break;
			// fall-through
		case YDEC_STATE_CRLFDTCR:
			if(es[i] == '\n') {
				if(isRaw) {
					*state = YDEC_STATE_CRLF;
					*src = es + i + 1;
					*dest = p;
					return YDEC_END_ARTICLE;
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
							return YDEC_END_ARTICLE;
						} else i--;
					} else if(es[i] == '=') {
						i++;
						YDEC_CHECK_END(YDEC_STATE_CRLFEQ)
						if(es[i] == 'y') {
							*src = es + i + 1;
							*dest = p;
							*state = YDEC_STATE_NONE;
							return YDEC_END_CONTROL;
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
						return YDEC_END_CONTROL;
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
					return YDEC_END_NONE;
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
	return YDEC_END_NONE;
}

template<bool isRaw, bool searchEnd>
YencDecoderEnd do_decode_scalar(const unsigned char** src, unsigned char** dest, size_t len, YencDecoderState* state) {
	if(searchEnd)
		return do_decode_end_scalar<isRaw>(src, dest, len, state);
	*dest += do_decode_noend_scalar<isRaw>(*src, *dest, len, state);
	*src += len;
	return YDEC_END_NONE;
}



template<bool isRaw, bool searchEnd, void(&kernel)(const uint8_t*, long&, unsigned char*&, unsigned char&, uint16_t&)>
inline YencDecoderEnd _do_decode_simd(size_t width, const unsigned char** src, unsigned char** dest, size_t len, YencDecoderState* state) {
	if(len <= width*2) return do_decode_scalar<isRaw, searchEnd>(src, dest, len, state);
	
	YencDecoderState tState = YDEC_STATE_CRLF;
	YencDecoderState* pState = state ? state : &tState;
	if((uintptr_t)(*src) & ((width-1))) {
		// find source memory alignment
		unsigned char* aSrc = (unsigned char*)(((uintptr_t)(*src) + (width-1)) & ~(width-1));
		int amount = (int)(aSrc - *src);
		len -= amount;
		YencDecoderEnd ended = do_decode_scalar<isRaw, searchEnd>(src, dest, amount, pState);
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
						return YDEC_END_ARTICLE;
					}
					if(searchEnd && *(uint16_t*)(*src +1) == UINT16_PACK('=','y')) {
						(*src) += 3;
						*pState = YDEC_STATE_NONE;
						return YDEC_END_CONTROL;
					}
				}
				else if(searchEnd && *(uint16_t*)(*src) == UINT16_PACK('=','y')) {
					(*src) += 2;
					*pState = YDEC_STATE_NONE;
					return YDEC_END_CONTROL;
				}
				break;
			case YDEC_STATE_CR:
				if(isRaw && *(uint16_t*)(*src) == UINT16_PACK('\n','.')) {
					nextMask = 2;
					if(searchEnd && *(uint16_t*)(*src +2) == UINT16_PACK('\r','\n')) {
						(*src) += 4;
						*pState = YDEC_STATE_CRLF;
						return YDEC_END_ARTICLE;
					}
					if(searchEnd && *(uint16_t*)(*src +2) == UINT16_PACK('=','y')) {
						(*src) += 4;
						*pState = YDEC_STATE_NONE;
						return YDEC_END_CONTROL;
					}
				}
				else if(searchEnd && (*(uint32_t*)(*src) & 0xffffff) == UINT32_PACK('\n','=','y',0)) {
					(*src) += 3;
					*pState = YDEC_STATE_NONE;
					return YDEC_END_CONTROL;
				}
				break;
			case YDEC_STATE_CRLFDT:
				if(searchEnd && isRaw && *(uint16_t*)(*src) == UINT16_PACK('\r','\n')) {
					(*src) += 2;
					*pState = YDEC_STATE_CRLF;
					return YDEC_END_ARTICLE;
				}
				if(searchEnd && isRaw && *(uint16_t*)(*src) == UINT16_PACK('=','y')) {
					(*src) += 2;
					*pState = YDEC_STATE_NONE;
					return YDEC_END_CONTROL;
				}
				break;
			case YDEC_STATE_CRLFDTCR:
				if(searchEnd && isRaw && **src == '\n') {
					(*src) += 1;
					*pState = YDEC_STATE_CRLF;
					return YDEC_END_ARTICLE;
				}
				break;
			case YDEC_STATE_CRLFEQ:
				if(searchEnd && **src == 'y') {
					(*src) += 1;
					*pState = YDEC_STATE_NONE;
					return YDEC_END_CONTROL;
				}
				break;
			default: break; // silence compiler warning
		}
		escFirst = (*pState == YDEC_STATE_EQ || *pState == YDEC_STATE_CRLFEQ);
		
		// our algorithm may perform an aligned load on the next part, of which we consider 2 bytes (for \r\n. sequence checking)
		long dLen = (long)(len - lenBuffer);
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
	if(len && !searchEnd) {
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
	return YDEC_END_NONE;
}

template<bool isRaw, bool searchEnd, size_t width, void(&kernel)(const uint8_t*, long&, unsigned char*&, unsigned char&, uint16_t&)>
YencDecoderEnd do_decode_simd(const unsigned char** src, unsigned char** dest, size_t len, YencDecoderState* state) {
	return _do_decode_simd<isRaw, searchEnd, kernel>(width, src, dest, len, state);
}
template<bool isRaw, bool searchEnd, size_t(&getWidth)(), void(&kernel)(const uint8_t*, long&, unsigned char*&, unsigned char&, uint16_t&)>
YencDecoderEnd do_decode_simd(const unsigned char** src, unsigned char** dest, size_t len, YencDecoderState* state) {
	return _do_decode_simd<isRaw, searchEnd, kernel>(getWidth(), src, dest, len, state);
}


#if defined(PLATFORM_X86) || defined(PLATFORM_ARM)
void decoder_init_lut(void* compactLUT);
#endif

template<bool isRaw>
static inline void decoder_set_nextMask(const uint8_t* src, size_t len, uint16_t& nextMask) {
	if(isRaw) {
		if(len != 0) { // have to gone through at least one loop cycle
			if(src[-2] == '\r' && src[-1] == '\n' && src[0] == '.')
				nextMask = 1;
			else if(src[-1] == '\r' && src[0] == '\n' && src[1] == '.')
				nextMask = 2;
			else
				nextMask = 0;
		}
	} else
		nextMask = 0;
}

// without backtracking
template<bool isRaw>
static inline uint16_t decoder_set_nextMask(const uint8_t* src, unsigned mask) {
	if(isRaw) {
		if(src[0] == '.')
			return mask & 1;
		if(src[1] == '.')
			return mask & 2;
	}
	return 0;
}

// resolve invalid sequences of = to deal with cases like '===='
// bit hack inspired from simdjson: https://youtu.be/wlvKAT7SZIQ?t=33m38s
template<typename T>
static inline T fix_eqMask(T mask, T maskShift1) {
	// isolate the start of each consecutive bit group (e.g. 01011101 -> 01000101)
	T start = mask & ~maskShift1;
	
	// this strategy works by firstly separating groups that start on even/odd bits
	// generally, it doesn't matter which one (even/odd) we pick, but clearing even groups specifically allows the escFirst bit in maskShift1 to work
	// (this is because the start of the escFirst group is at index -1, an odd bit, but we can't clear it due to being < 0, so we just retain all odd groups instead)
	
	const T even = (T)0x5555555555555555; // every even bit (01010101...)
	
	// obtain groups which start on an odd bit (clear groups that start on an even bit, but this leaves an unwanted trailing bit)
	T oddGroups = mask + (start & even);
	
	// clear even bits in odd groups, whilst conversely preserving even bits in even groups
	// the `& mask` also conveniently gets rid of unwanted trailing bits
	return (oddGroups ^ even) & mask;
}
