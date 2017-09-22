

#define do_decode do_decode_scalar

template<bool isRaw>
size_t do_decode_scalar(const unsigned char* src, unsigned char* dest, size_t len, char* state);
template<bool isRaw>
size_t do_decode_sse(const unsigned char* src, unsigned char* dest, size_t len, char* state);

void decoder_init();
