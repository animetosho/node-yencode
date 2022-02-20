#include "common.h"
#include <stddef.h> // for size_t
typedef uint32_t (*crc_func)(const void*, size_t, uint32_t);
