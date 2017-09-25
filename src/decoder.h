
extern size_t (*_do_decode)(const unsigned char*, unsigned char*, size_t, char*);
extern size_t (*_do_decode_raw)(const unsigned char*, unsigned char*, size_t, char*);

template<bool isRaw>
static inline size_t do_decode(const unsigned char* src, unsigned char* dest, size_t len, char* state) {
	return (*(isRaw ? _do_decode_raw : _do_decode))(src, dest, len, state);
}

void decoder_init();
