
extern size_t (*_do_decode)(const unsigned char*, unsigned char*, size_t, char*);
extern size_t (*_do_decode_raw)(const unsigned char*, unsigned char*, size_t, char*);
#define do_decode (*_do_decode)
#define do_decode_raw (*_do_decode_raw)

void decoder_init();
