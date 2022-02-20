
extern uint32_t (*_do_crc32_incremental)(const void* data, size_t length, uint32_t init);
#define do_crc32 (*_do_crc32_incremental)

uint32_t do_crc32_combine(uint32_t crc1, const uint32_t crc2, size_t len2);
uint32_t do_crc32_zeros(uint32_t crc1, size_t len);
void crc_init();
