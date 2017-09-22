
extern size_t (*_do_encode)(int, int*, const unsigned char*, unsigned char*, size_t);
#define do_encode (*_do_encode)
void encoder_init();
