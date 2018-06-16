
extern void (*_do_crc32)(const void* data, size_t length, unsigned char out[4]);
#define do_crc32 (*_do_crc32)
extern void (*_do_crc32_incremental)(const void* data, size_t length, unsigned char init[4]);
#define do_crc32_incremental (*_do_crc32_incremental)

void do_crc32_combine(unsigned char crc1[4], const unsigned char crc2[4], size_t len2);
void do_crc32_zeros(unsigned char crc1[4], size_t len);
void crc_init();
