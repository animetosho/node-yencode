
typedef enum {
	YDEC_STATE_CRLF, // default
	YDEC_STATE_EQ,
	YDEC_STATE_CR,
	YDEC_STATE_NONE,
	YDEC_STATE_CRLFDT,
	YDEC_STATE_CRLFDTCR,
	YDEC_STATE_CRLFEQ
} YencDecoderState;

extern size_t (*_do_decode)(const unsigned char*, unsigned char*, size_t, YencDecoderState*);
extern size_t (*_do_decode_raw)(const unsigned char*, unsigned char*, size_t, YencDecoderState*);

template<bool isRaw>
static inline size_t do_decode(const unsigned char* src, unsigned char* dest, size_t len, YencDecoderState* state) {
	return (*(isRaw ? _do_decode_raw : _do_decode))(src, dest, len, state);
}

void decoder_init();
