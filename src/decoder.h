
typedef enum {
	YDEC_STATE_CRLF, // default
	YDEC_STATE_EQ,
	YDEC_STATE_CR,
	YDEC_STATE_NONE,
	YDEC_STATE_CRLFDT,
	YDEC_STATE_CRLFDTCR,
	YDEC_STATE_CRLFEQ
} YencDecoderState;

extern int (*_do_decode)(const unsigned char**, unsigned char**, size_t, YencDecoderState*);
extern int (*_do_decode_raw)(const unsigned char**, unsigned char**, size_t, YencDecoderState*);
extern int (*_do_decode_end)(const unsigned char**, unsigned char**, size_t, YencDecoderState*);
extern int (*_do_decode_end_raw)(const unsigned char**, unsigned char**, size_t, YencDecoderState*);

template<bool isRaw>
static inline size_t do_decode(const unsigned char* src, unsigned char* dest, size_t len, YencDecoderState* state) {
	unsigned char* ds = dest;
	(*(isRaw ? _do_decode_raw : _do_decode))(&src, &ds, len, state);
	return ds - dest;
}

template<bool isRaw>
static inline int do_decode_end(const unsigned char** src, unsigned char** dest, size_t len, YencDecoderState* state) {
	return (*(isRaw ? _do_decode_end_raw : _do_decode_end))(src, dest, len, state);
}

void decoder_init();
