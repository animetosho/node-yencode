// can't seem to make this worth it
#include "common.h"
#ifdef __AVX2__

#include "encoder.h"
#include "encoder_common.h"
#define YMM_SIZE 32

#if defined(__x86_64__) || \
    defined(__amd64__ ) || \
    defined(__LP64    ) || \
    defined(_M_X64    ) || \
    defined(_M_AMD64  ) || \
    defined(_WIN64    )
# define PLATFORM_AMD64 1
#endif

#pragma pack(16)
struct TShufMix {
	__m128i shuf, mix;
};
#pragma pack()

static struct {
	const int8_t ALIGN_TO(64, expand_mergemix[65*2*64]);
	__m256i ALIGN_TO(32, shufExpand[65536]); // huge 2MB table
	const uint32_t ALIGN_TO(32, maskMixEOL[4*8]);
	uint32_t expand[65536]; // biggish 256KB table (but still smaller than the 2MB table)
	const int8_t ALIGN_TO(32, perm_expand[65*32]);
} lookups = {
	/*expand_mergemix*/ {
		#define _X2(n,k) n>=k?-1:0
		#define _X(n) _X2(n,0), _X2(n,1), _X2(n,2), _X2(n,3), _X2(n,4), _X2(n,5), _X2(n,6), _X2(n,7), \
			_X2(n,8), _X2(n,9), _X2(n,10), _X2(n,11), _X2(n,12), _X2(n,13), _X2(n,14), _X2(n,15), \
			_X2(n,16), _X2(n,17), _X2(n,18), _X2(n,19), _X2(n,20), _X2(n,21), _X2(n,22), _X2(n,23), \
			_X2(n,24), _X2(n,25), _X2(n,26), _X2(n,27), _X2(n,28), _X2(n,29), _X2(n,30), _X2(n,31), \
			_X2(n,32), _X2(n,33), _X2(n,34), _X2(n,35), _X2(n,36), _X2(n,37), _X2(n,38), _X2(n,39), \
			_X2(n,40), _X2(n,41), _X2(n,42), _X2(n,43), _X2(n,44), _X2(n,45), _X2(n,46), _X2(n,47), \
			_X2(n,48), _X2(n,49), _X2(n,50), _X2(n,51), _X2(n,52), _X2(n,53), _X2(n,54), _X2(n,55), \
			_X2(n,56), _X2(n,57), _X2(n,58), _X2(n,59), _X2(n,60), _X2(n,61), _X2(n,62), _X2(n,63)
		#define _Y2(n, m) '='*(n==m) + 64*(n==m-1) + 42*(n!=m)
		#define _Y(n) _Y2(n,0), _Y2(n,1), _Y2(n,2), _Y2(n,3), _Y2(n,4), _Y2(n,5), _Y2(n,6), _Y2(n,7), \
			_Y2(n,8), _Y2(n,9), _Y2(n,10), _Y2(n,11), _Y2(n,12), _Y2(n,13), _Y2(n,14), _Y2(n,15), \
			_Y2(n,16), _Y2(n,17), _Y2(n,18), _Y2(n,19), _Y2(n,20), _Y2(n,21), _Y2(n,22), _Y2(n,23), \
			_Y2(n,24), _Y2(n,25), _Y2(n,26), _Y2(n,27), _Y2(n,28), _Y2(n,29), _Y2(n,30), _Y2(n,31), \
			_Y2(n,32), _Y2(n,33), _Y2(n,34), _Y2(n,35), _Y2(n,36), _Y2(n,37), _Y2(n,38), _Y2(n,39), \
			_Y2(n,40), _Y2(n,41), _Y2(n,42), _Y2(n,43), _Y2(n,44), _Y2(n,45), _Y2(n,46), _Y2(n,47), \
			_Y2(n,48), _Y2(n,49), _Y2(n,50), _Y2(n,51), _Y2(n,52), _Y2(n,53), _Y2(n,54), _Y2(n,55), \
			_Y2(n,56), _Y2(n,57), _Y2(n,58), _Y2(n,59), _Y2(n,60), _Y2(n,61), _Y2(n,62), _Y2(n,63)
		#define _XY(n) _X(n), _Y(n)
			_XY(63), _XY(62), _XY(61), _XY(60), _XY(59), _XY(58), _XY(57), _XY(56),
			_XY(55), _XY(54), _XY(53), _XY(52), _XY(51), _XY(50), _XY(49), _XY(48),
			_XY(47), _XY(46), _XY(45), _XY(44), _XY(43), _XY(42), _XY(41), _XY(40),
			_XY(39), _XY(38), _XY(37), _XY(36), _XY(35), _XY(34), _XY(33), _XY(32),
			_XY(31), _XY(30), _XY(29), _XY(28), _XY(27), _XY(26), _XY(25), _XY(24),
			_XY(23), _XY(22), _XY(21), _XY(20), _XY(19), _XY(18), _XY(17), _XY(16),
			_XY(15), _XY(14), _XY(13), _XY(12), _XY(11), _XY(10), _XY( 9), _XY( 8),
			_XY( 7), _XY( 6), _XY( 5), _XY( 4), _XY( 3), _XY( 2), _XY( 1), _XY( 0),
			_XY(64)
		#undef _XY
		#undef _Y
		#undef _Y2
		#undef _X
		#undef _X2
	},
	
	/*shufExpand*/ {},
	/*maskMixEOL*/ {
		// first row is an AND mask, rest is PSHUFB table
		0xff0000ff, 0x00000000, 0, 0,    0x2a0a0d2a, 0x00000000, 0, 0,
		
		0xffff00ff, 0xffffff01, 0, 0,    0x0a0d6a3d, 0x0000002a, 0, 0,
		0xffffff00, 0xffffff01, 0, 0,    0x3d0a0d2a, 0x0000006a, 0, 0,
		0xffff00ff, 0xffff01ff, 0, 0,    0x0a0d6a3d, 0x00006a3d, 0, 0
	},
	/*expand*/ {},
	/*perm_expand*/ {
		#define _X2(n,k) (n==k) ? '=' : k-(n<k)
		#define _X(n) _X2(n,32), _X2(n,33), _X2(n,34), _X2(n,35), _X2(n,36), _X2(n,37), _X2(n,38), _X2(n,39), \
			_X2(n,40), _X2(n,41), _X2(n,42), _X2(n,43), _X2(n,44), _X2(n,45), _X2(n,46), _X2(n,47), \
			_X2(n,48), _X2(n,49), _X2(n,50), _X2(n,51), _X2(n,52), _X2(n,53), _X2(n,54), _X2(n,55), \
			_X2(n,56), _X2(n,57), _X2(n,58), _X2(n,59), _X2(n,60), _X2(n,61), _X2(n,62), _X2(n,63)
		_X(63), _X(62), _X(61), _X(60), _X(59), _X(58), _X(57), _X(56),
		_X(55), _X(54), _X(53), _X(52), _X(51), _X(50), _X(49), _X(48),
		_X(47), _X(46), _X(45), _X(44), _X(43), _X(42), _X(41), _X(40),
		_X(39), _X(38), _X(37), _X(36), _X(35), _X(34), _X(33), _X(32),
		_X(31), _X(30), _X(29), _X(28), _X(27), _X(26), _X(25), _X(24),
		_X(23), _X(22), _X(21), _X(20), _X(19), _X(18), _X(17), _X(16),
		_X(15), _X(14), _X(13), _X(12), _X(11), _X(10), _X( 9), _X( 8),
		_X( 7), _X( 6), _X( 5), _X( 4), _X( 3), _X( 2), _X( 1), _X( 0),
		_X(64)
		#undef _X2
		#undef _X
	}
};


#if defined(__AVX512VBMI2__) && defined(__AVX512VL__)
static void encoder_avx_vbmi2_lut() {
	for(int i=0; i<65536; i++) {
		int k = i;
		uint32_t expand = 0;
		int p = 0;
		for(int j=0; j<16; j++) {
			if(k & 1) {
				p++;
			}
			expand |= 1<<(j+p);
			k >>= 1;
		}
		lookups.expand[i] = expand;
	}
}
#endif

static void encoder_avx2_lut() {
	for(int i=0; i<65536; i++) {
		int k = i;
		uint8_t* res = (uint8_t*)(lookups.shufExpand + i);
		int p = 0;
		for(int j=0; j<16; j++) {
			if(k & 1) {
				res[j+p] = 0xff;
				p++;
			}
			res[j+p] = j;
			k >>= 1;
		}
		for(; p<16; p++)
			res[16+p] = 0x40; // arbitrary value (top bit cannot be set)
	}
}


static HEDLEY_ALWAYS_INLINE void encode_eol_handle_pre(const uint8_t* HEDLEY_RESTRICT es, long& i, uint8_t*& p, long& col, long lineSizeOffset) {
	__m256i magic = _mm256_set_epi8( // vector re-used for lookup-table in main loop
		'='-42,'='-42,'\r'-42,'\r'-42,'\t'-42,'\n'-42,'\n'-42,'\t'-42,'.'-42,'='-42,' '-42,' '-42,'\0'-42,-'\n',-'\r','\0'-42,
		'='-42,'='-42,'\r'-42,'\r'-42,'\t'-42,'\n'-42,'\n'-42,'\t'-42,'.'-42,'='-42,' '-42,' '-42,'\0'-42,-'\n',-'\r','\0'-42
	);
	__m128i lineChars = _mm_set1_epi16(*(uint16_t*)(es+i)); // unfortunately, _mm_broadcastw_epi16 requires __m128i argument
	unsigned testChars = _mm_movemask_epi8(_mm_cmpeq_epi8(
		lineChars, _mm256_castsi256_si128(magic)
	));
	if(HEDLEY_UNLIKELY(testChars & 0xfff9)) {
		unsigned esc1stChar = (testChars & 0x5551) != 0;
		unsigned esc2ndChar = (testChars & 0xaaa8) != 0;
		unsigned lut = (esc1stChar + esc2ndChar*2)*2;
		lineChars = _mm_shuffle_epi8(lineChars, _mm_load_si128((const __m128i*)lookups.maskMixEOL + lut));
		lineChars = _mm_add_epi8(lineChars, _mm_load_si128((const __m128i*)lookups.maskMixEOL + lut+1));
		_mm_storel_epi64((__m128i*)p, lineChars);
		col = lineSizeOffset + esc2ndChar;
		p += 4 + esc1stChar + esc2ndChar;
	} else {
		lineChars = _mm_and_si128(lineChars, _mm_cvtsi32_si128(0xff0000ff));
		lineChars = _mm_sub_epi8(lineChars, _mm256_castsi256_si128(magic));
		*(int*)p = _mm_cvtsi128_si32(lineChars);
		col = lineSizeOffset;
		p += 4;
	}
	i += 2;
}

template<enum YEncDecIsaLevel use_isa>
HEDLEY_ALWAYS_INLINE void do_encode_avx2(int line_size, int* colOffset, const uint8_t* HEDLEY_RESTRICT srcEnd, uint8_t* HEDLEY_RESTRICT& dest, size_t& len) {
	if(len < 2 || line_size < 4) return;
	
	uint8_t *p = dest; // destination pointer
	long i = -(long)len; // input position
	long lineSizeOffset = -line_size +2; // line size excluding first/last char
	long col = *colOffset + lineSizeOffset -1;
	
	// offset position to enable simpler loop condition checking
	const int INPUT_OFFSET = YMM_SIZE*2 + 2 -1; // extra 2 chars for EOL handling, -1 to change <= to <
	i += INPUT_OFFSET;
	const uint8_t* es = srcEnd - INPUT_OFFSET;
	
	if (LIKELIHOOD(0.999, col == lineSizeOffset-1)) {
		uint8_t c = es[i++];
		if (LIKELIHOOD(0.0273, escapedLUT[c] != 0)) {
			*(uint16_t*)p = escapedLUT[c];
			p += 2;
			col += 2;
		} else {
			*(p++) = c + 42;
			col += 1;
		}
	}
	if(LIKELIHOOD(0.001, col >= 0)) {
		if(col == 0)
			encode_eol_handle_pre(es, i, p, col, lineSizeOffset);
		else {
			uint8_t c = es[i++];
			if (LIKELIHOOD(0.0273, escapedLUT[c]!=0)) {
				*(uint32_t*)p = UINT32_16_PACK(UINT16_PACK('\r', '\n'), (uint32_t)escapedLUT[c]);
				p += 4;
				col = 1+lineSizeOffset;
			} else {
				*(uint32_t*)p = UINT32_PACK('\r', '\n', (uint32_t)(c+42), 0);
				p += 3;
				col = lineSizeOffset;
			}
		}
	}
	while(i < 0) {
		__m256i dataA = _mm256_loadu_si256((__m256i *)(es + i));
		__m256i dataB = _mm256_loadu_si256((__m256i *)(es + i) + 1);
		i += YMM_SIZE*2;
		// search for special chars
		__m256i cmpA = _mm256_cmpeq_epi8(
			_mm256_shuffle_epi8(_mm256_set_epi8( // vector re-used for EOL matching, so has additional elements
				'='-42,'='-42,'\r'-42,'\r'-42,'\t'-42,'\n'-42,'\n'-42,'\t'-42,'.'-42,'='-42,' '-42,' '-42,'\0'-42,-'\n',-'\r','\0'-42,
				'='-42,'='-42,'\r'-42,'\r'-42,'\t'-42,'\n'-42,'\n'-42,'\t'-42,'.'-42,'='-42,' '-42,' '-42,'\0'-42,-'\n',-'\r','\0'-42
			), _mm256_adds_epi8(dataA, _mm256_set1_epi8(80+42))),
			dataA
		);
		__m256i cmpB = _mm256_cmpeq_epi8(
			_mm256_shuffle_epi8(_mm256_set_epi8(
				'='-42,'='-42,'\r'-42,'\r'-42,'\t'-42,'\n'-42,'\n'-42,'\t'-42,'.'-42,'='-42,' '-42,' '-42,'\0'-42,-'\n',-'\r','\0'-42,
				'='-42,'='-42,'\r'-42,'\r'-42,'\t'-42,'\n'-42,'\n'-42,'\t'-42,'.'-42,'='-42,' '-42,' '-42,'\0'-42,-'\n',-'\r','\0'-42
			), _mm256_adds_epi8(dataB, _mm256_set1_epi8(80+42))),
			dataB
		);
		
#if defined(__AVX512VL__)
		if(use_isa >= ISA_LEVEL_AVX3) {
			dataA = _mm256_add_epi8(dataA, _mm256_set1_epi8(42));
			dataA = _mm256_ternarylogic_epi32(dataA, cmpA, _mm256_set1_epi8(64), 0xf8); // data | (cmp & 64)
			dataB = _mm256_add_epi8(dataB, _mm256_set1_epi8(42));
			dataB = _mm256_ternarylogic_epi32(dataB, cmpB, _mm256_set1_epi8(64), 0xf8); // data | (cmp & 64)
		}
#endif
		
		uint32_t maskA = (uint32_t)_mm256_movemask_epi8(cmpA);
		uint32_t maskB = (uint32_t)_mm256_movemask_epi8(cmpB);
		uint64_t mask = ((uint64_t)maskB << 32) | (uint64_t)maskA;
#ifdef PLATFORM_AMD64
		unsigned int maskBits = _mm_popcnt_u64(mask);
#else
		unsigned int maskBits = popcnt32(maskA) + popcnt32(maskB);
#endif
		unsigned int outputBytes = maskBits+YMM_SIZE*2;
		long bitIndex;
		// unlike the SSE (128-bit) encoder, the probability of at least one escaped character in a vector is much higher here, which causes the branch to be relatively unpredictable, resulting in poor performance
		// because of this, we tilt the probability towards the fast path by process single-character escape cases there; this results in a speedup, despite the fast path being slower
		// likelihood of >1 bit set: 1-((63/64)^64 + (63/64)^63 * (1/64) * 64C1)
		if (LIKELIHOOD(0.264, maskBits > 1)) {
			unsigned int m1 = mask & 0xffff;
			unsigned int m2, m3, m4;
			__m256i data1A, data2A;
			__m256i data1B, data2B;
			__m256i shuf1A, shuf1B; // not set in VBMI2 path
			__m256i shuf2A, shuf2B; // not set in VBMI2 path
			
#if defined(__AVX512VBMI2__) && defined(__AVX512VL__) && defined(__AVX512BW__)
			if(use_isa >= ISA_LEVEL_VBMI2) {
				m2 = maskA >> 16;
				m3 = maskB & 0xffff;
				m4 = maskB >> 16;
				
				/* alternative no-LUT strategy
				uint64_t expandMask = _pdep_u64(mask, 0x5555555555555555); // expand bits
				expandMask = _pext_u64(~expandMask, expandMask|0xaaaaaaaaaaaaaaaa);
				*/
				
				data1A = _mm256_mask_expand_epi8(_mm256_set1_epi8('='), lookups.expand[m1], dataA);
				data2A = _mm256_mask_expand_epi8(_mm256_set1_epi8('='), lookups.expand[m2], _mm256_castsi128_si256(
					_mm256_extracti128_si256(dataA, 1)
				));
				data1B = _mm256_mask_expand_epi8(_mm256_set1_epi8('='), lookups.expand[m3], dataB);
				data2B = _mm256_mask_expand_epi8(_mm256_set1_epi8('='), lookups.expand[m4], _mm256_castsi128_si256(
					_mm256_extracti128_si256(dataB, 1)
				));
			} else
#endif
			{
				if(use_isa < ISA_LEVEL_AVX3) {
					dataA = _mm256_add_epi8(dataA, _mm256_blendv_epi8(_mm256_set1_epi8(42), _mm256_set1_epi8(42+64), cmpA));
					dataB = _mm256_add_epi8(dataB, _mm256_blendv_epi8(_mm256_set1_epi8(42), _mm256_set1_epi8(42+64), cmpB));
				}
				
				m2 = (mask >> 11) & 0x1fffe0;
				m3 = (mask >> 27) & 0x1fffe0;
				m4 = (mask >> 43) & 0x1fffe0;
				
				// duplicate halves
				data1A = _mm256_inserti128_si256(dataA, _mm256_castsi256_si128(dataA), 1);
				data2A = _mm256_permute4x64_epi64(dataA, 0xee);
				data1B = _mm256_inserti128_si256(dataB, _mm256_castsi256_si128(dataB), 1);
				data2B = _mm256_permute4x64_epi64(dataB, 0xee);
				
				shuf1A = _mm256_load_si256(lookups.shufExpand + m1);
				shuf2A = _mm256_load_si256((__m256i*)((char*)lookups.shufExpand + m2));
				shuf1B = _mm256_load_si256((__m256i*)((char*)lookups.shufExpand + m3));
				shuf2B = _mm256_load_si256((__m256i*)((char*)lookups.shufExpand + m4));
				
				// expand
				data1A = _mm256_shuffle_epi8(data1A, shuf1A);
				data2A = _mm256_shuffle_epi8(data2A, shuf2A);
				data1B = _mm256_shuffle_epi8(data1B, shuf1B);
				data2B = _mm256_shuffle_epi8(data2B, shuf2B);
				// add in '='
				data1A = _mm256_blendv_epi8(data1A, _mm256_set1_epi8('='), shuf1A);
				data2A = _mm256_blendv_epi8(data2A, _mm256_set1_epi8('='), shuf2A);
				data1B = _mm256_blendv_epi8(data1B, _mm256_set1_epi8('='), shuf1B);
				data2B = _mm256_blendv_epi8(data2B, _mm256_set1_epi8('='), shuf2B);
			}
			
			unsigned char shuf1Len = popcnt32(m1) + 16;
			unsigned char shuf2Len = popcnt32(maskA) + 32;
			unsigned char shuf3Len = outputBytes - popcnt32(m4) - 16;
			_mm256_storeu_si256((__m256i*)p, data1A);
			_mm256_storeu_si256((__m256i*)(p + shuf1Len), data2A);
			_mm256_storeu_si256((__m256i*)(p + shuf2Len), data1B);
			_mm256_storeu_si256((__m256i*)(p + shuf3Len), data2B);
			p += outputBytes;
			col += outputBytes;
			
			if(col >= 0) {
				// we overflowed - find correct position to revert back to
				// this is perhaps sub-optimal on 32-bit, but who still uses that with AVX2?
				uint64_t eqMask1, eqMask2;
				uint64_t eqMask3, eqMask4;
				uint64_t eqMask;
				if(HEDLEY_UNLIKELY(col >= outputBytes-shuf2Len)) {
#if defined(__AVX512VBMI2__) && defined(__AVX512VL__) && defined(__AVX512BW__)
					if(use_isa >= ISA_LEVEL_VBMI2) {
						eqMask1 = lookups.expand[m1];
						eqMask2 = lookups.expand[m2];
					} else
#endif
					{
						eqMask1 = (uint32_t)_mm256_movemask_epi8(shuf1A);
						eqMask2 = (uint32_t)_mm256_movemask_epi8(shuf2A);
					}
					eqMask = eqMask1 | (eqMask2 << shuf1Len);
					if(use_isa >= ISA_LEVEL_VBMI2)
						i -= YMM_SIZE;
					else
						i += outputBytes-shuf2Len - YMM_SIZE;
				} else {
#if defined(__AVX512VBMI2__) && defined(__AVX512VL__) && defined(__AVX512BW__)
					if(use_isa >= ISA_LEVEL_VBMI2) {
						eqMask3 = lookups.expand[m3];
						eqMask4 = lookups.expand[m4];
					} else
#endif
					{
						eqMask3 = (uint32_t)_mm256_movemask_epi8(shuf1B);
						eqMask4 = (uint32_t)_mm256_movemask_epi8(shuf2B);
					}
					eqMask = eqMask3 | (eqMask4 << (shuf3Len-shuf2Len));
					outputBytes -= shuf2Len;
				}
				
#if defined(__GNUC__) && defined(PLATFORM_AMD64)
				if(use_isa >= ISA_LEVEL_VBMI2) {
					asm(
						"shrq $1, %[eqMask] \n"
						"shrq %%cl, %[eqMask] \n"
						"adcq %[col], %[p] \n"
						: [eqMask]"+r"(eqMask), [p]"+r"(p)
						: "c"(outputBytes - col -1), [col]"r"(~col)
					);
					i -= _mm_popcnt_u64(eqMask);
				} else
#endif
				{
					eqMask >>= outputBytes - col -1;
					unsigned int bitCount;
#ifdef PLATFORM_AMD64
					bitCount = (unsigned int)_mm_popcnt_u64(eqMask);
#else
					bitCount = popcnt32(eqMask & 0xffffffff) + popcnt32(eqMask >> 32);
#endif
#if defined(__AVX512VBMI2__) && defined(__AVX512VL__) && defined(__AVX512BW__)
					if(use_isa >= ISA_LEVEL_VBMI2) {
						i -= bitCount;
						p -= col;
						if(LIKELIHOOD(0.98, (eqMask & 1) != 1))
							p--;
						else
							i++;
					} else
#endif
					{
						i += bitCount;
						long revert = col + (eqMask & 1);
						p -= revert;
						i -= revert;
					}
				}
				goto _encode_eol_handle_pre;
			}
		} else {
#if defined(__AVX512VL__) && defined(__AVX512BW__)
			if(use_isa >= ISA_LEVEL_AVX3) {
				// store last byte
				_mm256_mask_storeu_epi8(p+YMM_SIZE+1, 1<<31, dataB);
				
				uint64_t blendMask = ~(mask-1);
				dataB = _mm256_mask_alignr_epi8(
					dataB,
					blendMask>>32,
					dataB,
					_mm256_permute2x128_si256(dataA, dataB, 0x21),
					15
				);
				dataB = _mm256_mask_blend_epi8(mask>>32, dataB, _mm256_set1_epi8('='));
				
# if defined(__AVX512VBMI2__)
				if(use_isa >= ISA_LEVEL_VBMI2) {
					dataA = _mm256_mask_expand_epi8(_mm256_set1_epi8('='), ~maskA, dataA);
				} else
# endif
				{
					__m256i swapped = _mm256_permute4x64_epi64(dataA, _MM_SHUFFLE(1,0,3,2));
					//p[YMM_SIZE*2] = _mm256_extract_epi8(swapped, 15); // = 3 uops on SKX/ICL, but masked store is 2 uops
					dataA = _mm256_mask_alignr_epi8(dataA, blendMask, dataA, swapped, 15);
					dataA = _mm256_ternarylogic_epi32(dataA, cmpA, _mm256_set1_epi8('='), 0xb8); // (data & ~cmp) | (cmp & '=')
				}
			} else
#endif
			{
#ifdef PLATFORM_AMD64
				bitIndex = _lzcnt_u64(mask);
#else
				bitIndex = _lzcnt_u32(maskA) + _lzcnt_u32(maskB);
				bitIndex += (maskA != 0)*32;
#endif
				__m256i mergeMaskA = _mm256_load_si256((const __m256i*)lookups.expand_mergemix + bitIndex*4);
				__m256i mergeMaskB = _mm256_load_si256((const __m256i*)lookups.expand_mergemix + bitIndex*4 + 1);
				// to deal with the pain of lane crossing, use shift + mask/blend
				__m256i dataAShifted = _mm256_alignr_epi8(
					dataA,
					_mm256_inserti128_si256(dataA, _mm256_castsi256_si128(dataA), 1),
					15
				);
				__m256i dataBShifted = _mm256_alignr_epi8(
					dataB,
					_mm256_permute2x128_si256(dataA, dataB, 0x21),
					15
				);
				dataA = _mm256_andnot_si256(cmpA, dataA);
				dataB = _mm256_andnot_si256(cmpB, dataB);
				dataA = _mm256_blendv_epi8(dataAShifted, dataA, mergeMaskA);
				dataB = _mm256_blendv_epi8(dataBShifted, dataB, mergeMaskB);
				
				dataA = _mm256_add_epi8(dataA, _mm256_load_si256((const __m256i*)lookups.expand_mergemix + bitIndex*4 + 2));
				dataB = _mm256_add_epi8(dataB, _mm256_load_si256((const __m256i*)lookups.expand_mergemix + bitIndex*4 + 3));
				p[YMM_SIZE*2] = es[i-1] + 42 + (64 & (mask>>(YMM_SIZE-1-6)));
			}
			// store main + additional char
			_mm256_storeu_si256((__m256i*)p, dataA);
			_mm256_storeu_si256((__m256i*)p + 1, dataB);
			p += outputBytes;
			col += outputBytes;
			
			if(LIKELIHOOD(0.3, col >= 0)) {
				if(use_isa >= ISA_LEVEL_AVX3) {
#ifdef PLATFORM_AMD64
					bitIndex = _lzcnt_u64(mask);
#else
					bitIndex = _lzcnt_u32(maskA) + _lzcnt_u32(maskB);
					bitIndex += (maskA != 0)*32;
#endif
				}
				bitIndex++;
				
				if(HEDLEY_UNLIKELY(col == bitIndex)) {
					// this is an escape character, so line will need to overflow
					p--;
				} else {
					i += (col > bitIndex);
				}
				i -= col;
				p -= col;
				
				_encode_eol_handle_pre:
				encode_eol_handle_pre(es, i, p, col, lineSizeOffset);
			}
		}
	}
	
	_mm256_zeroupper();
	
	*colOffset = col - lineSizeOffset +1;
	dest = p;
	len = -(i - INPUT_OFFSET);
}

#endif
