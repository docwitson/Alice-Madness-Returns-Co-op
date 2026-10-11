#pragma once

#include "../Protocol.hpp"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cmath>
#include <string_view>

namespace AliceCoopCore
{
	inline void PackAimOffset(float x, float y, std::uint8_t packed[3])
	{
		auto axis = [](float value) -> std::uint32_t
		{
			if (!std::isfinite(value)) value = 0.0f;
			const auto quantized = static_cast<std::int32_t>(std::lround(
				std::clamp(value, -1.0f, 1.0f) * 2047.0f));
			return static_cast<std::uint32_t>(quantized) & 0xfff;
		};
		const std::uint32_t bits = axis(x) | (axis(y) << 12);
		for (int i = 0; i < 3; ++i) packed[i] = static_cast<std::uint8_t>(bits >> (i * 8));
	}

	inline float UnpackAimOffset(const std::uint8_t packed[3], int axis)
	{
		const std::uint32_t bits = packed[0] | (static_cast<std::uint32_t>(packed[1]) << 8)
			| (static_cast<std::uint32_t>(packed[2]) << 16);
		const auto value = static_cast<std::int32_t>((bits >> (axis * 12)) & 0xfff);
		return (std::max)(-1.0f, (value >= 2048 ? value - 4096 : value) / 2047.0f);
	}

	inline std::uint32_t PackPawnStance(std::uint32_t flags, std::uint8_t stance)
	{
		return (flags & ~AliceCoopProtocol::PawnStanceMask)
			| (static_cast<std::uint32_t>(stance < 8 ? stance : 0) << AliceCoopProtocol::PawnStanceShift);
	}

	inline std::uint8_t UnpackPawnStance(std::uint32_t flags)
	{
		return static_cast<std::uint8_t>((flags & AliceCoopProtocol::PawnStanceMask)
			>> AliceCoopProtocol::PawnStanceShift);
	}

	inline int NativeAnimationSlot(std::uint8_t flags)
	{
		return static_cast<int>((flags >> 5) & 7) - 1;
	}

	inline std::uint8_t NativeFullBodyTimingFlags(float remaining, float pendingOut)
	{
		if (!std::isfinite(remaining) || remaining < 0.0f || !std::isfinite(pendingOut)) return 0;
		return static_cast<std::uint8_t>(AliceCoopProtocol::AnimationBlendNativeSlotTiming
			| (pendingOut < 0.0f ? AliceCoopProtocol::AnimationBlendNativeHold : 0));
	}

	inline float NativePendingBlendOut(std::uint8_t flags, std::uint8_t ticks)
	{
		return (flags & AliceCoopProtocol::AnimationBlendNativeHold) ? -1.0f : ticks * 0.002f;
	}

	inline std::uint16_t PackAnimationRootOptions(const std::uint8_t translation[3],
		const std::uint8_t rotation[3], bool zeroTranslation, bool zeroRotation)
	{
		std::uint16_t packed = (zeroTranslation ? 0x1000 : 0) | (zeroRotation ? 0x2000 : 0);
		for (int axis = 0; axis < 3; ++axis)
		{
			packed |= static_cast<std::uint16_t>((translation[axis] < 3 ? translation[axis] : 0) << (axis * 2));
			packed |= static_cast<std::uint16_t>((rotation[axis] < 3 ? rotation[axis] : 0) << (6 + axis * 2));
		}
		return packed;
	}

	inline std::uint8_t AnimationRootAxisOption(std::uint16_t packed, int axis, bool rotation)
	{
		const auto option = static_cast<std::uint8_t>((packed >> ((rotation ? 6 : 0) + axis * 2)) & 3);
		return option < 3 ? option : 0;
	}

	struct AnimationPhaseCorrection
	{
		float rate;
		bool snap;
	};

	inline AnimationPhaseCorrection CorrectAnimationPhase(float current, float target,
		float length, bool looping, float effectiveRate)
	{
		float error = target - current;
		if (looping && length > 0.001f)
			error = std::remainder(error, length);
		const float snapDistance = length > 0.001f ? (std::min)(0.25f, length * 0.5f) : 0.25f;
		if (std::fabs(error) > snapDistance)
			return {effectiveRate, true};
		// Correct small phase drift continuously; do not jump at every packet.
		const float limit = std::fabs(effectiveRate) * 0.10f;
		return {effectiveRate + std::clamp(error * 2.0f, -limit, limit), false};
	}

	// Native SetActiveChild scales the requested duration by (1 - current target weight).
	inline float NativeBlendTransitionTime(float remaining, float targetWeight)
	{
		if (!std::isfinite(remaining) || remaining <= 0.0f) return 0.0f;
		const float weight = std::isfinite(targetWeight) ? std::clamp(targetWeight, 0.0f, 1.0f) : 0.0f;
		return weight < 0.999f ? remaining / (1.0f - weight) : 0.0f;
	}

	inline float NormalizeAnimationRate(float effectiveRate, float meshScale, float assetScale)
	{
		const float scale = meshScale * assetScale;
		return std::isfinite(scale) && std::fabs(scale) > 0.0001f
			? effectiveRate / scale : 0.0f;
	}

	enum class VisualNotifyAdvance { Continue, Restart, LoopWrap, Correction };

	inline VisualNotifyAdvance ClassifyVisualNotifyAdvance(bool newSequence,
		bool newAction, bool looping, float previous, float current, float length)
	{
		if (newSequence) return VisualNotifyAdvance::Restart;
		if (current + 0.075f >= previous) return VisualNotifyAdvance::Continue;
		if (looping && length > 0.0f && previous > length * 0.65f
			&& current < length * 0.35f) return VisualNotifyAdvance::LoopWrap;
		return newAction ? VisualNotifyAdvance::Restart : VisualNotifyAdvance::Correction;
	}

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
