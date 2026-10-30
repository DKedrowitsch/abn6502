#include "abn_protocol.h"
#include "crc32.h"

#include <array>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <vector>

#define CHECK(condition)                                                        \
    do {                                                                        \
        if (!(condition)) {                                                     \
            std::cerr << "check failed: " #condition << " at " << __FILE__     \
                      << ':' << __LINE__ << '\n';                               \
            std::exit(1);                                                       \
        }                                                                       \
    } while (0)

static void test_crc32()
{
    static constexpr char text[] = "123456789";
    CHECK(abn_crc32(text, sizeof(text) - 1) == 0xcbf43926u);
}

static void test_usb_codec()
{
    const abn_request expected{ABN_USB_COMMAND_UPLOAD, ABN_UPLOAD_FLAG_AUTORUN,
                               0x12345678u,
                               0x3700u, 0x89abcdefu};
    std::array<uint8_t, ABN_USB_REQUEST_SIZE> wire{};
    abn_encode_request(wire.data(), &expected);

    abn_request decoded{};
    abn_error_code error = ABN_ERROR_NONE;
    CHECK(abn_decode_request(wire.data(), &decoded, &error));
    CHECK(decoded.request_id == expected.request_id);
    CHECK(decoded.command == ABN_USB_COMMAND_UPLOAD);
    CHECK(decoded.flags == ABN_UPLOAD_FLAG_AUTORUN);
    CHECK(decoded.image_length == expected.image_length);
    CHECK(decoded.image_crc32 == expected.image_crc32);

    wire[12] ^= 1;
    CHECK(!abn_decode_request(wire.data(), &decoded, &error));
    CHECK(error == ABN_ERROR_HEADER_CRC);

    const abn_response response{ABN_RESPONSE_RF_PROGRESS, 0, expected.request_id, 256};
    std::array<uint8_t, ABN_USB_RESPONSE_SIZE> response_wire{};
    abn_encode_response(response_wire.data(), &response);
    abn_response decoded_response{};
    CHECK(abn_decode_response(response_wire.data(), &decoded_response));
    CHECK(decoded_response.type == ABN_RESPONSE_RF_PROGRESS);
    CHECK(decoded_response.request_id == expected.request_id);
    CHECK(decoded_response.value == 256);

    abn_data_chunk chunk{};
    chunk.request_id = expected.request_id;
    chunk.sequence = 256;
    chunk.length = 30;
    for (size_t index = 0; index < chunk.length; ++index) chunk.data[index] = index;
    std::array<uint8_t, ABN_USB_DATA_SIZE> data_wire{};
    chunk.length = 29;
    abn_encode_data_chunk(data_wire.data(), &chunk);
    abn_data_chunk decoded_chunk{};
    CHECK(abn_decode_data_chunk(data_wire.data(), &decoded_chunk));
    CHECK(decoded_chunk.request_id == chunk.request_id);
    CHECK(decoded_chunk.sequence == chunk.sequence);
    CHECK(decoded_chunk.length == chunk.length);
    CHECK(std::memcmp(decoded_chunk.data, chunk.data, chunk.length) == 0);
    data_wire[20] ^= 1;
    CHECK(!abn_decode_data_chunk(data_wire.data(), &decoded_chunk));
    abn_encode_data_chunk(data_wire.data(), &chunk);
    data_wire[43] = 1;
    abn_put_le32(data_wire.data() + 44, abn_crc32(data_wire.data(), 44));
    CHECK(!abn_decode_data_chunk(data_wire.data(), &decoded_chunk));
    std::array<uint8_t, ABN_RF_PACKET_SIZE> packet{};
    chunk.length = 0;
    CHECK(!abn_make_chunk_packet(packet.data(), &chunk));
    chunk.length = ABN_RF_DATA_SIZE + 1;
    CHECK(!abn_make_chunk_packet(packet.data(), &chunk));

    const abn_request info_request{ABN_USB_COMMAND_INFO, 0, 0x87654321u, 0, 0};
    abn_encode_request(wire.data(), &info_request);
    CHECK(abn_decode_request(wire.data(), &decoded, &error));
    CHECK(decoded.command == ABN_USB_COMMAND_INFO);
    CHECK(decoded.image_length == 0);

    const abn_info expected_info{
        1, 1, 0, info_request.request_id,
        ABN_CAP_STREAMING | ABN_CAP_RF_RESTART | ABN_CAP_IMAGE_CRC32
            | ABN_CAP_INFO | ABN_CAP_RADIO_IRQ | ABN_CAP_AUTORUN,
        ABN_STATUS_RADIO_READY | ABN_STATUS_USB_CONFIGURED,
        ABN_MAX_IMAGE_SIZE, ABN_SAFE_IMAGE_SIZE, 123456u, ABN_ERROR_RF_MAX_RT, 2
    };
    std::array<uint8_t, ABN_USB_INFO_SIZE> info_wire{};
    abn_encode_info(info_wire.data(), &expected_info);
    abn_info decoded_info{};
    CHECK(abn_decode_info(info_wire.data(), &decoded_info));
    CHECK(decoded_info.firmware_major == 1);
    CHECK(decoded_info.firmware_minor == 1);
    CHECK(decoded_info.request_id == info_request.request_id);
    CHECK(decoded_info.capabilities == expected_info.capabilities);
    CHECK(decoded_info.status == expected_info.status);
    CHECK(decoded_info.max_image_size == ABN_MAX_IMAGE_SIZE);
    CHECK(decoded_info.safe_image_size == ABN_SAFE_IMAGE_SIZE);
    CHECK(decoded_info.uptime_ms == 123456u);
    CHECK(decoded_info.last_error == ABN_ERROR_RF_MAX_RT);
    CHECK(decoded_info.restart_count == 2);
    info_wire[28] ^= 1;
    CHECK(!abn_decode_info(info_wire.data(), &decoded_info));
}

static void test_rf_packets()
{
    std::vector<uint8_t> image(7681);
    for (size_t index = 0; index < image.size(); ++index) image[index] = index & 0xff;

    std::array<uint8_t, ABN_RF_PACKET_SIZE> packet{};
    abn_make_control_packet(packet.data(), static_cast<uint16_t>(image.size()),
                            ABN_RF_FLAG_AUTORUN);
    CHECK(packet[0] == 0 && packet[1] == 0x31);
    CHECK(abn_get_le16(packet.data() + 2) == image.size());
    CHECK(packet[4] == ABN_RF_FLAG_AUTORUN);

    CHECK(abn_rf_packet_count(7650) == 255);
    CHECK(abn_rf_packet_count(7651) == 256);
    CHECK(abn_rf_packet_count(7681) == 257);
    CHECK(abn_rf_packet_count(0x3700) == 470);

    CHECK(abn_make_data_packet(packet.data(), image.data(), image.size(), 255) == 30);
    CHECK(packet[0] == 0xff && packet[1] == 0x00);
    CHECK(abn_make_data_packet(packet.data(), image.data(), image.size(), 256) == 30);
    CHECK(packet[0] == 0x00 && packet[1] == 0x01);
    CHECK(abn_make_data_packet(packet.data(), image.data(), image.size(), 257) == 1);
    CHECK(packet[0] == 0x01 && packet[1] == 0x01);
    for (size_t index = 3; index < packet.size(); ++index) CHECK(packet[index] == 0);
}

static void test_requested_lengths()
{
    for (uint16_t length : {1, 29, 30, 31, 255, 256, 257, 511, 512, 513,
                            767, 768, 769, 7650, 7651, 7679, 7680, 7681,
                            0x36ff, 0x3700, 0xffff}) {
        std::vector<uint8_t> image(length, 0xa5);
        std::vector<uint8_t> rebuilt;
        std::array<uint8_t, ABN_RF_PACKET_SIZE> packet{};
        const uint16_t count = abn_rf_packet_count(length);
        for (uint16_t sequence = 1; sequence <= count; ++sequence) {
            const size_t used = abn_make_data_packet(packet.data(), image.data(), length, sequence);
            rebuilt.insert(rebuilt.end(), packet.begin() + 2, packet.begin() + 2 + used);
        }
        CHECK(rebuilt == image);
    }
}

int main()
{
    test_crc32();
    test_usb_codec();
    test_rf_packets();
    test_requested_lengths();
    std::cout << "bridge protocol tests passed\n";
}
