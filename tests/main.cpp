#include "Coop/Core/PureHelpers.hpp"
#include "Coop/Protocol.hpp"

#include <array>
#include <cstdint>
#include <iostream>
#include <limits>
#include <string>

namespace
{
	int g_failures = 0;

	void Check(bool condition, const char* name)
	{
		if (condition)
			return;
		std::cerr << "FAIL: " << name << '\n';
		++g_failures;
	}

	void TestProtocolHeaders()
	{
		using namespace AliceCoopProtocol;
		Header ping{};
		ping.type = PacketType::Ping;
		Check(IsValidHeader(ping, sizeof(Header)), "zero-payload Ping");
		ping.type = PacketType::Pong;
		Check(IsValidHeader(ping, sizeof(Header)), "zero-payload Pong");

		const HelloPayload hello{};
		const auto packet = MakePacket(PacketType::Hello, 1, 2, 3, hello);
		Check(IsValidHeader(packet.header, sizeof(packet)), "valid Hello packet");

		Header invalid = packet.header;
		invalid.magic ^= 1;
		Check(!IsValidHeader(invalid, sizeof(packet)), "wrong magic");
		invalid = packet.header;
		++invalid.version;
		Check(!IsValidHeader(invalid, sizeof(packet)), "wrong version");
		invalid = packet.header;
		invalid.type = static_cast<PacketType>(0);
		Check(!IsValidHeader(invalid, sizeof(packet)), "unknown packet type zero");
		invalid.type = static_cast<PacketType>(999);
		Check(!IsValidHeader(invalid, sizeof(packet)), "unknown packet type high");
		invalid = packet.header;
		invalid.payloadSize = static_cast<std::uint32_t>(
			MaxDatagramSize - sizeof(Header) + 1);
		Check(!IsValidHeader(invalid, MaxDatagramSize + 1), "payload above maximum");
		Check(!IsValidHeader(packet.header, sizeof(packet) - 1),
			"declared payload longer than datagram");
		Check(!IsValidHeader(packet.header, sizeof(packet) + 1),
			"declared payload shorter than datagram");
	}

	void TestSequenceComparison()
	{
		using AliceCoopCore::IsNewerSequence;
		constexpr auto max = (std::numeric_limits<std::uint32_t>::max)();
		Check(!IsNewerSequence(10, 10), "equal sequence");
		Check(IsNewerSequence(11, 10), "ordinary newer sequence");
		Check(!IsNewerSequence(10, 11), "ordinary older sequence");
		Check(IsNewerSequence(0, max), "wrap UINT32_MAX to zero");
		Check(!IsNewerSequence(max, 0), "reverse wrap is older");
		Check(IsNewerSequence(0x7fffffffu, 0), "positive half-range neighbor");
		Check(!IsNewerSequence(0x80000000u, 0), "exact half-range forward");
		Check(!IsNewerSequence(0, 0x80000000u), "exact half-range reverse");
		Check(!IsNewerSequence(0x80000001u, 0), "negative half-range neighbor");
		Check(IsNewerSequence(0, 0x80000001u), "reverse negative half-range neighbor");
	}

	void TestHashes()
	{
		using namespace AliceCoopCore;
		Check(HashMapName("") == 0x811c9dc5u, "empty map hash");
		Check(HashMapName("Alice") == 0x872213e7u, "map hash golden vector");
		Check(HashMapName("Alice") == HashMapName("aLiCe"),
			"map hash is case-insensitive");
		Check(HashMapName("CHAPTER1_P") == 0x8960eda6u,
			"representative map hash");
		Check(WorldTraceHash("") == 0x14650fb0739d0383ull,
			"empty world trace hash");
		Check(WorldTraceHash("Alice") == 0xead2dc6636017b45ull,
			"world trace hash golden vector");
		Check(WorldTraceHash("Alice") != WorldTraceHash("alice"),
			"world trace hash is case-sensitive");
		const std::array<std::uint8_t, 4> bytes{ 0, 1, 2, 255 };
		Check(SaveChunkChecksum(nullptr, 0) == 0x811c9dc5u,
			"empty save checksum");
		Check(SaveChunkChecksum(bytes.data(), bytes.size()) == 0x6fab6075u,
			"save checksum byte order");
	}

	void TestSaveChunks()
	{
		using namespace AliceCoopCore;
		using AliceCoopProtocol::SaveSyncChunkBytes;
		Check(SaveChunkCount(0) == 0, "zero chunks");
		Check(SaveChunkCount(1) == 1, "single-byte chunk");
		Check(SaveChunkCount(SaveSyncChunkBytes) == 1, "full chunk");
		Check(SaveChunkCount(SaveSyncChunkBytes + 1) == 2, "two chunks");
		Check(SaveChunkDataSize(1, 0) == 1, "single-byte data size");
		Check(SaveChunkDataSize(SaveSyncChunkBytes, 0) == SaveSyncChunkBytes,
			"full chunk data size");
		Check(SaveChunkDataSize(SaveSyncChunkBytes + 1, 0)
			== SaveSyncChunkBytes, "first of two chunks");
		Check(SaveChunkDataSize(SaveSyncChunkBytes + 1, 1) == 1,
			"last partial chunk");
		Check(SaveChunkDataSize(SaveSyncChunkBytes + 1, 2) == 0,
			"out-of-range chunk");
		Check(SaveChunkDataSize(0, 0) == 0, "chunk in empty file");
	}
}

int main()
{
	TestProtocolHeaders();
	TestSequenceComparison();
	TestHashes();
	TestSaveChunks();
	if (g_failures != 0)
	{
		std::cerr << g_failures << " AliceCoop test(s) failed.\n";
		return 1;
	}
	std::cout << "AliceCoop tests passed.\n";
	return 0;
}
