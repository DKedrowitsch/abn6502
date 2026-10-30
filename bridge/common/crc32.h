#ifndef ABN_CRC32_H
#define ABN_CRC32_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

uint32_t abn_crc32_begin(void);
uint32_t abn_crc32_update(uint32_t crc, const void *data, size_t length);
uint32_t abn_crc32_finish(uint32_t crc);
uint32_t abn_crc32(const void *data, size_t length);

#ifdef __cplusplus
}
#endif

#endif
