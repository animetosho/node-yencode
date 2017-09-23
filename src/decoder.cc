#include "common.h"


// TODO: need to support max output length somehow

// state var: refers to the previous state - only used for incremental processing
//   0: previous characters are `\r\n` OR there is no previous character
//   1: previous character is `=`
//   2: previous character is `\r`
//   3: previous character is none of the above
template<bool isRaw>
size_t do_decode_scalar(const unsigned char* src, unsigned char* dest, size_t len, char* state) {
	unsigned char *p = dest; // destination pointer
	unsigned long i = 0; // input position
	unsigned char c; // input character
	
	if(len < 1) return 0;
	
	if(isRaw) {
		
		if(state) switch(*state) {
			case 1:
				c = src[i];
				*p++ = c - 42 - 64;
				i++;
				if(c == '\r' && i < len) {
					*state = 2;
					// fall through to case 2
				} else {
					*state = 3;
					break;
				}
			case 2:
				if(src[i] != '\n') break;
				i++;
				*state = 0; // now `\r\n`
				if(len <= i) return 0;
			case 0:
				// skip past first dot
				if(src[i] == '.') i++;
		} else // treat as *state == 0
			if(src[i] == '.') i++;
		
		for(; i + 2 < len; i++) {
			c = src[i];
			switch(c) {
				case '\r':
					// skip past \r\n. sequences
					//i += (*(uint16_t*)(src + i + 1) == UINT16_PACK('\n', '.')) << 1;
					if(*(uint16_t*)(src + i + 1) == UINT16_PACK('\n', '.'))
						i += 2;
				case '\n':
					continue;
				case '=':
					c = src[i+1];
					*p++ = c - 42 - 64;
					i += (c != '\r'); // if we have a \r, reprocess character to deal with \r\n. case
					continue;
				default:
					*p++ = c - 42;
			}
		}
		
		if(state) *state = 3;
		
		if(i+1 < len) { // 2nd last char
			c = src[i];
			switch(c) {
				case '\r':
					if(state && src[i+1] == '\n') {
						*state = 0;
						return p - dest;
					}
				case '\n':
					break;
				case '=':
					c = src[i+1];
					*p++ = c - 42 - 64;
					i += (c != '\r');
					break;
				default:
					*p++ = c - 42;
			}
			i++;
		}
		
		// do final char; we process this separately to prevent an overflow if the final char is '='
		if(i < len) {
			c = src[i];
			if(c != '\n' && c != '\r' && c != '=') {
				*p++ = c - 42;
			} else if(state) {
				if(c == '=') *state = 1;
				else if(c == '\r') *state = 2;
				else *state = 3;
			}
		}
		
	} else {
		
		if(state && *state == 1) {
			*p++ = src[i] - 42 - 64;
			i++;
			*state = 3;
		}
		
		/*for(i = 0; i < len - 1; i++) {
			c = src[i];
			if(c == '\n' || c == '\r') continue;
			unsigned char isEquals = (c == '=');
			i += isEquals;
			*p++ = src[i] - (42 + (isEquals << 6));
		}*/
		for(; i+1 < len; i++) {
			c = src[i];
			switch(c) {
				case '\n': case '\r': continue;
				case '=':
					i++;
					c = src[i] - 64;
			}
			*p++ = c - 42;
		}
		if(state) *state = 3;
		// do final char; we process this separately to prevent an overflow if the final char is '='
		if(i < len) {
			c = src[i];
			if(c != '\n' && c != '\r' && c != '=') {
				*p++ = c - 42;
			} else
				if(state) *state = (c == '=' ? 1 : 3);
		}
		
	}
	
	return p - dest;
}
#ifdef __SSE2__
uint8_t eqFixLUT[256];
ALIGN_32(__m64 eqSubLUT[256]);
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

template<bool isRaw, bool use_ssse3>
size_t do_decode_sse(const unsigned char* src, unsigned char* dest, size_t len, char* state) {
	if(len <= sizeof(__m128i)*2) return do_decode_scalar<isRaw>(src, dest, len, state);
	
	unsigned char *p = dest; // destination pointer
	unsigned long i = 0; // input position
	unsigned char escFirst = 0; // input character; first char needs escaping
	unsigned int nextMask = 0;
	char tState = 0;
	char* pState = state ? state : &tState;
	if((uintptr_t)src & ((sizeof(__m128i)-1))) {
		// find source memory alignment
		unsigned char* aSrc = (unsigned char*)(((uintptr_t)src + (sizeof(__m128i)-1)) & ~(sizeof(__m128i)-1));
		
		i = aSrc - src;
		p += do_decode_scalar<isRaw>(src, dest, i, pState);
	}
	
	// handle finicky case of \r\n. straddled across initial boundary
	if(*pState == 0 && i+1 < len && src[i] == '.')
		nextMask = 1;
	else if(*pState == 2 && i+2 < len && *(uint16_t*)(src + i) == UINT16_PACK('\n','.'))
		nextMask = 2;
	escFirst = *pState == 1;
	
	// our algorithm may perform an aligned load on the next part, of which we consider 2 bytes (for \r\n. sequence checking)
	for(; i + (sizeof(__m128i)+1) < len; i += sizeof(__m128i)) {
		__m128i data = _mm_load_si128((__m128i *)(src + i));
		
		// search for special chars
		__m128i cmpEq = _mm_cmpeq_epi8(data, _mm_set1_epi8('=')),
		cmp = _mm_or_si128(
			_mm_or_si128(
				_mm_cmpeq_epi8(data, _mm_set1_epi8('\r')),
				_mm_cmpeq_epi8(data, _mm_set1_epi8('\n'))
			),
			cmpEq
		);
		unsigned int mask = _mm_movemask_epi8(cmp); // not the most accurate mask if we have invalid sequences; we fix this up later
		
		__m128i oData;
		if(escFirst) { // rarely hit branch: seems to be faster to use 'if' than a lookup table, possibly due to values being able to be held in registers?
			// first byte needs escaping due to preceeding = in last loop iteration
			oData = _mm_sub_epi8(data, _mm_set_epi8(42,42,42,42,42,42,42,42,42,42,42,42,42,42,42,42+64));
		} else {
			oData = _mm_sub_epi8(data, _mm_set1_epi8(42));
		}
		mask &= ~escFirst;
		mask |= nextMask;
		
		if (mask != 0) {
			// a spec compliant encoder should never generate sequences: ==, =\n and =\r, but we'll handle them to be spec compliant
			// the yEnc specification requires any character following = to be unescaped, not skipped over, so we'll deal with that
			
#define LOAD_HALVES(a, b) _mm_castps_si128(_mm_loadh_pi( \
	_mm_castsi128_ps(_mm_loadl_epi64((__m128i*)(a))), \
	(b) \
))
			// firstly, resolve invalid sequences of = to deal with cases like '===='
			unsigned int maskEq = _mm_movemask_epi8(cmpEq);
			unsigned int tmp = eqFixLUT[(maskEq&0xff) & ~escFirst];
			maskEq = (eqFixLUT[(maskEq>>8) & ~(tmp>>7)] << 8) | tmp;
			
			escFirst = (maskEq >> (sizeof(__m128i)-1));
			// next, eliminate anything following a `=` from the special char mask; this eliminates cases of `=\r` so that they aren't removed
			maskEq <<= 1;
			mask &= ~maskEq;
			
			// unescape chars following `=`
			oData = _mm_sub_epi8(
				oData,
				LOAD_HALVES(
					eqSubLUT + (maskEq&0xff),
					eqSubLUT + ((maskEq>>8)&0xff)
				)
			);
			
			// handle \r\n. sequences
			// RFC3977 requires the first dot on a line to be stripped, due to dot-stuffing
			if(isRaw) {
				__m128i nextData = _mm_load_si128((__m128i *)(src + i) + 1);
				// find instances of \r\n
				__m128i tmpData1, tmpData2;
#ifdef __SSSE3__
				if(use_ssse3) {
					tmpData1 = _mm_alignr_epi8(nextData, data, 1);
					tmpData2 = _mm_alignr_epi8(nextData, data, 2);
				} else {
#endif
# define ALIGNR(a, b, i) _mm_or_si128(_mm_slli_si128(a, sizeof(__m128i)-(i)), _mm_srli_si128(b, i))
					// TODO: consider using PINSRW instead
					// - first load may involve a cache-line crossing, but we shouldn't need this for Intel CPUs, so avoid cacheline split problems
					tmpData1 = ALIGNR(nextData, data, 1);
					tmpData2 = ALIGNR(nextData, data, 2);
# undef ALIGNR
#ifdef __SSSE3__
				}
#endif
				__m128i cmp1 = _mm_cmpeq_epi16(data, _mm_set1_epi16(0x0a0d));
				__m128i cmp2 = _mm_cmpeq_epi16(tmpData1, _mm_set1_epi16(0x0a0d));
				// merge the two comparisons
				cmp1 = _mm_or_si128(_mm_srli_si128(cmp1, 1), cmp2);
				// then check if there's a . after any of these instances
				tmpData2 = _mm_cmpeq_epi8(tmpData2, _mm_set1_epi8('.'));
				// grab bit-mask of matched . characters and OR with mask
				unsigned int killDots = _mm_movemask_epi8(_mm_and_si128(tmpData2, cmp1));
				mask |= (killDots << 2) & 0xffff;
				nextMask = killDots >> (sizeof(__m128i)-2);
			}
			
			// all that's left is to 'compress' the data (skip over masked chars)
#ifdef __SSSE3__
			if(use_ssse3) {
# ifdef __POPCNT__
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
				shuf = _mm_shuffle_epi8(shuf, _mm_load_si128(pshufb_combine_table + skipped));
				
				// shuffle data
				oData = _mm_shuffle_epi8(oData, shuf);
				STOREU_XMM(p, oData);
				
				// increment output position
# ifdef __POPCNT__
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
			nextMask = 0;
		}
	}
	
	if(escFirst) *pState = 1; // escape next character
	else if(nextMask == 1) *pState = 0; // next character is '.', where previous two were \r\n
	else if(nextMask == 2) *pState = 2; // next characters are '\n.', previous is \r
	else *pState = 3;
	
	// end alignment
	if(i < len) {
		p += do_decode_scalar<isRaw>(src + i, p, len - i, pState);
	}
	
	return p - dest;
}
#endif


size_t (*_do_decode)(const unsigned char*, unsigned char*, size_t, char*) = &do_decode_scalar<false>;
size_t (*_do_decode_raw)(const unsigned char*, unsigned char*, size_t, char*) = &do_decode_scalar<true>;

void decoder_init() {
#ifdef __SSE2__
	_do_decode = &do_decode_sse<false, false>;
	_do_decode_raw = &do_decode_sse<true, false>;
	// generate unshuf LUT
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
			res[j] = (k & 1) << 6;
			k >>= 1;
		}
		_mm_storel_epi64((__m128i*)(eqSubLUT + i), _mm_loadl_epi64((__m128i*)res));
	}
#endif
#ifdef __SSSE3__
	if((cpu_flags() & CPU_SHUFFLE_FLAGS) == CPU_SHUFFLE_FLAGS) {
		_do_decode = &do_decode_sse<false, true>;
		_do_decode_raw = &do_decode_sse<true, true>;
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
}
