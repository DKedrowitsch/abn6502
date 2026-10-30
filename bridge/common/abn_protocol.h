#ifndef ABN_PROTOCOL_H
#define ABN_PROTOCOL_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ABN_USB_VERSION 1u
#define ABN_USB_COMMAND_UPLOAD 1u
#define ABN_USB_COMMAND_INFO 2u
#define ABN_USB_REQUEST_SIZE 24u
#define ABN_USB_DATA_SIZE 48u
#define ABN_USB_RESPONSE_SIZE 20u
#define ABN_USB_INFO_SIZE 40u
#define ABN_SAFE_IMAGE_SIZE 0x3700u
#define ABN_MAX_IMAGE_SIZE 0xffffu
#define ABN_RF_PACKET_SIZE 32u
#define ABN_RF_DATA_SIZE 30u

#define ABN_CAP_STREAMING 0x00000001u
#define ABN_CAP_RF_RESTART 0x00000002u
#define ABN_CAP_IMAGE_CRC32 0x00000004u
#define ABN_CAP_INFO 0x00000008u
#define ABN_CAP_RADIO_IRQ 0x00000010u
#define ABN_CAP_AUTORUN 0x00000020u

#define ABN_STATUS_RADIO_READY 0x00000001u
#define ABN_STATUS_TRANSFER_ACTIVE 0x00000002u
#define ABN_STATUS_USB_CONFIGURED 0x00000004u
#define ABN_STATUS_RADIO_IRQ_ACTIVE 0x00000008u

#define ABN_UPLOAD_FLAG_AUTORUN 0x0001u
#define ABN_RF_FLAG_AUTORUN 0x01u

enum abn_response_type {
    ABN_RESPONSE_READY = 1,
    ABN_RESPONSE_IMAGE_OK = 2,
    ABN_RESPONSE_RF_START = 3,
    ABN_RESPONSE_RF_PROGRESS = 4,
    ABN_RESPONSE_RF_RESTART = 5,
    ABN_RESPONSE_DONE = 6,
    ABN_RESPONSE_ERROR = 0x7f
};

enum abn_error_code {
    ABN_ERROR_NONE = 0,
    ABN_ERROR_VERSION = 1,
    ABN_ERROR_COMMAND = 2,
    ABN_ERROR_FLAGS = 3,
    ABN_ERROR_LENGTH = 4,
    ABN_ERROR_HEADER_CRC = 5,
    ABN_ERROR_RX_TIMEOUT = 6,
    ABN_ERROR_IMAGE_CRC = 7,
    ABN_ERROR_BUSY = 8,
    ABN_ERROR_RF_MAX_RT = 9,
    ABN_ERROR_RF_TIMEOUT = 10,
    ABN_ERROR_USB_DISCONNECT = 11,
    ABN_ERROR_INTERNAL = 12
};

struct abn_request {
    uint8_t command;
    uint16_t flags;
    uint32_t request_id;
    uint16_t image_length;
    uint32_t image_crc32;
};

struct abn_info {
    uint8_t firmware_major;
    uint8_t firmware_minor;
    uint8_t firmware_patch;
    uint32_t request_id;
    uint32_t capabilities;
    uint32_t status;
    uint32_t max_image_size;
    uint32_t safe_image_size;
    uint32_t uptime_ms;
    uint16_t last_error;
    uint8_t restart_count;
};

struct abn_response {
    uint8_t type;
    uint16_t code;
    uint32_t request_id;
    uint32_t value;
};

struct abn_data_chunk {
    uint32_t request_id;
    uint16_t sequence;
    uint8_t length;
    uint8_t data[ABN_RF_DATA_SIZE];
};

uint16_t abn_get_le16(const uint8_t *data);
uint32_t abn_get_le32(const uint8_t *data);
void abn_put_le16(uint8_t *data, uint16_t value);
void abn_put_le32(uint8_t *data, uint32_t value);

void abn_encode_request(uint8_t output[ABN_USB_REQUEST_SIZE],
                        const struct abn_request *request);
int abn_decode_request(const uint8_t input[ABN_USB_REQUEST_SIZE],
                       struct abn_request *request,
                       enum abn_error_code *error);
void abn_encode_data_chunk(uint8_t output[ABN_USB_DATA_SIZE],
                           const struct abn_data_chunk *chunk);
int abn_decode_data_chunk(const uint8_t input[ABN_USB_DATA_SIZE],
                          struct abn_data_chunk *chunk);
void abn_encode_response(uint8_t output[ABN_USB_RESPONSE_SIZE],
                         const struct abn_response *response);
int abn_decode_response(const uint8_t input[ABN_USB_RESPONSE_SIZE],
                        struct abn_response *response);
void abn_encode_info(uint8_t output[ABN_USB_INFO_SIZE],
                     const struct abn_info *info);
int abn_decode_info(const uint8_t input[ABN_USB_INFO_SIZE],
                    struct abn_info *info);

uint16_t abn_rf_packet_count(uint16_t image_length);
void abn_make_control_packet(uint8_t output[ABN_RF_PACKET_SIZE],
                             uint16_t image_length,
                             uint8_t flags);
size_t abn_make_data_packet(uint8_t output[ABN_RF_PACKET_SIZE],
                            const uint8_t *image,
                            uint16_t image_length,
                            uint16_t sequence);
int abn_make_chunk_packet(uint8_t output[ABN_RF_PACKET_SIZE],
                          const struct abn_data_chunk *chunk);

#ifdef __cplusplus
}
#endif

#endif
