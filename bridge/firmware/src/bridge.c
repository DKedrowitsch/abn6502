#include "bridge.h"

#include "abn_protocol.h"
#include "crc32.h"
#include "firmware_version.h"
#include "nrf24.h"
#include "platform.h"
#include "usb_bridge.h"

#include <string.h>

enum bridge_state {
    BRIDGE_WAIT_HEADER,
    BRIDGE_WAIT_CHUNK
};

static enum bridge_state state;
static uint8_t frame[ABN_USB_DATA_SIZE];
static uint8_t frame_position;
static uint32_t last_byte_ms;
static struct abn_request request;
static uint16_t expected_sequence;
static uint32_t image_offset;
static uint32_t running_crc;
static uint8_t restart_count;
static int radio_ready;
static enum abn_error_code last_error;

static const uint8_t header_magic[4] = {'A', 'B', 'N', 'U'};
static const uint8_t chunk_magic[4] = {'A', 'B', 'N', 'D'};

static void reset_transaction(void)
{
    state = BRIDGE_WAIT_HEADER;
    frame_position = 0;
    expected_sequence = 1;
    image_offset = 0;
    running_crc = abn_crc32_begin();
    restart_count = 0;
    platform_led_set(0);
}

static int send_response(uint8_t type, uint16_t code, uint32_t value)
{
    uint8_t output[ABN_USB_RESPONSE_SIZE];
    const struct abn_response response = {type, code, request.request_id, value};
    abn_encode_response(output, &response);
    return usb_bridge_send(output, sizeof(output), 1000);
}

static void send_error(enum abn_error_code code, uint32_t value)
{
    last_error = code;
    send_response(ABN_RESPONSE_ERROR, (uint16_t)code, value);
    reset_transaction();
}

static void send_info(void)
{
    uint8_t output[ABN_USB_INFO_SIZE];
    struct abn_info info = {0};

    info.firmware_major = ABN_FIRMWARE_VERSION_MAJOR;
    info.firmware_minor = ABN_FIRMWARE_VERSION_MINOR;
    info.firmware_patch = ABN_FIRMWARE_VERSION_PATCH;
    info.request_id = request.request_id;
    info.capabilities = ABN_CAP_STREAMING | ABN_CAP_RF_RESTART
        | ABN_CAP_IMAGE_CRC32 | ABN_CAP_INFO | ABN_CAP_RADIO_IRQ
        | ABN_CAP_AUTORUN;
    info.status = ABN_STATUS_USB_CONFIGURED;
    if (radio_ready) info.status |= ABN_STATUS_RADIO_READY;
    if (nrf24_irq_active()) info.status |= ABN_STATUS_RADIO_IRQ_ACTIVE;
    if (state == BRIDGE_WAIT_CHUNK) info.status |= ABN_STATUS_TRANSFER_ACTIVE;
    info.max_image_size = ABN_MAX_IMAGE_SIZE;
    info.safe_image_size = ABN_SAFE_IMAGE_SIZE;
    info.uptime_ms = platform_millis();
    info.last_error = (uint16_t)last_error;
    info.restart_count = restart_count;
    abn_encode_info(output, &info);
    usb_bridge_send(output, sizeof(output), 1000);
}

static int establish_control(void)
{
    uint8_t packet[ABN_RF_PACKET_SIZE];
    abn_make_control_packet(packet, request.image_length,
                            (uint8_t)request.flags);
    for (unsigned attempt = 0; attempt < 10; ++attempt) {
        if (nrf24_send(packet, 100) == NRF24_TX_OK) {
            platform_delay_ms(40);
            return 1;
        }
        platform_delay_ms(100);
    }
    return 0;
}

static void start_request(const uint8_t input[ABN_USB_REQUEST_SIZE])
{
    enum abn_error_code error;

    request.request_id = abn_get_le32(input + 8);
    if (!abn_decode_request(input, &request, &error)) {
        send_error(error, 0);
        return;
    }
    if (request.command == ABN_USB_COMMAND_INFO) {
        send_info();
        reset_transaction();
        return;
    }
    last_error = ABN_ERROR_NONE;
    if (!radio_ready) {
        radio_ready = nrf24_init();
        if (!radio_ready) {
            send_error(ABN_ERROR_INTERNAL, 1);
            return;
        }
    }
    if (!send_response(ABN_RESPONSE_READY, ABN_ERROR_NONE, ABN_MAX_IMAGE_SIZE)) {
        reset_transaction();
        return;
    }
    if (!establish_control()) {
        send_error(ABN_ERROR_RF_MAX_RT, 0);
        return;
    }

    expected_sequence = 1;
    image_offset = 0;
    running_crc = abn_crc32_begin();
    restart_count = 0;
    state = BRIDGE_WAIT_CHUNK;
    frame_position = 0;
    last_byte_ms = platform_millis();
    platform_led_set(1);
    send_response(ABN_RESPONSE_RF_START, ABN_ERROR_NONE,
                  abn_rf_packet_count(request.image_length));
}

static void restart_transfer(uint16_t failed_sequence)
{
    if (restart_count >= 3) {
        send_error(ABN_ERROR_RF_MAX_RT, failed_sequence);
        return;
    }
    ++restart_count;
    nrf24_recover();
    platform_delay_ms(100);
    if (!establish_control()) {
        send_error(ABN_ERROR_RF_MAX_RT, 0);
        return;
    }
    expected_sequence = 1;
    image_offset = 0;
    running_crc = abn_crc32_begin();
    last_byte_ms = platform_millis();
    send_response(ABN_RESPONSE_RF_RESTART, ABN_ERROR_NONE,
                  ((uint32_t)restart_count << 16) | failed_sequence);
}

static void process_chunk(const uint8_t input[ABN_USB_DATA_SIZE])
{
    struct abn_data_chunk chunk;
    uint8_t packet[ABN_RF_PACKET_SIZE];
    const uint32_t remaining = request.image_length - image_offset;
    const uint8_t expected_length = remaining > ABN_RF_DATA_SIZE
        ? ABN_RF_DATA_SIZE : (uint8_t)remaining;
    enum nrf24_result result;

    if (!abn_decode_data_chunk(input, &chunk)) {
        send_error(ABN_ERROR_HEADER_CRC, expected_sequence);
        return;
    }
    if (chunk.request_id != request.request_id || chunk.sequence != expected_sequence
        || chunk.length != expected_length) {
        send_error(ABN_ERROR_COMMAND, expected_sequence);
        return;
    }

    if (!abn_make_chunk_packet(packet, &chunk)) {
        send_error(ABN_ERROR_INTERNAL, expected_sequence);
        return;
    }
    result = nrf24_send(packet, 100);
    if (result != NRF24_TX_OK) {
        restart_transfer(chunk.sequence);
        return;
    }

    platform_delay_ms(40);
    running_crc = abn_crc32_update(running_crc, chunk.data, chunk.length);
    image_offset += chunk.length;
    ++expected_sequence;
    send_response(ABN_RESPONSE_RF_PROGRESS, ABN_ERROR_NONE, chunk.sequence);

    if (image_offset == request.image_length) {
        if (abn_crc32_finish(running_crc) != request.image_crc32) {
            send_error(ABN_ERROR_IMAGE_CRC, 0);
            return;
        }
        send_response(ABN_RESPONSE_DONE, ABN_ERROR_NONE,
                      abn_rf_packet_count(request.image_length));
        reset_transaction();
    }
}

static void collect_byte(uint8_t value)
{
    const uint8_t *magic = state == BRIDGE_WAIT_HEADER ? header_magic : chunk_magic;
    const uint8_t size = state == BRIDGE_WAIT_HEADER
        ? ABN_USB_REQUEST_SIZE : ABN_USB_DATA_SIZE;

    last_byte_ms = platform_millis();
    if (frame_position < 4) {
        if (value == magic[frame_position]) {
            frame[frame_position++] = value;
        } else {
            frame_position = value == magic[0] ? 1 : 0;
            if (frame_position == 1) frame[0] = value;
        }
        return;
    }
    frame[frame_position++] = value;
    if (frame_position == size) {
        frame_position = 0;
        if (state == BRIDGE_WAIT_HEADER) {
            start_request(frame);
        } else {
            process_chunk(frame);
        }
    }
}

void bridge_init(void)
{
    reset_transaction();
    radio_ready = nrf24_init();
}

void bridge_poll(void)
{
    uint8_t value;

    if (!usb_bridge_configured()) {
        if (state != BRIDGE_WAIT_HEADER) reset_transaction();
        return;
    }
    if (usb_bridge_rx_overflowed()) {
        send_error(ABN_ERROR_INTERNAL, 2);
        return;
    }
    while (usb_bridge_read_byte(&value)) {
        collect_byte(value);
    }
    if (state == BRIDGE_WAIT_CHUNK) {
        if ((uint32_t)(platform_millis() - last_byte_ms) >= 2000u) {
            send_error(ABN_ERROR_RX_TIMEOUT, expected_sequence);
        }
    } else if (frame_position != 0) {
        const uint32_t timeout = 1000u;
        if ((uint32_t)(platform_millis() - last_byte_ms) >= timeout) {
            frame_position = 0;
        }
    }
}
