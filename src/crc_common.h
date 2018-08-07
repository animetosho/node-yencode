
#define PACK_4(arr) (((uint_fast32_t)arr[0] << 24) | ((uint_fast32_t)arr[1] << 16) | ((uint_fast32_t)arr[2] << 8) | (uint_fast32_t)arr[3])
#define UNPACK_4(arr, val) { \
	arr[0] = (unsigned char)(val >> 24) & 0xFF; \
	arr[1] = (unsigned char)(val >> 16) & 0xFF; \
	arr[2] = (unsigned char)(val >>  8) & 0xFF; \
	arr[3] = (unsigned char)val & 0xFF; \
}

#include <stddef.h> // for size_t
typedef void (*crc_func)(const void*, size_t, unsigned char[4]);
