#include "common.h"
#include "decoder.h"

// TODO: need to support max output length somehow

// state var: refers to the previous state - only used for incremental processing
template<bool isRaw>
size_t do_decode_noend_scalar(const unsigned char* src, unsigned char* dest, size_t len, YencDecoderState* state) {
	const unsigned char *es = src + len; // end source pointer
	unsigned char *p = dest; // destination pointer
	long i = -len; // input position
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
					// fall through
				} else {
					*state = YDEC_STATE_NONE;
					break;
				}
			case YDEC_STATE_CR:
				if(es[i] != '\n') break;
				i++;
				*state = YDEC_STATE_CRLF;
				if(i >= 0) return 0;
			case YDEC_STATE_CRLF:
				// skip past first dot
				if(es[i] == '.') i++;
			default: break; // silence compiler warnings
		} else // treat as YDEC_STATE_CRLF
			if(es[i] == '.') i++;
		
		for(; i < -2; i++) {
			c = es[i];
			switch(c) {
				case '\r':
					// skip past \r\n. sequences
					//i += (*(uint16_t*)(es + i + 1) == UINT16_PACK('\n', '.')) << 1;
					if(*(uint16_t*)(es + i + 1) == UINT16_PACK('\n', '.'))
						i += 2;
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
	long i = -len; // input position
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
			} // else fall thru and escape
		case YDEC_STATE_EQ:
			c = es[i];
			*p++ = c - 42 - 64;
			i++;
			if(c != '\r') break;
			YDEC_CHECK_END(YDEC_STATE_CR)
			// fall through
		case YDEC_STATE_CR:
			if(es[i] != '\n') break;
			i++;
			YDEC_CHECK_END(YDEC_STATE_CRLF)
		case YDEC_STATE_CRLF: do_decode_endable_scalar_c0:
			if(es[i] == '.' && isRaw) {
				i++;
				YDEC_CHECK_END(YDEC_STATE_CRLFDT)
				// fallthru
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
				// fallthru
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
			case '\r': {
				uint16_t next = *(uint16_t*)(es + i + 1);
				if(isRaw && next == UINT16_PACK('\n', '.')) {
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
				else if(next == UINT16_PACK('\n', '=')) {
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
			} case '\n':
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

template<bool isRaw, bool searchEnd, int width, bool(&kernel)(const uint8_t*, unsigned char*&, unsigned char&, uint16_t&)>
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
		size_t dLen = len - lenBuffer;
		dLen = (dLen + (width-1)) & ~(width-1);
		const uint8_t* dSrc = (const uint8_t*)(*src) + dLen;
		long dI = -dLen;
		
		for(; dI; dI += width) {
			if(!kernel(dSrc + dI, p, escFirst, nextMask)) {
				dLen += dI;
				break;
			}
		}
		
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


#ifdef __SSE2__
uint8_t eqFixLUT[256];
ALIGN_32(__m64 eqAddLUT[256]);
#ifdef __SSSE3__
ALIGN_32(__m64 unshufLUT[256]);
ALIGN_32(static const uint8_t _pshufb_combine_table[272]) = {
	0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f,
	0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x08,0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f,0x80,
	0x00,0x01,0x02,0x03,0x04,0x05,0x08,0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f,0x80,0x80,
	0x00,0x01,0x02,0x03,0x04,0x08,0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f,0x80,0x80,0x80,
	0x00,0x01,0x02,0x03,0x08,0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f,0x80,0x80,0x80,0x80,
	0x00,0x01,0x02,0x08,0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f,0x80,0x80,0x80,0x80,0x80,
	0x00,0x01,0x08,0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f,0x80,0x80,0x80,0x80,0x80,0x80,
	0x00,0x08,0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f,0x80,0x80,0x80,0x80,0x80,0x80,0x80,
	0x08,0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,
};
static const __m128i* pshufb_combine_table = (const __m128i*)_pshufb_combine_table;
#endif

template<bool isRaw, bool searchEnd, bool use_ssse3>
inline bool do_decode_sse(const uint8_t* src, unsigned char*& p, unsigned char& escFirst, uint16_t& nextMask) {
	__m128i data = _mm_load_si128((__m128i *)src);
	
	// search for special chars
	__m128i cmpEq = _mm_cmpeq_epi8(data, _mm_set1_epi8('=')),
#ifdef __AVX512VL__
	cmp = _mm_ternarylogic_epi32(
		_mm_cmpeq_epi8(data, _mm_set1_epi16(0x0a0d)),
		_mm_cmpeq_epi8(data, _mm_set1_epi16(0x0d0a)),
		cmpEq,
		0xFE
	);
#else
	cmp = _mm_or_si128(
		_mm_or_si128(
			_mm_cmpeq_epi8(data, _mm_set1_epi16(0x0a0d)), // \r\n
			_mm_cmpeq_epi8(data, _mm_set1_epi16(0x0d0a))  // \n\r
		),
		cmpEq
	);
#endif
	uint16_t mask = _mm_movemask_epi8(cmp); // not the most accurate mask if we have invalid sequences; we fix this up later
	
	__m128i oData;
	if(escFirst) { // rarely hit branch: seems to be faster to use 'if' than a lookup table, possibly due to values being able to be held in registers?
		// first byte needs escaping due to preceeding = in last loop iteration
		oData = _mm_sub_epi8(data, _mm_set_epi8(42,42,42,42,42,42,42,42,42,42,42,42,42,42,42,42+64));
		mask &= ~1;
	} else {
		oData = _mm_sub_epi8(data, _mm_set1_epi8(42));
	}
	if(isRaw) mask |= nextMask;
	
	if (mask != 0) {
		// a spec compliant encoder should never generate sequences: ==, =\n and =\r, but we'll handle them to be spec compliant
		// the yEnc specification requires any character following = to be unescaped, not skipped over, so we'll deal with that
		
#define LOAD_HALVES(a, b) _mm_castps_si128(_mm_loadh_pi( \
	_mm_castsi128_ps(_mm_loadl_epi64((__m128i*)(a))), \
	(b) \
))
		// firstly, resolve invalid sequences of = to deal with cases like '===='
		uint16_t maskEq = _mm_movemask_epi8(cmpEq);
		uint16_t tmp = eqFixLUT[(maskEq&0xff) & ~escFirst];
		maskEq = (eqFixLUT[(maskEq>>8) & ~(tmp>>7)] << 8) | tmp;
		
		unsigned char oldEscFirst = escFirst;
		escFirst = (maskEq >> (sizeof(__m128i)-1));
		// next, eliminate anything following a `=` from the special char mask; this eliminates cases of `=\r` so that they aren't removed
		maskEq <<= 1;
		mask &= ~maskEq;
		
		// unescape chars following `=`
#if defined(__AVX512VL__) && defined(__AVX512BW__)
		// GCC < 7 seems to generate rubbish assembly for this
		oData = _mm_mask_add_epi8(
			oData,
			maskEq,
			oData,
			_mm_set1_epi8(-64)
		);
#else
		oData = _mm_add_epi8(
			oData,
			LOAD_HALVES(
				eqAddLUT + (maskEq&0xff),
				eqAddLUT + ((maskEq>>8)&0xff)
			)
		);
#endif
		
		// handle \r\n. sequences
		// RFC3977 requires the first dot on a line to be stripped, due to dot-stuffing
		if(isRaw || searchEnd) {
			// find instances of \r\n
			__m128i tmpData1, tmpData2, tmpData3, tmpData4;
#if defined(__SSSE3__) && !defined(__tune_btver1__)
			if(use_ssse3) {
				__m128i nextData = _mm_load_si128((__m128i *)src + 1);
				tmpData1 = _mm_alignr_epi8(nextData, data, 1);
				tmpData2 = _mm_alignr_epi8(nextData, data, 2);
				if(searchEnd) tmpData3 = _mm_alignr_epi8(nextData, data, 3);
				if(searchEnd && isRaw) tmpData4 = _mm_alignr_epi8(nextData, data, 4);
			} else {
#endif
				tmpData1 = _mm_insert_epi16(_mm_srli_si128(data, 1), *(uint16_t*)(src + sizeof(__m128i)-1), 7);
				tmpData2 = _mm_insert_epi16(_mm_srli_si128(data, 2), *(uint16_t*)(src + sizeof(__m128i)), 7);
				if(searchEnd)
					tmpData3 = _mm_insert_epi16(_mm_srli_si128(tmpData1, 2), *(uint16_t*)(src + sizeof(__m128i)+1), 7);
				if(searchEnd && isRaw)
					tmpData4 = _mm_insert_epi16(_mm_srli_si128(tmpData2, 2), *(uint16_t*)(src + sizeof(__m128i)+2), 7);
#ifdef __SSSE3__
			}
#endif
			__m128i matchNl1 = _mm_cmpeq_epi16(data, _mm_set1_epi16(0x0a0d));
			__m128i matchNl2 = _mm_cmpeq_epi16(tmpData1, _mm_set1_epi16(0x0a0d));
			
			__m128i matchDots, matchNlDots;
			if(isRaw) {
				matchDots = _mm_cmpeq_epi8(tmpData2, _mm_set1_epi8('.'));
				// merge preparation (for non-raw, it doesn't matter if this is shifted or not)
				matchNl1 = _mm_srli_si128(matchNl1, 1);
				
				// merge matches of \r\n with those for .
#ifdef __AVX512VL__
				matchNlDots = _mm_ternarylogic_epi32(matchDots, matchNl1, matchNl2, 0xE0);
#else
				matchNlDots = _mm_and_si128(matchDots, _mm_or_si128(matchNl1, matchNl2));
#endif
			}
			
			if(searchEnd) {
				__m128i cmpB1 = _mm_cmpeq_epi16(tmpData2, _mm_set1_epi16(0x793d)); // "=y"
				__m128i cmpB2 = _mm_cmpeq_epi16(tmpData3, _mm_set1_epi16(0x793d));
				if(isRaw) {
					// match instances of \r\n.\r\n and \r\n.=y
					__m128i cmpC1 = _mm_cmpeq_epi16(tmpData3, _mm_set1_epi16(0x0a0d)); // "\r\n"
					__m128i cmpC2 = _mm_cmpeq_epi16(tmpData4, _mm_set1_epi16(0x0a0d));
#ifdef __AVX512VL__
					cmpC1 = _mm_ternarylogic_epi32(cmpC1, cmpB2, matchDots, 0xA8);
					cmpC2 = _mm_ternarylogic_epi32(cmpC2, _mm_cmpeq_epi16(tmpData4, _mm_set1_epi16(0x793d)), matchDots, 0xA8);
					
					// then merge w/ cmpB
					cmpB1 = _mm_ternarylogic_epi32(cmpB1, cmpC1, matchNl1, 0xA8);
					cmpB2 = _mm_ternarylogic_epi32(cmpB2, cmpC2, matchNl2, 0xA8);
					
					cmpB1 = _mm_or_si128(cmpB1, cmpB2);
#else
					cmpC1 = _mm_or_si128(cmpC1, cmpB2);
					cmpC2 = _mm_or_si128(cmpC2, _mm_cmpeq_epi16(tmpData4, _mm_set1_epi16(0x793d)));
					cmpC2 = _mm_slli_si128(cmpC2, 1);
					cmpC1 = _mm_or_si128(cmpC1, cmpC2);
					
					// and w/ dots
					cmpC1 = _mm_and_si128(cmpC1, matchNlDots);
					// then merge w/ cmpB
					cmpB1 = _mm_and_si128(cmpB1, matchNl1);
					cmpB2 = _mm_and_si128(cmpB2, matchNl2);
					
					cmpB1 = _mm_or_si128(cmpC1, _mm_or_si128(
						cmpB1, cmpB2
					));
#endif
				} else {
					cmpB1 = _mm_or_si128(
						_mm_and_si128(cmpB1, matchNl1),
						_mm_and_si128(cmpB2, matchNl2)
					);
				}
				if(_mm_movemask_epi8(cmpB1)) {
					// terminator found
					// there's probably faster ways to do this, but reverting to scalar code should be good enough
					escFirst = oldEscFirst;
					return false;
				}
			}
			if(isRaw) {
				uint16_t killDots = _mm_movemask_epi8(matchNlDots);
				mask |= (killDots << 2) & 0xffff;
				nextMask = killDots >> (sizeof(__m128i)-2);
			}
		}
		
		// all that's left is to 'compress' the data (skip over masked chars)
#ifdef __SSSE3__
		if(use_ssse3) {
# if defined(__POPCNT__) && (defined(__tune_znver1__) || defined(__tune_btver2__))
			unsigned char skipped = _mm_popcnt_u32(mask & 0xff);
# else
			unsigned char skipped = BitsSetTable256[mask & 0xff];
# endif
			// lookup compress masks and shuffle
			// load up two halves
			__m128i shuf = LOAD_HALVES(unshufLUT + (mask&0xff), unshufLUT + (mask>>8));
			
			// offset upper half by 8
			shuf = _mm_add_epi8(shuf, _mm_set_epi32(0x08080808, 0x08080808, 0, 0));
			// shift down upper half into lower
			// TODO: consider using `mask & 0xff` in table instead of counting bits
			shuf = _mm_shuffle_epi8(shuf, _mm_load_si128(pshufb_combine_table + skipped));
			
			// shuffle data
			oData = _mm_shuffle_epi8(oData, shuf);
			STOREU_XMM(p, oData);
			
			// increment output position
# if defined(__POPCNT__) && !defined(__tune_btver1__)
			p += XMM_SIZE - _mm_popcnt_u32(mask);
# else
			p += XMM_SIZE - skipped - BitsSetTable256[mask >> 8];
# endif
			
		} else {
#endif
			ALIGN_32(uint32_t mmTmp[4]);
			_mm_store_si128((__m128i*)mmTmp, oData);
			
			for(int j=0; j<4; j++) {
				if(mask & 0xf) {
					unsigned char* pMmTmp = (unsigned char*)(mmTmp + j);
					unsigned int maskn = ~mask;
					*p = pMmTmp[0];
					p += (maskn & 1);
					*p = pMmTmp[1];
					p += (maskn & 2) >> 1;
					*p = pMmTmp[2];
					p += (maskn & 4) >> 2;
					*p = pMmTmp[3];
					p += (maskn & 8) >> 3;
				} else {
					*(uint32_t*)p = mmTmp[j];
					p += 4;
				}
				mask >>= 4;
			}
#ifdef __SSSE3__
		}
#endif
#undef LOAD_HALVES
	} else {
		STOREU_XMM(p, oData);
		p += XMM_SIZE;
		escFirst = 0;
		if(isRaw) nextMask = 0;
	}
	return true;
}
#endif


#ifdef __ARM_NEON
uint8_t eqFixLUT[256];
ALIGN_32(uint8x8_t eqAddLUT[256]);
ALIGN_32(uint8x8_t unshufLUT[256]);
ALIGN_32(static const uint8_t pshufb_combine_table[272]) = {
	0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f,
	0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x08,0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f,0x80,
	0x00,0x01,0x02,0x03,0x04,0x05,0x08,0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f,0x80,0x80,
	0x00,0x01,0x02,0x03,0x04,0x08,0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f,0x80,0x80,0x80,
	0x00,0x01,0x02,0x03,0x08,0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f,0x80,0x80,0x80,0x80,
	0x00,0x01,0x02,0x08,0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f,0x80,0x80,0x80,0x80,0x80,
	0x00,0x01,0x08,0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f,0x80,0x80,0x80,0x80,0x80,0x80,
	0x00,0x08,0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f,0x80,0x80,0x80,0x80,0x80,0x80,0x80,
	0x08,0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,
};

template<bool isRaw, bool searchEnd>
inline bool do_decode_neon(const uint8_t* src, unsigned char*& p, unsigned char& escFirst, uint16_t& nextMask) {
	uint8x16_t data = vld1q_u8(src);
	
	// search for special chars
	uint8x16_t cmpEq = vceqq_u8(data, vdupq_n_u8('=')),
	cmp = vorrq_u8(
		vorrq_u8(
			vceqq_u8(data, vreinterpretq_u8_u16(vdupq_n_u16(0x0a0d))), // \r\n
			vceqq_u8(data, vreinterpretq_u8_u16(vdupq_n_u16(0x0d0a)))  // \n\r
		),
		cmpEq
	);
	uint16_t mask = neon_movemask(cmp); // not the most accurate mask if we have invalid sequences; we fix this up later
	
	uint8x16_t oData;
	if(escFirst) { // rarely hit branch: seems to be faster to use 'if' than a lookup table, possibly due to values being able to be held in registers?
		// first byte needs escaping due to preceeding = in last loop iteration
		oData = vsubq_u8(data, (uint8x16_t){42+64,42,42,42,42,42,42,42,42,42,42,42,42,42,42,42});
		mask &= ~1;
	} else {
		oData = vsubq_u8(data, vdupq_n_u8(42));
	}
	if(isRaw) mask |= nextMask;
	
	if (mask != 0) {
		// a spec compliant encoder should never generate sequences: ==, =\n and =\r, but we'll handle them to be spec compliant
		// the yEnc specification requires any character following = to be unescaped, not skipped over, so we'll deal with that
		
		// firstly, resolve invalid sequences of = to deal with cases like '===='
		uint16_t maskEq = neon_movemask(cmpEq);
		uint16_t tmp = eqFixLUT[(maskEq&0xff) & ~escFirst];
		maskEq = (eqFixLUT[(maskEq>>8) & ~(tmp>>7)] << 8) | tmp;
		
		unsigned char oldEscFirst = escFirst;
		escFirst = (maskEq >> (sizeof(uint8x16_t)-1));
		// next, eliminate anything following a `=` from the special char mask; this eliminates cases of `=\r` so that they aren't removed
		maskEq <<= 1;
		mask &= ~maskEq;
		
		// unescape chars following `=`
		oData = vaddq_u8(
			oData,
			vcombine_u8(
				vld1_u8((uint8_t*)(eqAddLUT + (maskEq&0xff))),
				vld1_u8((uint8_t*)(eqAddLUT + ((maskEq>>8)&0xff)))
			)
		);
		
		// handle \r\n. sequences
		// RFC3977 requires the first dot on a line to be stripped, due to dot-stuffing
		if(isRaw || searchEnd) {
			// find instances of \r\n
			uint8x16_t tmpData1, tmpData2, tmpData3, tmpData4;
			uint8x16_t nextData = vld1q_u8(src + sizeof(uint8x16_t));
			tmpData1 = vextq_u8(data, nextData, 1);
			tmpData2 = vextq_u8(data, nextData, 2);
			if(searchEnd) {
				tmpData3 = vextq_u8(data, nextData, 3);
				if(isRaw) tmpData4 = vextq_u8(data, nextData, 4);
			}
			uint8x16_t matchNl1 = vreinterpretq_u8_u16(vceqq_u16(vreinterpretq_u16_u8(data), vdupq_n_u16(0x0a0d)));
			uint8x16_t matchNl2 = vreinterpretq_u8_u16(vceqq_u16(vreinterpretq_u16_u8(tmpData1), vdupq_n_u16(0x0a0d)));
			
			uint8x16_t matchDots, matchNlDots;
			uint16_t killDots;
			if(isRaw) {
				matchDots = vceqq_u8(tmpData2, vdupq_n_u8('.'));
				// merge preparation (for non-raw, it doesn't matter if this is shifted or not)
				matchNl1 = vextq_u8(matchNl1, vdupq_n_u8(0), 1);
				
				// merge matches of \r\n with those for .
				matchNlDots = vandq_u8(matchDots, vorrq_u8(matchNl1, matchNl2));
				killDots = neon_movemask(matchNlDots);
			}
			
			if(searchEnd) {
				uint8x16_t cmpB1 = vreinterpretq_u8_u16(vceqq_u16(vreinterpretq_u16_u8(tmpData2), vdupq_n_u16(0x793d))); // "=y"
				uint8x16_t cmpB2 = vreinterpretq_u8_u16(vceqq_u16(vreinterpretq_u16_u8(tmpData3), vdupq_n_u16(0x793d)));
				if(isRaw && killDots) {
					// match instances of \r\n.\r\n and \r\n.=y
					uint8x16_t cmpC1 = vreinterpretq_u8_u16(vceqq_u16(vreinterpretq_u16_u8(tmpData3), vdupq_n_u16(0x0a0d)));
					uint8x16_t cmpC2 = vreinterpretq_u8_u16(vceqq_u16(vreinterpretq_u16_u8(tmpData4), vdupq_n_u16(0x0a0d)));
					cmpC1 = vorrq_u8(cmpC1, cmpB2);
					cmpC2 = vorrq_u8(cmpC2, vreinterpretq_u8_u16(vceqq_u16(vreinterpretq_u16_u8(tmpData4), vdupq_n_u16(0x793d))));
					cmpC2 = vextq_u8(vdupq_n_u8(0), cmpC2, 15);
					cmpC1 = vorrq_u8(cmpC1, cmpC2);
					
					// and w/ dots
					cmpC1 = vandq_u8(cmpC1, matchNlDots);
					// then merge w/ cmpB
					cmpB1 = vandq_u8(cmpB1, matchNl1);
					cmpB2 = vandq_u8(cmpB2, matchNl2);
					
					cmpB1 = vorrq_u8(cmpC1, vorrq_u8(
						cmpB1, cmpB2
					));
				} else {
					cmpB1 = vorrq_u8(
						vandq_u8(cmpB1, matchNl1),
						vandq_u8(cmpB2, matchNl2)
					);
				}
#ifdef __aarch64__
				if(vget_lane_u64(vqmovn_u64(vreinterpretq_u64_u8(cmpB1)), 0))
#else
				uint32x4_t tmp1 = vreinterpretq_u32_u8(cmpB1);
				uint32x2_t tmp2 = vorr_u32(vget_low_u32(tmp1), vget_high_u32(tmp1));
				if(vget_lane_u32(vpmax_u32(tmp2, tmp2), 0))
#endif
				{
					// terminator found
					// there's probably faster ways to do this, but reverting to scalar code should be good enough
					escFirst = oldEscFirst;
					return false;
				}
			}
			if(isRaw) {
				mask |= (killDots << 2) & 0xffff;
				nextMask = killDots >> (sizeof(uint8x16_t)-2);
			}
		}
		
		// all that's left is to 'compress' the data (skip over masked chars)
		unsigned char skipped = BitsSetTable256[mask & 0xff];
		// lookup compress masks and shuffle
		oData = vcombine_u8(
			vtbl1_u8(vget_low_u8(oData),  vld1_u8((uint8_t*)(unshufLUT + (mask&0xff)))),
			vtbl1_u8(vget_high_u8(oData), vld1_u8((uint8_t*)(unshufLUT + (mask>>8))))
		);
		// compact down
		uint8x16_t compact = vld1q_u8(pshufb_combine_table + skipped*sizeof(uint8x16_t));
# ifdef __aarch64__
		oData = vqtbl1q_u8(oData, compact);
# else
		uint8x8x2_t dataH = {vget_low_u8(oData), vget_high_u8(oData)};
		oData = vcombine_u8(vtbl2_u8(dataH, vget_low_u8(compact)),
		                    vtbl2_u8(dataH, vget_high_u8(compact)));
# endif
		vst1q_u8(p, oData);
		
		// increment output position
		p += sizeof(uint8x16_t) - skipped - BitsSetTable256[mask >> 8];
		
	} else {
		vst1q_u8(p, oData);
		p += sizeof(uint8x16_t);
		escFirst = 0;
		if(isRaw) nextMask = 0;
	}
	return true;
}

int (*_do_decode)(const unsigned char**, unsigned char**, size_t, YencDecoderState*) = &do_decode_simd<false, false, sizeof(uint8x16_t), do_decode_neon<false, false> >;
int (*_do_decode_raw)(const unsigned char**, unsigned char**, size_t, YencDecoderState*) = &do_decode_simd<true, false, sizeof(uint8x16_t), do_decode_neon<true, false> >;
int (*_do_decode_end)(const unsigned char**, unsigned char**, size_t, YencDecoderState*) = &do_decode_simd<false, true, sizeof(uint8x16_t), do_decode_neon<false, true> >;
int (*_do_decode_end_raw)(const unsigned char**, unsigned char**, size_t, YencDecoderState*) = &do_decode_simd<true, true, sizeof(uint8x16_t), do_decode_neon<true, true> >;

#else

int (*_do_decode)(const unsigned char**, unsigned char**, size_t, YencDecoderState*) = &do_decode_scalar<false, false>;
int (*_do_decode_raw)(const unsigned char**, unsigned char**, size_t, YencDecoderState*) = &do_decode_scalar<true, false>;
int (*_do_decode_end)(const unsigned char**, unsigned char**, size_t, YencDecoderState*) = &do_decode_end_scalar<false>;
int (*_do_decode_end_raw)(const unsigned char**, unsigned char**, size_t, YencDecoderState*) = &do_decode_end_scalar<true>;

#endif

void decoder_init() {
#ifdef __SSE2__
	_do_decode = &do_decode_simd<false, false, sizeof(__m128i), do_decode_sse<false, false, false> >;
	_do_decode_raw = &do_decode_simd<true, false, sizeof(__m128i), do_decode_sse<true, false, false> >;
	_do_decode_end = &do_decode_simd<false, true, sizeof(__m128i), do_decode_sse<false, true, false> >;
	_do_decode_end_raw = &do_decode_simd<true, true, sizeof(__m128i), do_decode_sse<true, true, false> >;
	for(int i=0; i<256; i++) {
		int k = i;
		uint8_t res[8];
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
		k = i;
		for(int j=0; j<8; j++) {
			res[j] = (k & 1) ? 192 /* == -64 */ : 0;
			k >>= 1;
		}
		_mm_storel_epi64((__m128i*)(eqAddLUT + i), _mm_loadl_epi64((__m128i*)res));
	}
#endif
#ifdef __SSSE3__
	if((cpu_flags() & CPU_SHUFFLE_FLAGS) == CPU_SHUFFLE_FLAGS) {
		_do_decode = &do_decode_simd<false, false, sizeof(__m128i), do_decode_sse<false, false, true> >;
		_do_decode_raw = &do_decode_simd<true, false, sizeof(__m128i), do_decode_sse<true, false, true> >;
		_do_decode_end = &do_decode_simd<false, true, sizeof(__m128i), do_decode_sse<false, true, true> >;
		_do_decode_end_raw = &do_decode_simd<true, true, sizeof(__m128i), do_decode_sse<true, true, true> >;
		// generate unshuf LUT
		for(int i=0; i<256; i++) {
			int k = i;
			uint8_t res[8];
			int p = 0;
			for(int j=0; j<8; j++) {
				if(!(k & 1)) {
					res[p++] = j;
				}
				k >>= 1;
			}
			for(; p<8; p++)
				res[p] = 0;
			_mm_storel_epi64((__m128i*)(unshufLUT + i), _mm_loadl_epi64((__m128i*)res));
		}
	}
#endif

#ifdef __ARM_NEON
	for(int i=0; i<256; i++) {
		int k = i;
		uint8_t res[8];
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
		k = i;
		for(int j=0; j<8; j++) {
			res[j] = (k & 1) ? 192 /* == -64 */ : 0;
			k >>= 1;
		}
		vst1_u8((uint8_t*)(eqAddLUT + i), vld1_u8(res));
		
		k = i;
		p = 0;
		for(int j=0; j<8; j++) {
			if(!(k & 1)) {
				res[p++] = j;
			}
			k >>= 1;
		}
		for(; p<8; p++)
			res[p] = 0;
		vst1_u8((uint8_t*)(unshufLUT + i), vld1_u8(res));
	}
#endif
}
