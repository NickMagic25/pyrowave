// Copyright (c) 2026
// SPDX-License-Identifier: MIT

#include "../pyrowave_bitstream.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <vector>

namespace
{
using namespace PyroWave;

void require(bool condition, const char *message)
{
	if (!condition)
		throw std::runtime_error(message);
}

struct FrameFixture
{
	BlockLayout layout;
	std::vector<BitstreamPacket> metadata;
	std::vector<uint32_t> raw;
	size_t active_blocks = 0;

	FrameFixture(int width, int height, ChromaSubsampling chroma, uint32_t sequence)
	{
		require(layout.init(width, height, chroma), "Layout creation failed");
		metadata.resize(size_t(layout.block_count_32x32));
		for (size_t i = 0; i < metadata.size(); i++)
		{
			// Include uncoded blocks, including block zero, to exercise total_blocks
			// and the sequence header independently from the full layout count.
			if (i % 7 == 0)
				continue;
			BitstreamHeader header = {};
			header.payload_words = 2;
			header.sequence = sequence;
			header.block_index = uint32_t(i);
			metadata[i] = { uint32_t(raw.size()), 2 };
			raw.resize(raw.size() + 2);
			std::memcpy(raw.data() + metadata[i].offset_u32, &header, sizeof(header));
			active_blocks++;
		}
	}
};

struct PackedFrame
{
	static constexpr size_t GuardWords = 4;
	static constexpr uint32_t Guard = 0xa5a5a5a5u;
	std::vector<uint32_t> storage;
	std::vector<Packet> packets;
	size_t byte_size = 0;

	const uint8_t *data() const
	{
		return reinterpret_cast<const uint8_t *>(storage.data() + GuardWords);
	}
};

constexpr size_t PackedFrame::GuardWords;
constexpr uint32_t PackedFrame::Guard;

PackedFrame pack(const FrameFixture &frame, size_t boundary)
{
	PackedFrame packed;
	const size_t count = compute_num_packets(frame.layout, frame.metadata.data(), boundary);
	require(count != 0, "Packet count was zero");
	const size_t payload_words = frame.raw.size() + sizeof(BitstreamSequenceHeader) / sizeof(uint32_t);
	packed.byte_size = payload_words * sizeof(uint32_t);
	packed.storage.assign(payload_words + 2 * PackedFrame::GuardWords, PackedFrame::Guard);
	const Packet sentinel = { std::numeric_limits<size_t>::max(), std::numeric_limits<size_t>::max() };
	std::vector<Packet> packet_storage(count + 2, sentinel);
	const size_t written = packetize(frame.layout, packet_storage.data() + 1, boundary,
	                                packed.storage.data() + PackedFrame::GuardWords, packed.byte_size,
	                                frame.metadata.data(), frame.raw.data());
	require(written == count, "Packet count and packetization disagreed");
	require(packet_storage.front().offset == sentinel.offset && packet_storage.front().size == sentinel.size &&
	        packet_storage.back().offset == sentinel.offset && packet_storage.back().size == sentinel.size,
	        "Packetization overwrote packet-array guards");
	for (size_t i = 0; i < PackedFrame::GuardWords; i++)
	{
		require(packed.storage[i] == PackedFrame::Guard, "Packetization overwrote the output prefix");
		require(packed.storage[PackedFrame::GuardWords + payload_words + i] == PackedFrame::Guard,
		        "Packetization overwrote the output suffix");
	}
	packed.packets.assign(packet_storage.begin() + 1, packet_storage.begin() + 1 + written);
	size_t next = 0;
	for (const auto &packet : packed.packets)
	{
		require(packet.offset == next && packet.size != 0, "Packet ranges were not contiguous");
		require(packet.offset <= packed.byte_size && packet.size <= packed.byte_size - packet.offset,
		        "Packet range exceeded output capacity");
		require(packet.size <= boundary, "Minimal blocks exceeded the requested packet boundary");
		next += packet.size;
	}
	require(next == packed.byte_size, "Packetization did not cover the entire frame");
	return packed;
}

void push(BitstreamParser &parser, const PackedFrame &frame, size_t index)
{
	const auto &packet = frame.packets[index];
	require(parser.push_packet(frame.data() + packet.offset, packet.size), "Valid packet was rejected");
}

void verify_complete(const BitstreamParser &parser, const FrameFixture &frame, uint32_t sequence)
{
	require(parser.decode_is_ready(false), "Complete packetized frame was not ready");
	require(size_t(parser.get_decoded_blocks()) == frame.active_blocks, "Decoded block count was wrong");
	require(size_t(parser.get_total_blocks_in_sequence()) == frame.active_blocks, "Sequence block count was wrong");
	require(parser.payload().size() == frame.raw.size(), "Assembled payload size was wrong");
	const auto &offsets = parser.dequant_offsets();
	for (size_t i = 0; i < frame.metadata.size(); i++)
	{
		if (!frame.metadata[i].num_words)
		{
			require(offsets[i] == UINT32_MAX, "Uncoded block acquired a payload offset");
			continue;
		}
		require(offsets[i] <= parser.payload().size() &&
		        2 <= parser.payload().size() - offsets[i], "Decoded payload offset was out of bounds");
		BitstreamHeader header = {};
		std::memcpy(&header, parser.payload().data() + offsets[i], sizeof(header));
		require(header.block_index == i && header.payload_words == 2 && header.sequence == sequence,
		        "Assembled block header did not match the source");
	}
}

void roundtrip(int width, int height, ChromaSubsampling chroma, size_t expected_blocks, size_t boundary)
{
	FrameFixture fixture(width, height, chroma, 2);
	require(size_t(fixture.layout.block_count_32x32) == expected_blocks, "Known full-resolution block count changed");
	const auto packed = pack(fixture, boundary);
	BitstreamParser parser;
	parser.init(&fixture.layout);
	for (size_t i = 0; i < packed.packets.size(); i++)
		push(parser, packed, i);
	verify_complete(parser, fixture, 2);
}

void reordered_duplicate_and_missing_packets()
{
	FrameFixture fixture(3440, 1440, ChromaSubsampling::Chroma444, 3);
	const auto packed = pack(fixture, 1200);
	require(packed.packets.size() > 2, "Fixture did not exercise multiple network packets");
	BitstreamParser parser;
	parser.init(&fixture.layout);
	for (size_t i = packed.packets.size(); i > 0; i--)
	{
		push(parser, packed, i - 1);
		push(parser, packed, i - 1);
	}
	verify_complete(parser, fixture, 3);

	parser.clear();
	const size_t missing = packed.packets.size() / 2;
	for (size_t i = 0; i < packed.packets.size(); i++)
		if (i != missing)
			push(parser, packed, i);
	require(!parser.decode_is_ready(false), "A frame with a missing packet was marked complete");
	push(parser, packed, missing);
	verify_complete(parser, fixture, 3);
}

void sequence_wrap_and_replay()
{
	FrameFixture fixture(3840, 2160, ChromaSubsampling::Chroma444, 6);
	BitstreamParser parser;
	parser.init(&fixture.layout);
	for (uint32_t sequence : { 6u, 7u, 0u, 1u })
	{
		for (const auto &meta : fixture.metadata)
		{
			if (!meta.num_words)
				continue;
			BitstreamHeader header = {};
			std::memcpy(&header, fixture.raw.data() + meta.offset_u32, sizeof(header));
			header.sequence = sequence;
			std::memcpy(fixture.raw.data() + meta.offset_u32, &header, sizeof(header));
		}
		const auto packed = pack(fixture, UINT32_MAX);
		push(parser, packed, 0);
		verify_complete(parser, fixture, sequence);
		parser.mark_frame_decoded();
		push(parser, packed, 0);
		require(!parser.decode_is_ready(false), "Replay made an already submitted sequence ready again");
	}
}

void zeroed_encoder_payload_is_not_complete()
{
	FrameFixture fixture(3440, 1440, ChromaSubsampling::Chroma444, 2);
	std::fill(fixture.metadata.begin(), fixture.metadata.end(), BitstreamPacket{});
	fixture.raw.assign(6, 0);
	fixture.active_blocks = 3;
	// Reproduce the failure shape: nonzero metadata refers to zeroed payloads,
	// while one real header at offset zero supplies the frame's sequence.
	fixture.metadata[1] = { 2, 2 };
	fixture.metadata[2] = { 4, 2 };
	fixture.metadata.back() = { 0, 2 };
	BitstreamHeader valid = {};
	valid.payload_words = 2;
	valid.sequence = 2;
	valid.block_index = uint32_t(fixture.metadata.size() - 1);
	std::memcpy(fixture.raw.data(), &valid, sizeof(valid));
	const auto packed = pack(fixture, UINT32_MAX);
	BitstreamParser parser;
	parser.init(&fixture.layout);
	const auto &packet = packed.packets.front();
	parser.push_packet(packed.data() + packet.offset, packet.size);
	require(!parser.decode_is_ready(false), "Zeroed encoder payload was falsely marked complete");
	require(parser.get_total_blocks_in_sequence() == 3, "Corruption lost the frame's expected block count");
}
}

int main()
{
	try
	{
		for (size_t boundary : { size_t(1200), size_t(UINT32_MAX) })
		{
			roundtrip(3440, 1440, ChromaSubsampling::Chroma420, 7683, boundary);
			roundtrip(3440, 1440, ChromaSubsampling::Chroma444, 15135, boundary);
			roundtrip(3840, 2160, ChromaSubsampling::Chroma420, 12429, boundary);
			roundtrip(3840, 2160, ChromaSubsampling::Chroma444, 24669, boundary);
		}
		reordered_duplicate_and_missing_packets();
		sequence_wrap_and_replay();
		zeroed_encoder_payload_is_not_complete();
		std::puts("Bitstream roundtrip controls passed (3440/4K, 420/444, packet bounds, reorder/duplicates/drop, sequence wrap, corruption).");
		return 0;
	}
	catch (const std::exception &error)
	{
		std::fprintf(stderr, "bitstream_roundtrip: %s\n", error.what());
		return 1;
	}
}
