

#define do_decode do_decode_scalar

size_t do_decode_scalar_raw(const unsigned char* src, unsigned char* dest, size_t len, char* state);
size_t do_decode_scalar(const unsigned char* src, unsigned char* dest, size_t len, char* state, bool isRaw);
size_t do_decode_sse(const unsigned char* src, unsigned char* dest, size_t len, char* state, bool isRaw);
void decoder_init();
