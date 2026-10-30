# ABN6502 nRF24 Wireless Loader

This document describes the current bug-fixed and enhanced ABN6502 ROM loader,
the existing Raspberry Pi sender, and the CH32V203 USB bridge. It covers the RF
wire format, loading state machine, execution behavior, build and programming
steps, validation, and remaining limitations.

The ROM and user programs continue to use ca65/ld65. Nothing in the wireless
loader requires 64tass or changes the overall firmware architecture.

## Quick Start with the CH32 Bridge

Requirements:

- ABN6502 programmed with the current `build/abn6502rom-29ee010.bin` image.
- CH32 bridge programmed with current firmware, version 1.3.0.
- nRF24L01+ modules attached to both systems.
- Linux bridge device visible as `/dev/ttyACM0` or another ACM number.

Build the Linux uploader if it is not already present:

```sh
cmake -S bridge -B bridge/build-host -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build bridge/build-host
```

Check the bridge:

```sh
bridge/build-host/abnload --device /dev/ttyACM0 --info
```

Build and upload the monitor-independent smoke test:

```sh
sh tests/build_wireless_test.sh
bridge/build-host/abnload --device /dev/ttyACM0 \
  --patched-rom --run build/wireless_test.bin
```

The 122-byte program is assembled for a 65C02 and uses the 65C02-only `BRA`
instruction. It is loaded at `$0300` and executed automatically. It
disables the ROM heartbeat and changes CTRL7 to approximately one second on and
one second off using VIA Timer 1. This has been verified on hardware: the
bridge sent five acknowledged RF data packets and the ABN6502 heartbeat changed
immediately after loading.

## System Architecture

```text
Linux abnload
    |
    | CRC-protected USB CDC stream
    v
CH32V203 bridge
    |
    | nRF24 Enhanced ShockBurst, 32-byte payloads
    v
ABN6502 nRF24 receiver
    |
    | bit-banged VIA SPI
    v
ABN6502 ROM loader -> RAM at $0300 -> optional execution
```

The Raspberry Pi sender in `software_linux/bootload.cpp` can replace the Linux
USB/CH32 side. Both senders generate the same RF control and data packets. The
CH32 bridge adds a separate USB protocol with CRC32, strict host-side sequence
checking, INFO reporting, lock-step flow control, and bounded restart.

## Memory Map and Size Limits

Wireless loading always begins at `$0300`.

| Item | Range or value |
|---|---|
| Load address | `$0300` |
| Linker USERLAND extent | `$0300-$39FF` |
| Linker USERLAND size | `$3700`, 14,080 bytes |
| RF length field | Nonzero unsigned 16-bit little-endian |
| RF protocol maximum | 65,535 bytes |
| Main ROM CPU window | `$8000-$FFFF`, 32 KiB |

The ROM does not enforce a destination bound. `$3700` is a linker extent, not
a runtime safety check or a promise that every address is ordinary RAM. The
configured VRAM window begins at `$0800`, inside the numerical USERLAND extent,
and larger transfers can reach memory-mapped hardware, ROM-selected space, and
eventually wrap the 16-bit address space.

Uploader policy:

| Mode | Limit and behavior |
|---|---|
| Default/legacy | At most 7,650 bytes; lengths divisible by 256 rejected |
| `--patched-rom` | At most 14,080 bytes |
| `--run` | Implies patched-ROM mode and requests execution |
| `--allow-unsafe-size` | At most 65,535 bytes; implies patched-ROM mode |

Use unsafe mode only with a deliberately designed hardware memory map.

## nRF24 Configuration

The receiver ROM bit-bangs SPI through the VIA. It explicitly:

- Flushes RX.
- Clears `RX_DR`.
- Enables dynamic payload length with `FEATURE=$04`.
- Enables dynamic payloads on all pipes with `DYNPD=$3F`.
- Writes `CONFIG=$0F`: powered RX, two-byte CRC.
- Raises CE.

The ROM relies on nRF24 reset defaults for several other settings. At boot it
checks that the first byte of `TX_ADDR` is `$E7`; failure disables further RF
polling. The expected shared settings are:

| Setting | Value |
|---|---|
| Channel | 2, 2.402 GHz |
| Air rate | 2 Mbps |
| Address width | 5 bytes |
| Pipe/address | `E7:E7:E7:E7:E7` |
| RF CRC | Two bytes |
| Auto-ack | Pipe 0 |
| Payload on air | Always 32 bytes |
| Transmitter retry delay | 4 ms |
| Transmitter retry count | 15 |
| Transmitter power | 0 dBm in the CH32 bridge |

The CH32 bridge uses 6 MHz hardware SPI, PB0 for CE, PA4 for CSN, and PB1 for
the active-low nRF IRQ. PB1 uses the MCU internal pull-up. See
`bridge/README.md` for the complete bridge wiring and build procedure.

Use 100 nF plus 10-47 uF directly at each radio module. PA/LNA modules normally
need a separate adequately rated 3.3 V regulator.

## RF Wire Protocol

Every application payload is exactly 32 bytes. The receiver rejects any other
dynamic payload width before writing the message buffer.

### Control Packet

| Offset | Size | Meaning |
|---:|---:|---|
| 0 | 1 | `$00` |
| 1 | 1 | `$31`, ASCII `1` |
| 2 | 2 | Image length, little-endian |
| 4 | 1 | Flags |
| 5 | 27 | Normally zero |

Control flag bit 0 is `AUTORUN`:

- Clear: finish loading and wait for F5 or `run`.
- Set: jump to `$0300` immediately after the byte count reaches zero.

Other flag bits are currently discarded by the ROM. A zero-length control
leaves the receiver inactive; both supported senders reject zero-length files.

### Data Packet

| Offset | Size | Meaning |
|---:|---:|---|
| 0 | 2 | Sequence number, little-endian, starting at 1 |
| 2 | 30 | Image bytes; final payload zero-padded |

Examples:

| Sequence | Header bytes |
|---:|---|
| 255 | `FF 00` |
| 256 | `00 01` |
| 257 | `01 01` |

There is no end packet. The ROM completes solely when its advertised remaining
byte count becomes zero. Final RF padding is not stored.

## ROM Receiver State Machine

### Probe and Initialization

Reset clears ROM workspace, configures VIA port B for SPI, checks the nRF
default `$E7` address byte, and initializes RX. If the probe fails, `RF_ERR` is
set and the main loop skips radio processing until reset.

### Poll and Payload Validation

The main loop polls nRF STATUS. When data may be present, `getmessage` issues
`R_RX_PL_WID` and accepts only 32. Width zero, a short payload, or a width above
32 causes RX reinitialization and FIFO flushing. A valid packet is read into
`MSGBUF`, `RX_DR` is cleared, and classification begins.

### Packet Classification

A packet is control only when bytes 0 and 1 are exactly `00 31`. Every other
header is treated as data while a transfer is active. This complete-signature
test is important: sequence 256 is `00 01` and must be data.

### Control Processing

For a valid control packet the ROM:

1. Loads bytes 2-3 into its 16-bit remaining counter.
2. Stores byte 4 bit 0 as the autorun flag.
3. Resets the destination pointer to `$0300`.
4. Returns to the polling loop.

A new control packet can restart an incomplete transfer by resetting the
pointer and count.

### Data Processing

The ROM ignores data-header bytes 0-1 and copies bytes 2-31 sequentially. After
each byte it performs a conventional 16-bit decrement:

```text
if low == 0:
    high--
low--
```

It tests `high | low` after the decrement. This correctly handles lengths such
as 256, 512 and 768. Copying stops immediately at zero, even in the middle of
the final 30-byte payload.

### Completion and Execution

At completion the ROM:

1. Sets the user run vector to `$0300`.
2. Clears the byte counter, destination high byte and keyboard pointers.
3. Jumps through the run vector if autorun is set.
4. Otherwise prints the manual execution message and returns to the monitor.

Manual execution is F5 or the monitor command `run`.

## Correctness Fixes in the Current ROM and Sender

### Sequence 256 Classification

The original ROM examined only data-header byte 0. Sequence 256 begins `00 01`
and was discarded as a malformed control-like packet after the radio had
already acknowledged it. Packet 256 begins at file offset 7,650, so the first
image requiring it is 7,651 bytes.

The current ROM recognizes control only as complete signature `00 31`.

### Lengths Divisible by 256

The original count algorithm incremented the high byte before transfer and
decremented high when the newly decremented low byte reached zero. Length 256
therefore completed after 512 bytes, 512 after 768, and so on.

The current ROM uses the conventional decrement shown above and explicitly
leaves zero-length transfers inactive.

### Sender Extra Packet

The original Pi loop sent an extra all-zero packet for image sizes divisible by
30. After normal completion that packet could be written into zero page, and it
also overran an unused host allocation. The current sender transmits exactly
`ceil(length / 30)` packets and no end packet.

### Payload Width

The original ROM trusted `R_RX_PL_WID`, allowing short payloads to reuse stale
buffer bytes and invalid widths to overrun the 32-byte message buffer. The
current ROM requires exactly 32 and flushes/reinitializes RX otherwise.

### Autorun Extension

Control byte 4 was previously unused. Bit 0 now requests execution at `$0300`
after complete reception. Zero remains compatible with the original manual-run
workflow. An older ROM ignores the flag, so `--run` requires the current ROM.

## Reliability Model

### What an RF ACK Means

An Enhanced ShockBurst ACK means that the receiver radio accepted a CRC-valid
packet. It does not prove that the 6502:

- Read the RX FIFO.
- Classified the packet as intended.
- Stored every byte.
- Has a source-identical image.
- Executed the image.

The CH32 bridge's `DONE` means that each transmission reached `TX_DS` and the
bytes streamed over USB matched the host image CRC. That CRC does not verify
ABN6502 RAM.

### Pacing

Both supported transmitters wait 40 ms after each successfully acknowledged
data packet. The CH32 bridge also waits 40 ms after control. This conservative
delay gives the 6502 time to poll the radio and drain its three-payload RX FIFO.
Hardware ACK by itself is not sufficient flow control for the 6502 software.

### Sequence Handling

The ROM does not compare packet sequence numbers with an expected value. It
cannot detect missing, duplicate, out-of-order, or stale RF packets. Any packet
other than exact `00 31` is accepted as the next data while a transfer is
active.

The CH32 USB protocol validates request IDs, sequence numbers, chunk lengths,
padding and CRC before RF transmission, but those checks are not repeated by
the 6502.

### Whole-Transfer Restart

An nRF `MAX_RT` is ambiguous because the payload might have arrived while its
ACK was lost. The bridge therefore does not retry only that data packet. It
clears/flushed TX, waits, sends a new control packet, resets transfer state, and
asks Linux to replay from sequence 1. Three whole-transfer restarts are
permitted. The Pi sender also sends a new control and rewinds after a failed
data write, but does not impose the bridge's whole-transfer restart limit.

## CH32 Bridge Uploader

Build:

```sh
cmake -S bridge -B bridge/build-host -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build bridge/build-host
```

Inspect the bridge:

```sh
bridge/build-host/abnload --device /dev/ttyACM0 --info
```

Manual execution after loading:

```sh
bridge/build-host/abnload --device /dev/ttyACM0 \
  --patched-rom program.bin
```

Automatic execution:

```sh
bridge/build-host/abnload --device /dev/ttyACM0 \
  --patched-rom --run program.bin
```

`--run` already implies patched-ROM mode; spelling both options emphasizes the
ROM requirement. Add `--verbose` for per-packet progress. See
`bridge/README.md` for the USB protocol, firmware build, wiring, flashing and
recovery procedures.

## Raspberry Pi Sender

The existing sender is `software_linux/bootload.cpp`. It uses RF24 with CE GPIO
22 and SPI/CS device 0. It sends the same control and data layout but leaves
control byte 4 zero, so execution remains manual.

Representative build command from the source comment:

```sh
g++ -Ofast -Wall -pthread \
  -I/usr/local/include/RF24/.. -L/usr/local/lib \
  software_linux/bootload.cpp -lrf24 -o bootload
```

Upload:

```sh
sudo ./bootload -s build/wireless_test.bin
```

Then press F5 or type `run`. The Pi sender has no safe memory-map limit, image
CRC, application completion response, or autorun option. It also retains some
rough command-line behavior: unused getopt letters, noisy packet printing, and
weak cancellation/error reporting.

## Building the ABN6502 ROM

Dependencies:

- cc65 tools: `ca65` and `ld65`.
- Microsoft BASIC source, because current `abn6502rom.s` has `BASIC := 1`.

Clone BASIC into the repository:

```sh
git clone https://github.com/mist64/msbasic.git msbasic
```

Build without invoking a programmer or remote Raspberry Pi deployment:

```sh
mkdir -p build
ca65 -vvv --cpu 6502 -l build/listing.txt \
  -o build/abn6502rom.o abn6502rom.s
ca65 crom.s -o build/crom.o
ca65 userland.s --cpu 65c02 -l build/userlisting.txt \
  -o build/userland.o
ld65 -o build/abn6502rom.bin -C memmap.cfg \
  build/abn6502rom.o build/crom.o build/userland.o
```

Outputs include:

| File | Purpose |
|---|---|
| `build/abn6502rom.bin` | 32 KiB main CPU ROM image |
| `build/crom.bin` | Character ROM image |
| `build/userland.bin` | Linked RAM/user program |

`assemble.sh` is not build-only: it currently invokes Firestarter programming
and then SSH/SCP deployment commands. Inspect and adapt it before running.

## Programming a 29EE010/SST39SF010-Sized ROM

The CPU sees only `$8000-$FFFF`, so ld65 emits 32 KiB. On Revision 2 the ROM's
A15 and A16 are pulled low, selecting the lowest 32 KiB of a 128 KiB device.
Mirroring is unnecessary.

For a programmer expecting a full 128 KiB image:

| EEPROM offsets | Content |
|---|---|
| `$00000-$07FFF` | `build/abn6502rom.bin` |
| `$08000-$1FFFF` | `$FF` padding |

Create and verify it from the repository root:

```sh
dd if=/dev/zero bs=131072 count=1 status=none | \
  tr '\000' '\377' > build/abn6502rom-29ee010.bin
dd if=build/abn6502rom.bin of=build/abn6502rom-29ee010.bin \
  bs=32768 count=1 conv=notrunc status=none
test "$(stat -c %s build/abn6502rom-29ee010.bin)" -eq 131072
cmp -n 32768 build/abn6502rom.bin build/abn6502rom-29ee010.bin
```

The generated artifact in this workspace is:

```text
build/abn6502rom-29ee010.bin
size: 131072 bytes
```

Select the programmer profile matching the actual installed chip. A 29EE010
and SST39SF010 are both 128 KiB devices but use different programming
algorithms. Program at offset zero, erase first, and verify after writing.

Revision 2's default resistor population already selects VRAM at `$0800`; no
VRAM jumper change is required for the current ROM.

## Smoke Test Program

Build:

```sh
sh tests/build_wireless_test.sh
```

The test is linked at `$0300`, prints through the fixed ROM `PRIMM` entry at
`$FFC8`, disables interrupts, and polls VIA Timer 1 for its one-second CTRL7
phases. It is a 65C02 program because its loop uses `BRA`; replace that branch
with an NMOS-compatible jump before using the test on an original 6502.

Upload and autorun:

```sh
bridge/build-host/abnload --device /dev/ttyACM0 \
  --patched-rom --run build/wireless_test.bin
```

Expected text when a monitor is connected:

```text
WIRELESS UPLOAD TEST PASSED!
CTRL7 NOW BLINKS ONCE PER SECOND.
RESET TO EXIT.
```

Without a monitor, the slower CTRL7 heartbeat is the execution indicator.
Reset the ABN6502 to exit.

## Rockwell/WDC Bit-Instruction Diagnostic

`tests/rockwell_test.s` is a monitor-independent capability test assembled for
the baseline NMOS instruction set. It probes opcode `$17`, which is `RMB1 zp`
on Rockwell/WDC-enhanced 65C02 implementations and undocumented `SLO zp,X` on
mainstream NMOS 6502 parts.

Build and upload:

```sh
sh tests/build_rockwell_test.sh
bridge/build-host/abnload --device /dev/ttyACM0 \
  --patched-rom --run build/rockwell_test.bin
```

Interpret CTRL7:

| Pattern | Result |
|---|---|
| Fast, about 124 ms per phase | `RMB1` behavior detected |
| Slow, about one second per phase | Rockwell bit behavior not detected, or test RAM failed |

The current image is 255 bytes. It uses zero-page `$EA` as both the test byte
and a harmless NOP operand, followed by NOP padding. That keeps control flow
aligned for credible one-, two-, and three-byte undefined-opcode behaviors. It
masks IRQs, clears decimal mode, verifies that the test location can hold
`$FF`, and configures VIA Timer 1 itself.

This detects an instruction capability, not a manufacturer. WDC and Rockwell
parts pass, as do compatible FPGA/emulator or other CPU cores implementing the
same RMB instruction. Unknown clones can define otherwise-unused opcodes
differently, so no illegal-opcode probe can be universal. Reset the ABN6502 to
exit the test.

## Automated Validation

ROM receiver behavioral model:

```sh
python3 -m unittest discover -s tests -v
```

Coverage includes 255/256 count boundaries, sequences 255-257, exact multiples
of 30, complete control signature, autorun, invalid payload widths, zero length,
and the 65,535-byte protocol maximum. This is a host model, not instruction-
level execution of the assembled ROM.

Bridge codec/protocol tests:

```sh
cmake -S bridge -B bridge/build-host -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build bridge/build-host
ctest --test-dir bridge/build-host --output-on-failure
```

The current suites pass as one native CTest and eight Python tests. The smoke
test has also been transferred and autorun successfully on physical hardware.

## Diagnostics

### ROM

With a monitor attached the ROM displays receive length, remaining-byte
progress, and manual-run instructions. An invalid dynamic payload width is
silently discarded after RX reinitialization. There is no displayed sequence,
bounds, CRC, duplicate, or incomplete-transfer error.

### Bridge

`abnload --info` reports firmware/protocol versions, radio initialization, live
IRQ state, image limits, uptime, last error, restart count and capabilities.
Defined errors cover unsupported command/version/flags, length, header/image
CRC, receive timeout, RF retry/timeout, USB disconnect, busy and internal
failure.

## Known Limitations

1. The ROM does not validate sequence numbers.
2. The ROM has no image checksum or final sender-visible acknowledgement.
3. RF ACK occurs before 6502 software consumption.
4. The ROM does not bound destination writes.
5. The `$3700` linker region overlaps the configured `$0800` VRAM window.
6. Several receiver radio settings rely on nRF24 reset defaults.
7. Invalid-width recovery flushes RX and can discard queued traffic.
8. A new unauthenticated control packet can restart an active transfer.
9. Autorun executes after byte-count completion without validating code
   integrity in ABN6502 RAM.
10. Bridge CRC protects USB delivery and source replay, not final 6502 memory.
11. The Pi sender lacks autorun, image CRC and machine-specific size bounds.

Future RF protocol work should add a session identifier, ROM-side expected
sequence, an image CRC, and an application-level completion response. Those
changes should be a separate protocol version rather than complicating this
compatible loader fix.

## Relevant Source Files

- `abn6502rom.s`: ROM receiver, countdown, payload validation and autorun.
- `software_linux/bootload.cpp`: Raspberry Pi RF24 sender.
- `bridge/common/abn_protocol.c`: USB and RF packet construction.
- `bridge/firmware/src/bridge.c`: CH32 transfer and restart state machine.
- `bridge/firmware/src/nrf24.c`: CH32 radio configuration and PB1 IRQ.
- `bridge/host/abnload.cpp`: Linux CDC uploader and INFO client.
- `tests/test_wireless_loader.py`: ROM behavioral regression model.
- `tests/wireless_test.s`: monitor-independent uploaded-code smoke test.
- `tests/rockwell_test.s`: Rockwell/WDC RMB capability and LED-speed test.
