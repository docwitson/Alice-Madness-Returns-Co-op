#pragma once

#include "../Protocol.hpp"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace AliceCoopCore
{
	inline bool IsNewerSequence(
		std::uint32_t candidate, std::uint32_t current)
	{
		return static_cast<std::int32_t>(candidate - current) > 0;
	}

	inline std::uint32_t HashMapName(std::string_view mapName)
	{
		std::uint32_t hash = 2166136261u;
		for (const unsigned char character : mapName)
		{
			const unsigned char normalized = static_cast<unsigned char>(
				std::tolower(character));
			hash ^= normalized;
			hash *= 16777619u;
		}
		return hash;
	}

	inline std::uint64_t WorldTraceHash(std::string_view value)
	{
		std::uint64_t hash = 1469598103934665603ull;
		for (const unsigned char byte : value)
		{
			hash ^= byte;
			hash *= 1099511628211ull;
		}
		return hash;
	}

	inline std::uint32_t SaveChunkChecksum(
		const std::uint8_t* data, std::size_t size)
	{
		std::uint32_t value = 2166136261u;
		for (std::size_t index = 0; index < size; ++index)
		{
			value ^= data[index];
			value *= 16777619u;
		}
		return value;
	}

	inline std::size_t SaveChunkCount(std::size_t byteCount)
	{
		return (byteCount + AliceCoopProtocol::SaveSyncChunkBytes - 1)
			/ AliceCoopProtocol::SaveSyncChunkBytes;
	}

	inline std::size_t SaveChunkDataSize(
		std::size_t byteCount, std::size_t chunkIndex)
	{
		if (chunkIndex >= SaveChunkCount(byteCount))
			return 0;
		const std::size_t offset =
			chunkIndex * AliceCoopProtocol::SaveSyncChunkBytes;
		return (std::min)(AliceCoopProtocol::SaveSyncChunkBytes,
			byteCount - offset);
	}
}
