#include "hedley.h"

extern size_t (*_do_encode)(int, int*, const unsigned char* HEDLEY_RESTRICT, unsigned char* HEDLEY_RESTRICT, size_t);
#define do_encode (*_do_encode)
void encoder_init();
