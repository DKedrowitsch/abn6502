# CH32V203 USB-to-nRF24 ABN6502 Bridge

This directory contains firmware for a WeAct CH32V203C8T6 BluePill+ board and a native Linux uploader. The bridge accepts an image through USB CDC ACM, constructs the existing ABN6502 radio packets, and transmits them through an nRF24L01+.

The ABN6502 ROM receiver and RF-side workflow are documented in
[`../WIRELESS_LOADER.md`](../WIRELESS_LOADER.md).

The original ABN6502 ca65/ld65 architecture is unchanged. The radio format is
backward-compatible with the original implementation and now assigns control
byte 4 bit 0 to optional execution after loading.

The current bridge firmware version is **1.3.0**. It includes PB1
interrupt-driven nRF24 completion, the MCU internal PB1 pull-up, INFO reporting,
and backward-compatible wireless autorun.

## Architecture

```text
abnload program.bin
        |
        | USB CDC ACM, CRC-protected streaming frames
        v
WeAct CH32V203C8T6
  USB parser <-> transfer state machine <-> nRF24 SPI driver
                                           |
                                           | 32-byte Enhanced ShockBurst packets
                                           v
                                      ABN6502 ROM loader
```

The Linux program owns the complete source image. The CH32 does not buffer the whole image. Instead, one 30-byte image chunk is sent to the bridge, verified, transmitted over RF, and hardware-acknowledged before Linux sends the next chunk. This uses little RAM and permits images up to the existing RF protocol's 65,535-byte limit.

If RF delivery becomes uncertain, the bridge sends a new ABN control packet and asks Linux to stream the image again from sequence 1. This is necessary because an nRF24 hardware acknowledgement does not prove that the 6502 has consumed a packet.

## Important Size Warning

The current ABN control packet contains a 16-bit nonzero length. Its maximum is 65,535 bytes, not 65,536 bytes.

The ABN6502 linker defines the normal userland region as `$0300-$39ff`, which is `$3700` or 14,080 bytes. Larger transfers can cross video RAM, memory-mapped I/O, ROM-selected addresses, and eventually wrap around the 16-bit address space.

`$3700` is a linker extent, not a ROM-enforced bound or a guarantee that every
address is ordinary RAM. In particular, the configured `$0800` VRAM window lies
inside that numerical interval. Programs approaching the limit must account for
the actual hardware map.

The original ROM rejects RF sequence 256 and miscounts lengths divisible by
256. Therefore `abnload` defaults to at most 7,650 bytes and rejects
256-byte multiples in legacy mode. Pass `--patched-rom` after flashing the
bug-fixed ROM in this workspace to use the safe `$3700` userland limit.
`--run` and `--allow-unsafe-size` both imply patched-ROM mode. Without the ROM
fixes, a transfer can receive RF ACKs and report apparent success despite
missing data.

`--allow-unsafe-size` raises the host-side limit to 65,535 bytes for deliberate experiments with a known memory map. It does not make those addresses safe.

## Source Layout

```text
bridge/
  common/
    abn_protocol.c/.h    USB and ABN RF wire encoding
    crc32.c/.h           IEEE reflected CRC-32
  firmware/
    CMakeLists.txt
    fetch-sdk.sh
    cmake/               RISC-V CMake toolchain file
    inc/
      firmware_version.h Firmware semantic version reported by INFO
    src/
      main.c             startup and cooperative main loop
      bridge.c           USB/RF transfer state machine
      nrf24.c            SPI and Enhanced ShockBurst driver
      platform.c         clock-derived timing and board LED
      usb_bridge.c       CDC RX ring and response transport
      usb_endp.c         USB endpoint callbacks
      usb_hw.c            USB clock, pins and IRQ handlers
      usb_prop.c          CDC class/control handling
  host/
    abnload.cpp          native Linux uploader
  tests/
    test_protocol.cpp    wire, CRC and rollover regressions
  ../tests/
    test_wireless_loader.py  ABN ROM receiver/state-machine model
    wireless_test.s          Uploaded-code hardware smoke test
    build_wireless_test.sh   ca65/ld65 smoke-test build
```

## Hardware Wiring

| nRF24L01+ | CH32V203 pin | Function |
|---|---|---|
| VCC | 3V3 | 3.3 V only |
| GND | GND | Common ground |
| CSN | PA4 | GPIO output, idle high |
| SCK | PA5 | SPI1 SCK |
| MISO | PA6 | SPI1 MISO |
| MOSI | PA7 | SPI1 MOSI |
| CE | PB0 | GPIO output, idle low |
| IRQ | PB1 | Active-low EXTI input with MCU internal pull-up |

PA4-PA7 are also routed to the board's optional SPI-flash footprint. The flash must be unpopulated or otherwise isolated.

The USB-C connector is the normal bridge connection. It uses PA11 for USB D- and PA12 for USB D+. PA13 and PA14 remain available for the WCH-LinkE debug connection. PB2 is the board's active-high blue status LED. It flashes three times when the application reaches `main()`, remains off while idle, and is held on during an RF transaction.

Place a 100 nF ceramic and 10-47 uF bulk capacitor directly at the nRF24 module. A PA/LNA radio module should use a separate adequately rated 3.3 V regulator.

The internal PB1 pull-up keeps the input defined if the jumper is disconnected
or the radio is unpowered. The falling-edge ISR only records an event; all SPI
STATUS reads and IRQ clearing remain in normal firmware context. A 100 ms
timeout remains active so a missing IRQ jumper produces a bounded error rather
than hanging the bridge.

## Radio Configuration

| Setting | Value |
|---|---|
| Channel | 2, or 2.402 GHz |
| Air rate | 2 Mbps |
| TX power | 0 dBm (`RF_PWR=11`) |
| Address width | 5 bytes |
| TX and ACK pipe address | `E7:E7:E7:E7:E7` |
| RF CRC | Two bytes |
| Auto acknowledgement | Enabled on pipe 0 |
| Retry register | `SETUP_RETR=$ff` |
| Retry delay | 4 ms |
| Retry count | 15 |
| Dynamic payload feature | Enabled |
| Actual payload size | Always 32 bytes |
| Application pacing | 40 ms after every acknowledged packet |

SPI1 runs at 6 MHz with the firmware's default 96 MHz system clock, below the nRF24L01+ 10 MHz limit. If FEATURE cannot be enabled directly, the driver sends `ACTIVATE 73`, which is required by some older devices and clones.

### ABN RF Packets

Control packet:

| Offset | Size | Meaning |
|---:|---:|---|
| 0 | 1 | `$00` |
| 1 | 1 | `$31`, ASCII `1` |
| 2 | 2 | Image length, little-endian |
| 4 | 1 | Flags: bit 0 requests execution after complete reception |
| 5 | 27 | Zero |

Data packet:

| Offset | Size | Meaning |
|---:|---:|---|
| 0 | 2 | Sequence starting at 1, little-endian |
| 2 | 30 | Image data, final packet zero-padded |

There is no end packet. The ABN receiver stops after the advertised number of image bytes. Sequence 256 is encoded `00 01` and requires the bug-fixed ABN ROM included in this workspace. Control byte 4 is backward-compatible: zero retains the original F5/`run` behavior, while bit 0 makes the updated ROM jump to `$0300` only after the complete transfer.

## USB CDC Protocol

USB CDC is a byte stream. USB packet boundaries are never used as application boundaries. All multibyte values are little-endian. CRC fields use reflected IEEE CRC-32 with polynomial `0xedb88320`, initial value `$ffffffff`, and final XOR `$ffffffff`. The check value for `123456789` is `$cbf43926`.

Only one request is active at a time.

### Upload Request

The request is 24 bytes:

| Offset | Size | Field |
|---:|---:|---|
| 0 | 4 | Magic `ABNU` |
| 4 | 1 | Version, currently 1 |
| 5 | 1 | Command: 1 upload, 2 information query |
| 6 | 2 | Flags: bit 0 requests autorun; other bits zero |
| 8 | 4 | Request ID |
| 12 | 2 | Image length, 1-65,535 |
| 14 | 2 | Reserved, zero |
| 16 | 4 | CRC32 of the complete image |
| 20 | 4 | CRC32 of bytes 0-19 |

After validation, the bridge returns `READY`, establishes the ABN RF control packet, and returns `RF_START`.

An information query uses command 2 with zero image length and image CRC. It
does not start the radio transfer state machine.

### Streaming Data Frame

Each 48-byte frame carries one ABN RF data payload:

| Offset | Size | Field |
|---:|---:|---|
| 0 | 4 | Magic `ABND` |
| 4 | 1 | Version 1 |
| 5 | 1 | Reserved, zero |
| 6 | 2 | RF sequence number |
| 8 | 4 | Request ID |
| 12 | 1 | Data length, 1-30 |
| 13 | 1 | Reserved, zero |
| 14 | 30 | Data, unused bytes zero |
| 44 | 4 | CRC32 of bytes 0-43 |

Linux waits for `RF_PROGRESS` before sending the next frame. This lock-step behavior prevents CDC buffering from making RF restart ambiguous.

### Response

Every response is 20 bytes:

| Offset | Size | Field |
|---:|---:|---|
| 0 | 4 | Magic `ABNS` |
| 4 | 1 | Version 1 |
| 5 | 1 | Response type |
| 6 | 2 | Status or error code |
| 8 | 4 | Request ID |
| 12 | 4 | Type-specific value |
| 16 | 4 | CRC32 of bytes 0-15 |

| Type | Value | Meaning |
|---|---:|---|
| `READY` | Maximum bridge length | Header accepted |
| `RF_START` | Data packet count | Control packet hardware-acknowledged |
| `RF_PROGRESS` | Sequence | Data packet hardware-acknowledged |
| `RF_RESTART` | Restart and failed sequence | Host must replay from sequence 1 |
| `DONE` | Data packet count | Full stream CRC matched |
| `ERROR` | Diagnostic | Transaction failed |

`DONE` means all RF payloads produced `TX_DS` and the streamed bytes matched the request CRC. The current ABN protocol has no return channel proving that the 6502 stored or executed the complete image.

### Information Response

Command 2 returns one 40-byte information frame:

| Offset | Size | Field |
|---:|---:|---|
| 0 | 4 | Magic `ABNI` |
| 4 | 1 | USB protocol version |
| 5 | 3 | Firmware major, minor and patch |
| 8 | 4 | Request ID |
| 12 | 4 | Capability flags |
| 16 | 4 | Current status flags |
| 20 | 4 | Protocol maximum image size |
| 24 | 4 | Safe ABN userland image size |
| 28 | 4 | Uptime in milliseconds |
| 32 | 2 | Last error code |
| 34 | 1 | Restart count for the active transaction |
| 35 | 1 | Reserved, zero |
| 36 | 4 | CRC32 of bytes 0-35 |

Capability bits are: bit 0 streaming, bit 1 whole-transfer RF restart, bit 2
full-image CRC32, bit 3 information query, bit 4 PB1 radio IRQ, and bit 5
autorun. Status bits are: bit 0 radio initialized, bit 1 transfer active, bit 2
USB configured, and bit 3 active-low PB1 IRQ currently asserted. INFO requests
are accepted only while idle, so transfer activity is not currently observable
through `--info`. The restart counter is reset when a transaction ends, so a
normal idle query reports zero.

## Restart Behavior

An nRF24 `MAX_RT` result is ambiguous: the packet may have arrived while its ACK was lost. The bridge never retries that data packet alone and never moves to the next sequence.

It instead clears `MAX_RT`, flushes TX, waits 100 ms, sends a new ABN control packet, waits 40 ms, returns `RF_RESTART`, and resets its expected sequence and CRC. Linux then streams from sequence 1. Three complete restarts are allowed. The fourth uncertain data delivery ends with `RF_MAX_RT`.

## Arch Linux Packages

```sh
sudo pacman -S --needed \
  base-devel git cmake ninja rust \
  riscv64-elf-binutils riscv64-elf-gcc riscv64-elf-newlib \
  riscv64-elf-gdb usbutils libusb systemd-libs

yay -S wlink
```

`riscv64-elf-gcc` supports the required RV32 target through `-march=rv32imac_zicsr_zifencei -mabi=ilp32`. MounRiver Studio and WCH's modified compiler are not required.

The firmware has been built successfully with Arch `riscv64-elf-gcc` 15.2,
binutils 2.45.1 and newlib 4.6, and with GCC 16.2.0, binutils 2.47 and newlib
4.6.0.20260123. CMake rejects GCC older than 15. Compiler binaries are not
pinned, so retain a package snapshot or container when byte-for-byte
reproducibility is required.

For non-root WCH-LinkE access, create `/etc/udev/rules.d/60-wch-link.rules`:

```udev
SUBSYSTEM=="usb", ATTR{idVendor}=="1a86", ATTR{idProduct}=="8010", MODE="0660", TAG+="uaccess"
SUBSYSTEM=="usb", ATTR{idVendor}=="1a86", ATTR{idProduct}=="8012", MODE="0660", TAG+="uaccess"
```

```sh
sudo udevadm control --reload
sudo udevadm trigger --subsystem-match=usb
wlink list
```

The WCH-LinkE must be in RISC-V/RV mode, normally USB ID `1a86:8010`.

## Build the Linux Uploader

```sh
cmake -S bridge -B bridge/build-host -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build bridge/build-host
ctest --test-dir bridge/build-host --output-on-failure
```

The executable is `bridge/build-host/abnload`.

## Build the CH32 Firmware

```sh
bash bridge/firmware/fetch-sdk.sh bridge/firmware/third_party/ch32v20x

cmake -S bridge/firmware -B bridge/build-firmware -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$PWD/bridge/firmware/cmake/ch32v203-toolchain.cmake" \
  -DCH32V20X_SDK="$PWD/bridge/firmware/third_party/ch32v20x" \
  -DCMAKE_BUILD_TYPE=Release
cmake --build bridge/build-firmware
```

Outputs:

```text
bridge/build-firmware/abn_ch32v203_bridge.elf
bridge/build-firmware/abn_ch32v203_bridge.bin
bridge/build-firmware/abn_ch32v203_bridge.hex
bridge/build-firmware/abn_ch32v203_bridge.map
```

The SDK is pinned to commit `b375bbbc7dfe8642d6fb7c17e6d3207ee1c3c9c1`.

## Flash with WCH-LinkE

Connect target reference, ground, SDI and clock to the board's 3V3, GND, PA13 and PA14 debug pins. Avoid powering the board simultaneously from both probe and USB unless the exact board power arrangement is understood.

```sh
wlink flash bridge/build-firmware/abn_ch32v203_bridge.elf
```

Reset the board and connect its USB-C port to Linux. It should enumerate through `cdc_acm` as `/dev/ttyACM*`.

The firmware uses the WCH example's `1a86:fe0c` VID/PID and USB strings. These are suitable for personal experimentation with the WCH MCU, but a distributed product needs an authorized VID/PID.

If `1a86:fe0c` appears in `lsusb` but no `/dev/ttyACM*` appears, check the
kernel module and installed module tree:

```sh
lsusb -d 1a86:fe0c
uname -r
modinfo cdc_acm
sudo modprobe cdc_acm
sudo journalctl -k -b | grep -E 'cdc_acm|ttyACM|1a86'
```

`modinfo` must resolve under `/lib/modules/$(uname -r)`. Reboot after an Arch
kernel upgrade if the running kernel and installed modules differ. Serial
devices are normally assigned to `uucp` on Arch; group changes require a new
login session.

## Flash with the Factory USB Bootloader

The CH32V203 ROM USB ISP bootloader uses the board's USB-C connector and does
not require a WCH-LinkE. Install the first upstream revision containing the
option-byte `WRITE_CONFIG` fix directly into `~/.local/bin`:

```sh
cargo install \
  --root "$HOME/.local" \
  --git https://github.com/ch32-rs/wchisp \
  --rev 08ab9bfc994161a48554b9f70271465fc6ca5ce6 \
  --force
hash -r
```

The released AUR `wchisp 0.3.0` can flash an already-unprotected device, but it
must not be used for `config unprotect` because its option-byte command is one
byte short. Verify that `command -v wchisp` selects the fixed installation.

For non-root access, create `/etc/udev/rules.d/50-wchisp.rules`:

```udev
SUBSYSTEM=="usb", ATTR{idVendor}=="4348", ATTR{idProduct}=="55e0", GROUP="uucp", MODE="0660", TAG+="uaccess"
SUBSYSTEM=="usb", ATTR{idVendor}=="1a86", ATTR{idProduct}=="55e0", GROUP="uucp", MODE="0660", TAG+="uaccess"
```

Reload the rules and reconnect or reset the board:

```sh
sudo udevadm control --reload-rules
sudo udevadm trigger
```

Enter USB ISP mode:

1. Connect USB-C to Linux.
2. Hold BOOT0.
3. Press and hold NRST.
4. Release NRST while continuing to hold BOOT0.
5. Wait approximately 0.5 seconds and release BOOT0.

Confirm that the ROM bootloader appears:

```sh
lsusb -d 4348:55e0
wchisp probe
```

Some ROM revisions use USB VID `1a86` instead of `4348`. The expected target
identification is `CH32V203C8T6[0x3119]`.

Re-enter USB ISP mode after probing, then perform the flash as a single command:

```sh
wchisp -u flash bridge/build-firmware/abn_ch32v203_bridge.bin
```

`wchisp flash` erases, programs, verifies, and resets by default. Ensure BOOT0
has been released before that reset so the MCU starts CodeFlash rather than
returning to the ROM bootloader. BIN, ELF and Intel HEX inputs are supported;
the BIN used here begins at CodeFlash offset zero.

If a new board reports `RDPR=FF`, it is read-protected. Removing protection
mass-erases CodeFlash. With the fixed `wchisp` build, enter ISP mode and run:

```sh
wchisp -u -v config unprotect
```

Then completely remove USB power, re-enter ISP mode, and verify the new option
state before flashing:

```sh
wchisp -u -v info
# Must show: protected false, RDPR A5, WRP FFFFFFFF
wchisp -u -v flash bridge/build-firmware/abn_ch32v203_bridge.bin
```

If `RDPR` remains `FF`, stop repeating writes and recover with the official WCH
tool or WCH-LinkE instead.

USB ISP recovery remains available even if the application firmware is broken.
Do not use `wchisp config unprotect` or `config reset` casually because option
changes can erase CodeFlash.

## Upload an ABN Program

```sh
bridge/build-host/abnload --device /dev/ttyACM0 build/userland.bin
bridge/build-host/abnload --device /dev/ttyACM0 --patched-rom --verbose program.bin
bridge/build-host/abnload --device /dev/ttyACM0 --patched-rom --run program.bin
bridge/build-host/abnload --allow-unsafe-size large.bin
bridge/build-host/abnload --device /dev/ttyACM0 --info
```

The CDC baud rate is ignored by USB transport. The uploader selects raw termios mode and uses 115200 only as a conventional CDC line-coding value.

### First Hardware Test

Build the standalone 122-byte 65C02 smoke test:

```sh
sh tests/build_wireless_test.sh
```

Upload it:

```sh
bridge/build-host/abnload --device /dev/ttyACM0 \
  --patched-rom --run build/wireless_test.bin
```

`--run` requires the updated ROM and implies patched-ROM mode. It sets control
flag bit 0, making that ROM execute `$0300` immediately after receiving the
advertised byte count. A stock ROM ignores the flag and still requires F5 or
`run`. Without `--run`, the updated ROM also retains manual behavior. A
successful run prints:

```text
WIRELESS UPLOAD TEST PASSED!
CTRL7 NOW BLINKS ONCE PER SECOND.
RESET TO EXIT.
```

CTRL7 then uses approximately one second on and one second off, visibly
different from the ROM heartbeat. The test disables IRQ handling and polls VIA
Timer 1 for 250 approximately 4 ms periods per phase. It therefore confirms
both uploaded-code execution and VIA Timer 1 operation. The test uses the ROM's
fixed `$ffc8` inline print entry and preserves VEN and the `$0800` VRAM
selection while blinking. It uses the 65C02-only `BRA` instruction and is not
directly suitable for an NMOS 6502.

For an NMOS-compatible runtime test of the Rockwell/WDC RMB bit instructions,
build and autorun `build/rockwell_test.bin` as documented in
[`../WIRELESS_LOADER.md`](../WIRELESS_LOADER.md). It uses a fast CTRL7 pattern
when RMB behavior is detected and a slow pattern otherwise.

## Validation

The native C++ test covers the IEEE CRC32 vector, USB request/data/response and
INFO codecs, corruption rejection, canonical padding, RF sequences 255-257,
length boundaries, exact multiples of 30, and reconstruction through 65,535
bytes. Its INFO fixture uses a synthetic version rather than asserting the
firmware build's semantic version. The separate Python receiver model covers
8-bit length rollovers, complete control classification, packet 256, exact
packet counts, autorun, invalid RF widths, zero length and the protocol maximum.

```sh
ctest --test-dir bridge/build-host --output-on-failure
python3 -m unittest discover -s tests -v
```

These currently pass as one native CTest and eight Python tests. The firmware
build uses 13,588 bytes of text/constant data, 272 bytes of initialized data,
and 2,412 bytes of BSS; its BIN is 13,860 bytes. USB CDC enumeration, INFO,
nRF24 initialization, PB1 IRQ delivery, RF upload, autorun and uploaded VIA
Timer 1 code have all been verified on hardware with firmware 1.3.0. The
122-byte smoke test completed as five acknowledged RF packets and immediately
changed the ABN6502 CTRL7 heartbeat after autorun.
