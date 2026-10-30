import unittest


PAYLOAD_SIZE = 32
DATA_SIZE = 30


def control_packet(length, flags=0):
    if not 1 <= length <= 0xFFFF:
        raise ValueError("length is not representable by the protocol")
    packet = bytearray(PAYLOAD_SIZE)
    packet[1] = 0x31
    packet[2] = length & 0xFF
    packet[3] = length >> 8
    packet[4] = flags & 0x01
    return bytes(packet)


def data_packets(image):
    packets = []
    for offset in range(0, len(image), DATA_SIZE):
        sequence = offset // DATA_SIZE + 1
        packet = bytearray(PAYLOAD_SIZE)
        packet[0] = sequence & 0xFF
        packet[1] = sequence >> 8
        chunk = image[offset : offset + DATA_SIZE]
        packet[2 : 2 + len(chunk)] = chunk
        packets.append(bytes(packet))
    return packets


class ReceiverModel:
    def __init__(self):
        self.remaining_low = 0
        self.remaining_high = 0
        self.autorun = False
        self.image = bytearray()

    @property
    def remaining(self):
        return self.remaining_low | self.remaining_high << 8

    def receive(self, packet):
        if len(packet) != PAYLOAD_SIZE:
            return "invalid"

        if packet[0] == 0 and packet[1] == 0x31:
            self.remaining_low = packet[2]
            self.remaining_high = packet[3]
            self.autorun = (packet[4] & 0x01) != 0
            self.image.clear()
            return "control"

        if self.remaining == 0:
            return "inactive"

        for value in packet[2:]:
            self.image.append(value)
            if self.remaining_low == 0:
                self.remaining_high = (self.remaining_high - 1) & 0xFF
            self.remaining_low = (self.remaining_low - 1) & 0xFF
            if (self.remaining_low | self.remaining_high) == 0:
                return "complete"
        return "data"


class WirelessLoaderTests(unittest.TestCase):
    def transfer(self, length):
        source = bytes(index & 0xFF for index in range(length))
        receiver = ReceiverModel()
        self.assertEqual(receiver.receive(control_packet(length)), "control")
        packets = data_packets(source)
        for packet in packets:
            receiver.receive(packet)
        self.assertEqual(receiver.remaining, 0)
        self.assertEqual(receiver.image, source)
        return packets, receiver

    def test_lengths_around_8_bit_rollovers(self):
        for length in (255, 256, 257, 511, 512, 513, 767, 768, 769):
            with self.subTest(length=length):
                self.transfer(length)

    def test_sequence_255_through_257_is_data(self):
        packets, _ = self.transfer(7681)
        receiver = ReceiverModel()
        receiver.receive(control_packet(7681))

        self.assertEqual(packets[254][:2], b"\xff\x00")
        self.assertEqual(packets[255][:2], b"\x00\x01")
        self.assertEqual(packets[256][:2], b"\x01\x01")
        self.assertEqual(receiver.receive(packets[254]), "data")
        self.assertEqual(receiver.receive(packets[255]), "data")
        self.assertEqual(receiver.receive(packets[256]), "data")

    def test_first_image_requiring_sequence_256(self):
        self.assertEqual(len(data_packets(bytes(7650))), 255)
        self.assertEqual(len(data_packets(bytes(7651))), 256)

    def test_exact_multiples_of_30_have_no_extra_packet(self):
        for length in (30, 60, 255 * 30, 256 * 30):
            with self.subTest(length=length):
                packets, receiver = self.transfer(length)
                self.assertEqual(len(packets), length // DATA_SIZE)
                self.assertEqual(receiver.receive(bytes(PAYLOAD_SIZE)), "inactive")

    def test_control_requires_complete_signature(self):
        receiver = ReceiverModel()
        receiver.receive(control_packet(256))
        packet_256 = bytearray(PAYLOAD_SIZE)
        packet_256[:2] = b"\x00\x01"
        self.assertEqual(receiver.receive(packet_256), "data")

    def test_autorun_control_flag(self):
        receiver = ReceiverModel()
        receiver.receive(control_packet(1, flags=1))
        self.assertTrue(receiver.autorun)
        receiver.receive(control_packet(1))
        self.assertFalse(receiver.autorun)

    def test_payload_width_must_be_32(self):
        receiver = ReceiverModel()
        receiver.receive(control_packet(1))
        for width in (0, 1, 31, 33):
            with self.subTest(width=width):
                self.assertEqual(receiver.receive(bytes(width)), "invalid")
                self.assertEqual(receiver.remaining, 1)

    def test_zero_and_protocol_maximum(self):
        with self.assertRaises(ValueError):
            control_packet(0)
        with self.assertRaises(ValueError):
            control_packet(0x10000)
        packets, _ = self.transfer(0xFFFF)
        self.assertEqual(len(packets), 2185)


if __name__ == "__main__":
    unittest.main()
