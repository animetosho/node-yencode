#ifndef __YENC_ENCODER_H
#define __YENC_ENCODER_H

#ifdef __cplusplus
extern "C" {
#endif



#include "hedley.h"

extern size_t (*_do_encode)(int, int*, const unsigned char* HEDLEY_RESTRICT, unsigned char* HEDLEY_RESTRICT, size_t, int);
#define do_encode (*_do_encode)
void encoder_init();



#ifdef __cplusplus
}
#endif
#endif
