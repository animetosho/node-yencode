#ifndef __YENC_CRC_H
#define __YENC_CRC_H

#ifdef __cplusplus
extern "C" {
#endif



typedef uint32_t (*crc_func)(const void*, size_t, uint32_t);
extern crc_func _do_crc32_incremental;
extern int _crc32_isa;
#define do_crc32 (*_do_crc32_incremental)

uint32_t do_crc32_combine(uint32_t crc1, const uint32_t crc2, size_t len2);
uint32_t do_crc32_zeros(uint32_t crc1, size_t len);
void crc_init();
static inline int crc32_isa_level() {
	return _crc32_isa;
}



#ifdef __cplusplus
}
#endif
#endif
