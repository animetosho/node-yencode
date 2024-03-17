// compile with: c++ -O -DYENC_DISABLE_CRCUTIL=1 testcrcfuncs.c ../src/crc.cc ../src/crc_*.cc ../src/platform.cc -o testcrcfuncs

#include <stdint.h>
#include <stddef.h>
#include "../src/crc.h"
#include <assert.h>

int main(void) {
	crc_init();
	
	// test crc32_bytepow
	
	assert(crc32_bytepow(0) == 0);
	assert(crc32_bytepow(1) == 8);
	assert(crc32_bytepow(2) == 16);
	assert(crc32_bytepow((1<<29)-1) == 0xfffffff8);
	assert(crc32_bytepow(1<<29) == 1);
	assert(crc32_bytepow((1<<29)+1) == 9);
	
	uint32_t actual;
	// just do an exhaustive search, since it's not that slow
	for(uint64_t i=1<<29; i<((1ULL<<32) + 256); i++) {
		uint32_t ref = (i*8) % 0xffffffff;
		actual = crc32_bytepow(i);
		if(ref == 0)
			assert(actual == 0 || actual == 0xffffffff);
		else
			assert(ref == actual);
	}
	
	assert(crc32_bytepow(1ULL<<60) == 0x80000000);
	assert(crc32_bytepow(1ULL<<61) == 1);
	assert(crc32_bytepow(1ULL<<62) == 2);
	assert(crc32_bytepow(1ULL<<63) == 4);
	actual = crc32_bytepow(~0ULL);
	assert(actual == 0 || actual == 0xffffffff);
	assert(crc32_bytepow(~0ULL -1) == 0xfffffff7);
	assert(crc32_bytepow((1ULL<<63) -1) == 0xfffffffb);
	assert(crc32_bytepow((1ULL<<63) +1) == 12);
	
	
	// test crc32_mul2pow
	assert(crc32_mul2pow(0, 0) == 0);
	assert(crc32_mul2pow(0, 1) == 1);
	assert(crc32_mul2pow(1, 0) == 0);
	assert(crc32_mul2pow(1, 0x80000000) == 0x40000000);
	assert(crc32_mul2pow(1, 0x40000000) == 0x20000000);
	assert(crc32_mul2pow(4, 0x80000000) == 0x08000000);
	assert(crc32_mul2pow(5, 0x80000000) == 0x04000000);
	assert(crc32_mul2pow(0xffffffff, 123) == 123);
	
	return 0;
}
