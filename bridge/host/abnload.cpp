#include "abn_protocol.h"
#include "crc32.h"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include <fcntl.h>
#include <poll.h>
#include <termios.h>
#include <unistd.h>

namespace {

struct Options {
    std::string device = "/dev/ttyACM0";
    std::string filename;
    bool info = false;
    bool verbose = false;
    bool patched_rom = false;
    bool autorun = false;
    bool allow_unsafe_size = false;
};

[[noreturn]] void fail(const std::string &message)
{
    throw std::runtime_error(message);
}

Options parse_options(int argc, char **argv)
{
    Options options;
    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        if (argument == "--device") {
            if (++index == argc) fail("--device requires a path");
            options.device = argv[index];
        } else if (argument == "--verbose" || argument == "-v") {
            options.verbose = true;
        } else if (argument == "--allow-unsafe-size") {
            options.allow_unsafe_size = true;
            options.patched_rom = true;
        } else if (argument == "--patched-rom") {
            options.patched_rom = true;
        } else if (argument == "--run") {
            options.autorun = true;
            options.patched_rom = true;
        } else if (argument == "--info") {
            options.info = true;
        } else if (argument == "--help" || argument == "-h") {
            std::cout << "Usage: abnload [--device /dev/ttyACM0] [--verbose] "
                         "[--patched-rom] [--allow-unsafe-size] [--run] program.bin\n"
                         "       abnload [--device /dev/ttyACM0] --info\n";
            std::exit(0);
        } else if (!argument.empty() && argument[0] == '-') {
            fail("unknown option: " + argument);
        } else if (options.filename.empty()) {
            options.filename = argument;
        } else {
            fail("only one image may be specified");
        }
    }
    if (options.info && !options.filename.empty()) fail("--info does not accept an image");
    if (options.info && options.autorun) fail("--run is valid only with an image");
    if (!options.info && options.filename.empty()) fail("no image specified");
    return options;
}

std::vector<uint8_t> read_image(const std::string &path, const Options &options)
{
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) fail("cannot open image: " + path);
    const auto size = input.tellg();
    const auto limit = options.allow_unsafe_size ? ABN_MAX_IMAGE_SIZE
        : (options.patched_rom ? ABN_SAFE_IMAGE_SIZE : 7650u);
    if (size <= 0 || size > static_cast<std::streamoff>(limit)) {
        fail(options.allow_unsafe_size
                 ? "image size must be between 1 and 65535 bytes"
                 : (options.patched_rom
                        ? "image exceeds the safe 14080-byte ABN userland region; use "
                          "--allow-unsafe-size only with a deliberate memory map"
                        : "image exceeds the original ROM's 7650-byte packet-255 limit; "
                          "flash the bug-fixed ROM and pass --patched-rom"));
    }
    if (!options.patched_rom && (size & 0xff) == 0) {
        fail("the original ROM miscounts image lengths divisible by 256; flash the "
             "bug-fixed ROM and pass --patched-rom");
    }
    std::vector<uint8_t> image(static_cast<size_t>(size));
    input.seekg(0);
    input.read(reinterpret_cast<char *>(image.data()), size);
    if (!input) fail("cannot read complete image");
    return image;
}

class SerialPort {
public:
    using Clock = std::chrono::steady_clock;
    using Deadline = Clock::time_point;

    explicit SerialPort(const std::string &path)
    {
        const int descriptor = open(path.c_str(), O_RDWR | O_NOCTTY | O_CLOEXEC | O_NONBLOCK);
        if (descriptor < 0) fail("cannot open " + path + ": " + std::strerror(errno));

        termios settings{};
        if (tcgetattr(descriptor, &settings) != 0) {
            const std::string message = "tcgetattr failed: " + std::string(std::strerror(errno));
            close(descriptor);
            fail(message);
        }
        cfmakeraw(&settings);
        settings.c_cflag |= CLOCAL | CREAD;
        settings.c_cflag &= ~(CRTSCTS | CSTOPB | PARENB | CSIZE);
        settings.c_cflag |= CS8;
        if (cfsetispeed(&settings, B115200) != 0 || cfsetospeed(&settings, B115200) != 0
            || tcsetattr(descriptor, TCSANOW, &settings) != 0
            || tcflush(descriptor, TCIOFLUSH) != 0) {
            const std::string message = "serial setup failed: " + std::string(std::strerror(errno));
            close(descriptor);
            fail(message);
        }
        fd_ = descriptor;
    }

    ~SerialPort()
    {
        if (fd_ >= 0) close(fd_);
    }

    static Deadline deadline_after(int timeout_ms)
    {
        return Clock::now() + std::chrono::milliseconds(timeout_ms);
    }

    void write_all(const void *data, size_t length, int timeout_ms = 5000)
    {
        const auto *bytes = static_cast<const uint8_t *>(data);
        const Deadline deadline = deadline_after(timeout_ms);
        while (length != 0) {
            const ssize_t result = write(fd_, bytes, length);
            if (result < 0 && errno == EINTR) continue;
            if (result < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                wait(POLLOUT, deadline);
                continue;
            }
            if (result <= 0) fail("USB write failed: " + std::string(std::strerror(errno)));
            bytes += result;
            length -= static_cast<size_t>(result);
        }
    }

    uint8_t read_byte(Deadline deadline)
    {
        for (;;) {
            wait(POLLIN, deadline);
            uint8_t value;
            const ssize_t result = read(fd_, &value, 1);
            if (result == 1) return value;
            if (result < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)) {
                continue;
            }
            fail("USB read failed: " + std::string(std::strerror(errno)));
        }
    }

private:
    void wait(short events, Deadline deadline)
    {
        for (;;) {
            const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
                deadline - Clock::now());
            if (remaining.count() <= 0) fail("timeout communicating with bridge");
            pollfd descriptor{fd_, events, 0};
            const int timeout = static_cast<int>(std::min<int64_t>(remaining.count(), INT32_MAX));
            const int result = poll(&descriptor, 1, timeout);
            if (result < 0 && errno == EINTR) continue;
            if (result == 0) fail("timeout communicating with bridge");
            if (result < 0) fail("poll failed: " + std::string(std::strerror(errno)));
            if ((descriptor.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
                fail("USB device disconnected");
            }
            if ((descriptor.revents & events) != 0) return;
        }
    }

    int fd_ = -1;
};

abn_response read_response(SerialPort &port, uint32_t request_id, int timeout_ms)
{
    static constexpr uint8_t magic[4] = {'A', 'B', 'N', 'S'};
    uint8_t wire[ABN_USB_RESPONSE_SIZE];
    size_t matched = 0;
    const auto deadline = SerialPort::deadline_after(timeout_ms);

    while (matched != sizeof(magic)) {
        const uint8_t value = port.read_byte(deadline);
        matched = value == magic[matched] ? matched + 1 : (value == magic[0] ? 1 : 0);
    }
    std::memcpy(wire, magic, sizeof(magic));
    for (size_t index = sizeof(magic); index < sizeof(wire); ++index) {
        wire[index] = port.read_byte(deadline);
    }

    abn_response response{};
    if (!abn_decode_response(wire, &response)) fail("invalid bridge response CRC or version");
    if (response.request_id != request_id) fail("stale bridge response request ID");
    return response;
}

abn_info read_info(SerialPort &port, uint32_t request_id, int timeout_ms)
{
    static constexpr uint8_t magic[4] = {'A', 'B', 'N', 'I'};
    uint8_t wire[ABN_USB_INFO_SIZE];
    size_t matched = 0;
    const auto deadline = SerialPort::deadline_after(timeout_ms);

    while (matched != sizeof(magic)) {
        const uint8_t value = port.read_byte(deadline);
        matched = value == magic[matched] ? matched + 1 : (value == magic[0] ? 1 : 0);
    }
    std::memcpy(wire, magic, sizeof(magic));
    for (size_t index = sizeof(magic); index < sizeof(wire); ++index) {
        wire[index] = port.read_byte(deadline);
    }

    abn_info info{};
    if (!abn_decode_info(wire, &info)) fail("invalid bridge information CRC or version");
    if (info.request_id != request_id) fail("stale bridge information request ID");
    return info;
}

const char *error_name(uint16_t code)
{
    switch (code) {
    case ABN_ERROR_NONE: return "none";
    case ABN_ERROR_VERSION: return "unsupported protocol version";
    case ABN_ERROR_COMMAND: return "unsupported command";
    case ABN_ERROR_FLAGS: return "invalid flags";
    case ABN_ERROR_LENGTH: return "invalid image length";
    case ABN_ERROR_HEADER_CRC: return "header CRC mismatch";
    case ABN_ERROR_RX_TIMEOUT: return "image receive timeout";
    case ABN_ERROR_IMAGE_CRC: return "image CRC mismatch";
    case ABN_ERROR_BUSY: return "bridge busy";
    case ABN_ERROR_RF_MAX_RT: return "radio retry limit reached";
    case ABN_ERROR_RF_TIMEOUT: return "radio timeout";
    case ABN_ERROR_USB_DISCONNECT: return "USB disconnected";
    default: return "internal bridge error";
    }
}

void print_info(const abn_info &info)
{
    std::cout << "Firmware: " << static_cast<unsigned>(info.firmware_major) << '.'
              << static_cast<unsigned>(info.firmware_minor) << '.'
              << static_cast<unsigned>(info.firmware_patch) << '\n'
              << "USB protocol: " << ABN_USB_VERSION << '\n'
              << "Radio ready: "
              << ((info.status & ABN_STATUS_RADIO_READY) != 0 ? "yes" : "no") << '\n'
              << "Radio IRQ active: "
              << ((info.status & ABN_STATUS_RADIO_IRQ_ACTIVE) != 0 ? "yes" : "no") << '\n'
              << "Transfer active: "
              << ((info.status & ABN_STATUS_TRANSFER_ACTIVE) != 0 ? "yes" : "no") << '\n'
              << "Maximum image: " << info.max_image_size << " bytes\n"
              << "Safe image: " << info.safe_image_size << " bytes\n"
              << "Uptime: " << info.uptime_ms << " ms\n"
              << "Last error: " << error_name(info.last_error)
              << " (" << info.last_error << ")\n"
              << "Last restart count: " << static_cast<unsigned>(info.restart_count) << '\n'
              << "Capabilities:";
    if ((info.capabilities & ABN_CAP_STREAMING) != 0) std::cout << " streaming";
    if ((info.capabilities & ABN_CAP_RF_RESTART) != 0) std::cout << " rf-restart";
    if ((info.capabilities & ABN_CAP_IMAGE_CRC32) != 0) std::cout << " image-crc32";
    if ((info.capabilities & ABN_CAP_INFO) != 0) std::cout << " info";
    if ((info.capabilities & ABN_CAP_RADIO_IRQ) != 0) std::cout << " radio-irq";
    if ((info.capabilities & ABN_CAP_AUTORUN) != 0) std::cout << " autorun";
    std::cout << '\n';
}

void check_response_ok(const abn_response &response)
{
    if (response.type == ABN_RESPONSE_ERROR) {
        fail(std::string(error_name(response.code)) + " (value "
             + std::to_string(response.value) + ")");
    }
    if (response.code != ABN_ERROR_NONE) fail("nonzero status on successful response");
}

} // namespace

int main(int argc, char **argv)
{
    try {
        const Options options = parse_options(argc, argv);
        uint32_t request_id = static_cast<uint32_t>(
            std::chrono::steady_clock::now().time_since_epoch().count()) ^
            static_cast<uint32_t>(getpid());
        if (request_id == 0) request_id = 1;

        SerialPort port(options.device);
        if (options.info) {
            const abn_request info_request{ABN_USB_COMMAND_INFO, 0, request_id, 0, 0};
            uint8_t request_wire[ABN_USB_REQUEST_SIZE];
            abn_encode_request(request_wire, &info_request);
            port.write_all(request_wire, sizeof(request_wire));
            print_info(read_info(port, request_id, 2000));
            return 0;
        }

        const std::vector<uint8_t> image = read_image(options.filename, options);
        const uint32_t image_crc = abn_crc32(image.data(), image.size());

        const uint16_t upload_flags = options.autorun ? ABN_UPLOAD_FLAG_AUTORUN : 0;
        abn_request request{ABN_USB_COMMAND_UPLOAD, upload_flags, request_id,
                            static_cast<uint16_t>(image.size()), image_crc};
        uint8_t request_wire[ABN_USB_REQUEST_SIZE];
        abn_encode_request(request_wire, &request);

        port.write_all(request_wire, sizeof(request_wire));
        abn_response response = read_response(port, request_id, 2000);
        check_response_ok(response);
        if (response.type != ABN_RESPONSE_READY) fail("bridge did not accept upload header");
        if (response.value < image.size()) fail("bridge reported an insufficient size limit");
        response = read_response(port, request_id, 15000);
        check_response_ok(response);
        if (response.type != ABN_RESPONSE_RF_START) fail("bridge did not start RF transfer");
        const uint16_t packet_count = abn_rf_packet_count(request.image_length);
        if (response.value != packet_count) fail("bridge reported an incorrect packet count");
        std::cout << "Transmitting " << response.value << " RF packets\n";

        uint16_t sequence = 1;
        size_t offset = 0;
        while (offset < image.size()) {
            const size_t length = std::min<size_t>(ABN_RF_DATA_SIZE, image.size() - offset);
            abn_data_chunk chunk{};
            chunk.request_id = request_id;
            chunk.sequence = sequence;
            chunk.length = static_cast<uint8_t>(length);
            std::memcpy(chunk.data, image.data() + offset, length);
            uint8_t data_wire[ABN_USB_DATA_SIZE];
            abn_encode_data_chunk(data_wire, &chunk);
            port.write_all(data_wire, sizeof(data_wire));

            response = read_response(port, request_id, 5000);
            check_response_ok(response);
            switch (response.type) {
            case ABN_RESPONSE_RF_PROGRESS:
                if (options.verbose) std::cout << "Acknowledged packet " << response.value << "\n";
                if (response.value != sequence) fail("bridge acknowledged unexpected sequence");
                offset += length;
                ++sequence;
                break;
            case ABN_RESPONSE_RF_RESTART:
                if ((response.value & 0xffffu) != sequence
                    || (response.value >> 16) == 0 || (response.value >> 16) > 3) {
                    fail("invalid RF restart response");
                }
                std::cout << "RF restart " << (response.value >> 16)
                          << " after packet " << (response.value & 0xffffu) << "\n";
                offset = 0;
                sequence = 1;
                break;
            case ABN_RESPONSE_DONE:
                fail("bridge reported completion before final progress acknowledgement");
            default:
                fail("unexpected bridge response type");
            }
        }
        response = read_response(port, request_id, 5000);
        check_response_ok(response);
        if (response.type == ABN_RESPONSE_DONE) {
            if (response.value != packet_count) fail("bridge completion packet count mismatch");
            std::cout << "Upload complete: " << image.size() << " bytes, "
                      << response.value << " RF packets\n";
            return 0;
        }
        fail("bridge did not report completion");
    } catch (const std::exception &error) {
        std::cerr << "abnload: " << error.what() << '\n';
        return 1;
    }
}
