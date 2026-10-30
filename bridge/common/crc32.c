#include "crc32.h"

uint32_t abn_crc32_begin(void)
{
    return 0xffffffffu;
}

uint32_t abn_crc32_update(uint32_t crc, const void *data, size_t length)
{
    const uint8_t *bytes = (const uint8_t *)data;

    while (length-- != 0u) {
        crc ^= *bytes++;
        for (unsigned bit = 0; bit < 8; ++bit) {
            const uint32_t mask = (uint32_t)-(int32_t)(crc & 1u);
            crc = (crc >> 1) ^ (0xedb88320u & mask);
        }
    }
    return crc;
}

uint32_t abn_crc32_finish(uint32_t crc)
{
    return crc ^ 0xffffffffu;
}

uint32_t abn_crc32(const void *data, size_t length)
{
    return abn_crc32_finish(abn_crc32_update(abn_crc32_begin(), data, length));
}
