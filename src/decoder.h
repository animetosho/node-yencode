
typedef enum {
	YDEC_STATE_CRLF, // default
	YDEC_STATE_EQ,
	YDEC_STATE_CR,
	YDEC_STATE_NONE,
	YDEC_STATE_CRLFDT,
	YDEC_STATE_CRLFDTCR,
	YDEC_STATE_CRLFEQ // may actually be "\r\n.=" in raw decoder
} YencDecoderState;

typedef enum {
	YDEC_END_NONE,    // end not reached
	YDEC_END_CONTROL, // \r\n=y sequence found, src points to byte after 'y'
	YDEC_END_ARTICLE  // \r\n.\r\n sequence found, src points to byte after last '\n'
} YencDecoderEnd;

#include "hedley.h"

extern YencDecoderEnd (*_do_decode)(const unsigned char*HEDLEY_RESTRICT*, unsigned char*HEDLEY_RESTRICT*, size_t, YencDecoderState*);
extern YencDecoderEnd (*_do_decode_raw)(const unsigned char*HEDLEY_RESTRICT*, unsigned char*HEDLEY_RESTRICT*, size_t, YencDecoderState*);
extern YencDecoderEnd (*_do_decode_end_raw)(const unsigned char*HEDLEY_RESTRICT*, unsigned char*HEDLEY_RESTRICT*, size_t, YencDecoderState*);

template<bool isRaw>
static inline size_t do_decode(const unsigned char* HEDLEY_RESTRICT src, unsigned char* HEDLEY_RESTRICT dest, size_t len, YencDecoderState* state) {
	unsigned char* ds = dest;
	(*(isRaw ? _do_decode_raw : _do_decode))(&src, &ds, len, state);
	return ds - dest;
}

static inline YencDecoderEnd do_decode_end(const unsigned char*HEDLEY_RESTRICT* src, unsigned char*HEDLEY_RESTRICT* dest, size_t len, YencDecoderState* state) {
	return _do_decode_end_raw(src, dest, len, state);
}

void decoder_init();
