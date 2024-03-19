#include "common.h"
#include <stddef.h> // for size_t
#include "crc.h"

extern const uint32_t crc_power[32];

#ifdef __GNUC__
# define ctz32 __builtin_ctz
#elif defined(_MSC_VER)
static HEDLEY_ALWAYS_INLINE unsigned ctz32(uint32_t n) {
	unsigned long r;
	_BitScanForward(&r, n);
	return r;
}
#endif
