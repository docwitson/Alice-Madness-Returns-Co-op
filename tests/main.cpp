#include "Coop/Core/PureHelpers.hpp"
#include "Coop/Protocol.hpp"
#include "../include/GfxMovieStreamOverride.hpp"
#include "../include/NativePersistentReadCache.hpp"
#include "../include/NativeSoundArgument.hpp"
#include "../include/StartupDisplayMode.hpp"
#include "../include/MenuDisplayAspect.hpp"

#include <array>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <string>

bool TestNativeAudioResultStorage();

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

    void TestNativeSoundArgument()
    {
        using namespace AliceCoopNative;
        std::uint32_t cue = 0;
        auto read = [](std::uint32_t address, std::uint32_t& value) {
            if (address != 0x1010 && address != 0x2010) return false;
            value = address == 0x1010 ? 0xaaaa : 0xbbbb; return true;
        };
        auto offset = [](std::uint32_t property, std::int32_t& value) {
            if (property != 0x3000) return false;
            value = 0x10; return true;
        };
        const std::array<std::uint8_t, 5> local{0, 0, 0x30, 0, 0};
        Check(ReadSoundArgument(local, 0x1000, 0x2000, read, offset, cue) && cue == 0xbbbb,
            "native sound local argument reads caller locals");
        auto instance = local; instance[0] = 1;
        Check(ReadSoundArgument(instance, 0x1000, 0x2000, read, offset, cue) && cue == 0xaaaa,
            "native sound instance argument reads actual actor");
        auto constant = local; constant[0] = 0x20;
        Check(ReadSoundArgument(constant, 0, 0, read, offset, cue) && cue == 0x3000,
            "native sound object constant");
        Check(!ReadSoundArgument(std::span(local).first(4), 0x1000, 0x2000, read, offset, cue),
            "truncated sound expression rejected");
        auto call = local; call[0] = 0x1c;
        Check(!ReadSoundArgument(call, 0x1000, 0x2000, read, offset, cue),
            "sound capture never evaluates an argument function twice");
        Check(!ReadSoundArgument(local, 0x1000, 0, read, offset, cue), "missing native frame locals rejected");
        Check(!ReadSoundArgument(instance, 0xfffffff0, 0, read, offset, cue), "sound address overflow rejected");
        auto invalid = local; invalid[1] = 4;
        Check(!ReadSoundArgument(invalid, 0x1000, 0x2000, read, offset, cue), "unknown sound property rejected");
        auto arrayRead = [](std::uint32_t address, std::uint32_t& value) {
            switch (address) {
            case 0x1010: value = 0x5000; return true;
            case 0x1014: value = 2; return true;
            case 0x5000: value = 0xaaaa; return true;
            case 0x5004: value = 0xbbbb; return true;
            default: return false;
            }
        };
        std::array<std::uint8_t, 7> array{0x10, 0x25, 1, 0, 0x30, 0, 0};
        Check(ReadSoundArgument(array, 0x1000, 0, arrayRead, offset, cue) && cue == 0xaaaa,
            "native firing sound array chooses authored first cue");
        array[1] = 0x26;
        Check(ReadSoundArgument(array, 0x1000, 0, arrayRead, offset, cue) && cue == 0xbbbb,
            "native firing sound array chooses authored second cue");
        Check(!ReadSoundArgument(std::span(array).first(6), 0x1000, 0, arrayRead, offset, cue),
            "truncated native firing array expression rejected");
        const std::array<std::uint8_t, 11> outOfBounds{0x10, 0x1d, 2,0,0,0, 1,0,0x30,0,0};
        Check(!ReadSoundArgument(outOfBounds, 0x1000, 0, arrayRead, offset, cue),
            "native firing array bounds checked before reading cue");
        Check(IsFreshAudioEvent(350, 0, false) && !IsFreshAudioEvent(351, 0, false), "late one-shot audio discarded");
        Check(IsFreshAudioEvent(600, 0, true) && !IsFreshAudioEvent(601, 0, true)
            && !IsFreshAudioEvent(1, 2, true), "loop timeout and clock underflow guarded");
    }

    void TestPersistentArchiveInvalidation()
    {
        using namespace AliceCoopNative;
        // Relocated persistent loader: a paired global read/store and buffer
        // layout are required. An unrelated operand or truncated code is unsafe.
        std::array<std::uint8_t, 256> code{};
        const std::uint8_t load[] = {0x8b,0x35,0x1c,0x70,0x49,1,0x3b,0xf3,
            0x75,0x2b,0x6a,8,0x68,0x94,0,0,0,0xe8,0,0,0,0};
        const std::uint8_t store[] = {0x89,0x35,0x1c,0x70,0x49,1,0x3b,0xc3,
            0x74,8,0x8d,0xbe,0x88,0,0,0};
        std::memcpy(code.data() + 40, load, sizeof(load));
        std::memcpy(code.data() + 83, store, sizeof(store));
        Check(FindPersistentArchiveCacheSlot(code) == 0x149701c, "persistent cache relocated read/store");
        Check(!FindPersistentArchiveCacheSlot({code.data(), 95}), "truncated archive loader rejected");
        code[85] ^= 4;
        Check(!FindPersistentArchiveCacheSlot(code), "unrelated archive store rejected");
        code[85] ^= 4;
        std::memcpy(code.data() + 150, load, sizeof(load));
        std::memcpy(code.data() + 193, store, sizeof(store));
        Check(!FindPersistentArchiveCacheSlot(code), "ambiguous archive loader rejected");
        ArchiveByteBuffer32 buffer{0x123400, 18782, 20000};
        int seeks = 0;
        Check(InvalidatePersistentArchiveBuffer(buffer, [&] { ++seeks; return true; })
            && !buffer.count && buffer.data == 0x123400 && buffer.capacity == 20000
            && seeks == 1, "invalidate stale profile bytes preserves native allocation");
        Check(InvalidatePersistentArchiveBuffer(buffer, [&] { ++seeks; return true; })
            && seeks == 1, "empty archive needs no rewind");
        buffer.count = 12;
        Check(!InvalidatePersistentArchiveBuffer(buffer, [] { return false; })
            && buffer.count == 12, "failed rewind preserves archive");
        buffer.count = 21000;
        Check(!InvalidatePersistentArchiveBuffer(buffer, [&] { ++seeks; return true; })
            && seeks == 1, "invalid archive layout rejected before native call");
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

		ProjectileEventPayload bomb{};
		bomb.kind = ProjectileEventKind::ClockBombUpdate;
		bomb.clockBombMaterialHashes[0] = 0x10203040;
		bomb.clockBombMaterialHashes[1] = 0x50607080;
		bomb.clockBombMaterialHashes[2] = 0x90a0b0c0;
		const auto bombPacket = MakePacket(PacketType::ProjectileEvent, 1, 2, 4, bomb);
		std::array<std::uint8_t, sizeof(bombPacket)> wire{};
		std::memcpy(wire.data(), &bombPacket, wire.size());
		Packet<ProjectileEventPayload> received{};
		std::memcpy(&received, wire.data(), wire.size());
		Check(IsValidHeader(received.header, wire.size()) && wire.size() <= MaxDatagramSize,
			"clock-bomb materials fit relay datagram");
		Check(received.payload.clockBombMaterialHashes[0] == 0x10203040
			&& received.payload.clockBombMaterialHashes[1] == 0x50607080
			&& received.payload.clockBombMaterialHashes[2] == 0x90a0b0c0,
			"all three clock-bomb material identities survive wire copy");
		Check(!IsValidHeader(received.header, wire.size() - sizeof(bomb.clockBombMaterialHashes)),
			"truncated material state rejected");
	}

    void TestGfxMovieStreamBoundary()
    {
        // Simulate the native tag loop after the GFX header has been consumed.
        // An enlarged first tag moves the initialization action past old EOF.
        std::vector<std::uint8_t> original(12, 0);
        std::vector<std::uint8_t> compiled(35, 0);
        const std::uint32_t originalLength = static_cast<std::uint32_t>(original.size());
        const std::uint32_t compiledLength = static_cast<std::uint32_t>(compiled.size());
        std::memcpy(original.data() + 4, &originalLength, sizeof(originalLength));
        std::memcpy(compiled.data() + 4, &compiledLength, sizeof(compiledLength));
        const std::uint16_t spriteTag = (39 << 6) | 20;
        const std::uint16_t actionTag = (12 << 6) | 1;
        std::memcpy(compiled.data() + 8, &spriteTag, sizeof(spriteTag));
        std::memcpy(compiled.data() + 30, &actionTag, sizeof(actionTag));
        auto reachesInitialization = [](const std::uint8_t* data, std::uint32_t size,
            std::uint32_t cachedEnd) {
            bool initialization = false;
            std::uint32_t position = 8;
            while (position < cachedEnd && position + 2 <= size)
            {
                std::uint16_t header = 0;
                std::memcpy(&header, data + position, sizeof(header));
                position += 2;
                const std::uint32_t payloadSize = header & 63;
                if (position + payloadSize > size) return false;
                if ((header >> 6) == 12) initialization = true;
                if ((header >> 6) == 0) return initialization;
                position += payloadSize;
            }
            return false;
        };
        Check(!reachesInitialization(compiled.data(), compiledLength, originalLength),
            "changing GFX bytes and header alone still truncates native root parsing");

        auto* data = original.data();
        std::uint32_t size = originalLength, end = originalLength;
        {
            GfxMovieStreamOverride override;
            Check(override.Apply(data, size, end, compiled), "GFX root override accepted");
            Check(reachesInitialization(data, size, end),
                "GFX cached bound permits late initialization action and End tag");
        }
        Check(data == original.data() && size == originalLength && end == originalLength,
            "GFX override restores engine allocation and cached bound together");

        end = originalLength - 1;
        {
            GfxMovieStreamOverride override;
            Check(!override.Apply(data, size, end, compiled), "unknown GFX loader bound rejected");
        }
        Check(data == original.data() && size == originalLength && end == originalLength - 1,
            "rejected GFX stream leaves all original fields untouched");
        end = originalLength;
        compiled[4] ^= 1;
        {
            GfxMovieStreamOverride override;
            Check(!override.Apply(data, size, end, compiled), "invalid compiled GFX length rejected");
        }
    }

    void TestSessionCheckpointIdentity()
    {
        using namespace AliceCoopCore;
        using namespace AliceCoopProtocol;
        const std::string map = "Chapter2_W1_P";
        std::vector<std::uint8_t> save(12 + map.size() + 1);
        std::uint32_t version = 3; std::int32_t count = static_cast<std::int32_t>(map.size() + 1);
        std::memcpy(save.data(), &version, 4); std::memcpy(save.data() + 8, &count, 4);
        std::memcpy(save.data() + 12, map.c_str(), map.size() + 1);
        Check(CheckpointMatchesMap(save, "chapter2_w1_p"), "saved map identity is case insensitive");
        Check(!CheckpointMatchesMap(save, "Chapter2_W2_P"), "join rejects stale host checkpoint from another map");
        Check(!CheckpointMatchesMap(save, "Chapter2_W1"), "map prefix cannot match another checkpoint");
        save.pop_back(); Check(!CheckpointMatchesMap(save, map), "join rejects truncated checkpoint map");
        save.push_back(1); Check(!CheckpointMatchesMap(save, map), "join requires terminated archive string");
        save.back() = 0;
        count = std::numeric_limits<std::int32_t>::min(); std::memcpy(save.data() + 8, &count, 4);
        Check(!CheckpointMatchesMap(save, map), "malformed archive length cannot overflow");
        count = -static_cast<std::int32_t>(map.size() + 1);
        save.assign(12 + 2 * (map.size() + 1), 0);
        std::memcpy(save.data(), &version, 4); std::memcpy(save.data() + 8, &count, 4);
        for (std::size_t i = 0; i < map.size(); ++i) save[12 + i * 2] = map[i];
        Check(CheckpointMatchesMap(save, map), "wide archive map supported");
        save[13] = 1; Check(!CheckpointMatchesMap(save, map), "wide archive high bytes must match");
        SaveSyncRequestPayload request{}; request.transferId = 123; request.flags = SaveRequestJoinSession;
        const auto packet = MakePacket(PacketType::SaveSyncRequest, 1, 2, 3, request);
        Check(IsValidHeader(packet.header, sizeof(packet)) && request.flags != SaveRequestReplaceProfile,
            "session join has a distinct wire purpose from confirmed profile replacement");
    }

    void TestStreamingState()
    {
        using namespace AliceCoopCore;
        using namespace AliceCoopProtocol;
        HostSnapshotPayload snapshot{};
        snapshot.streamingLevelCount = MaxVisibleStreamingLevels;
        for (unsigned i = 0; i < MaxVisibleStreamingLevels; ++i)
            snapshot.streamingLevelHashes[i] = HashMapName("Chapter2_W1_" + std::to_string(i));
        Check(HasCompleteStreamingState(snapshot), "maximum streaming list is complete");
        auto packet = MakePacket(PacketType::HostSnapshot, 1, 2, 3, snapshot);
        Check(sizeof(packet) <= MaxDatagramSize && sizeof(packet) < 1200
            && IsValidHeader(packet.header, sizeof(packet)), "host streaming and maximum enemy snapshot fit safe datagram");
        Packet<HostSnapshotPayload> received{};
        std::memcpy(&received, &packet, sizeof(packet));
        Check(received.payload.streamingLevelHashes[31] == snapshot.streamingLevelHashes[31], "last streaming region survives relay copy");
        snapshot.streamingLevelsOverflow = 1;
        Check(!HasCompleteStreamingState(snapshot), "overflow cannot pretend all destination geometry was loaded");
        snapshot.streamingLevelsOverflow = 0; snapshot.streamingLevelCount = 255;
        Check(!HasCompleteStreamingState(snapshot), "corrupt streaming count cannot read outside packet");
    }

	void TestNativePawnStance()
	{
		using namespace AliceCoopCore;
		using namespace AliceCoopProtocol;
		const std::uint32_t flags = StateVisible | StateAuthoritative | StateBodyHidden | StateDodgeInvisible;
		for (std::uint8_t stance = 0; stance < 8; ++stance)
		{
			const auto packed = PackPawnStance(flags, stance);
			Check(UnpackPawnStance(packed) == stance, "all authored pawn stances survive state relay");
			Check((packed & ~PawnStanceMask) == flags, "stance preserves visibility authority and dodge flags");
			Check(UnpackPawnStance(PackPawnStance(packed, 0)) == 0, "combat exit clears previous stance");
		}
		Check(UnpackPawnStance(PackPawnStance(flags, 255)) == 0, "invalid stance uses standing branch");
	}

	void TestAimOffset()
	{
		using namespace AliceCoopCore;
		std::uint8_t packed[3]{};
		for (int i = -2000; i <= 2000; ++i)
		{
			const float x = i / 2000.0f, y = -x * 0.37f;
			PackAimOffset(x, y, packed);
			Check(std::fabs(UnpackAimOffset(packed, 0) - x) <= 0.00025f
				&& std::fabs(UnpackAimOffset(packed, 1) - y) <= 0.00025f,
				"independent signed aiming axes retain sub-degree precision");
		}
		PackAimOffset(2.0f, -2.0f, packed);
		Check(UnpackAimOffset(packed, 0) == 1.0f && UnpackAimOffset(packed, 1) == -1.0f,
			"aiming extremes saturate without wrapping");
		PackAimOffset(std::numeric_limits<float>::quiet_NaN(),
			std::numeric_limits<float>::infinity(), packed);
		Check(UnpackAimOffset(packed, 0) == 0.0f && UnpackAimOffset(packed, 1) == 0.0f,
			"invalid aiming input returns neutral pose");
		AliceCoopProtocol::HostSnapshotPayload snapshot{};
		PackAimOffset(-0.75f, 0.25f, snapshot.hostState.aimOffset);
		PackAimOffset(0.6f, -0.4f, snapshot.clientState.aimOffset);
		const auto packet = AliceCoopProtocol::MakePacket(
			AliceCoopProtocol::PacketType::HostSnapshot, 1, 2, 3, snapshot);
		Check(sizeof(packet) <= AliceCoopProtocol::MaxDatagramSize
			&& AliceCoopProtocol::IsValidHeader(packet.header, sizeof(packet)),
			"both aiming states fit complete maximum enemy snapshot");
		Check(std::fabs(UnpackAimOffset(packet.payload.hostState.aimOffset, 0) + 0.75f) < 0.00025f
			&& std::fabs(UnpackAimOffset(packet.payload.clientState.aimOffset, 1) + 0.4f) < 0.00025f,
			"host and client aiming states remain independent in relay snapshot");
	}

	void TestAnimationRootOptions()
	{
		using namespace AliceCoopCore;
		const std::uint8_t dodge[3]{2, 2, 2}, noRotation[3]{0, 0, 0};
		const auto packed = PackAnimationRootOptions(dodge, noRotation, false, false);
		Check(packed == 0x2a, "native dodge extraction survives wire encoding");
		for (int axis = 0; axis < 3; ++axis)
		{
			Check(AnimationRootAxisOption(packed, axis, false) == 2, "dodge extraction on each axis");
			Check(AnimationRootAxisOption(packed, axis, true) == 0, "dodge rotation remains authored default");
		}
		const std::uint8_t movement[3]{1, 0, 1}, rotation[3]{1, 2, 0};
		const auto mixed = PackAnimationRootOptions(movement, rotation, true, false);
		Check(mixed == 0x1251, "independent axis and zero-root flags");
		Check(AnimationRootAxisOption(mixed, 1, false) == 0, "retain authored vertical root axis");
		Check(AnimationRootAxisOption(mixed, 1, true) == 2, "retain extracted rotation axis");
		Check(AnimationRootAxisOption(0xffff, 0, false) == 0, "invalid wire root option uses default");
		Check(sizeof(AliceCoopProtocol::Packet<AliceCoopProtocol::AnimationGraphPayload>)
			<= AliceCoopProtocol::MaxDatagramSize, "root metadata keeps graph within datagram budget");
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

	void TestVisualNotifyAdvancement()
	{
		using namespace AliceCoopCore;
		Check(ClassifyVisualNotifyAdvance(true, false, false, 0.8f, 0.0f, 1.0f)
			== VisualNotifyAdvance::Restart, "new animation emits initial notifies");
		Check(ClassifyVisualNotifyAdvance(false, true, false, 0.3f, 0.0f, 1.0f)
			== VisualNotifyAdvance::Restart, "rapid repeated attack emits effects again");
		Check(ClassifyVisualNotifyAdvance(false, false, false, 0.3f, 0.1f, 1.0f)
			== VisualNotifyAdvance::Correction, "network rewind does not replay effects");
		Check(ClassifyVisualNotifyAdvance(false, false, true, 0.95f, 0.02f, 1.0f)
			== VisualNotifyAdvance::LoopWrap, "loop wrap includes previous cycle tail");
		Check(ClassifyVisualNotifyAdvance(false, false, true, 0.5f, 0.2f, 1.0f)
			== VisualNotifyAdvance::Correction, "mid-loop correction is not a wrap");
		Check(ClassifyVisualNotifyAdvance(false, false, false, 0.2f, 0.21f, 1.0f)
			== VisualNotifyAdvance::Continue, "ordinary animation progress");
	}

	void TestNativeTerminalPoseAndRelease()
	{
		using namespace AliceCoopCore;
		using namespace AliceCoopProtocol;
		AnimationGraphPayload graph{};
		graph.sequenceCount = 2;
		graph.blendCount = 1;
		const std::uint8_t extraction[3]{2, 2, 1}, rotation[3]{0, 0, 0};
		graph.sequences[0].rootBoneOptions = PackAnimationRootOptions(extraction, rotation, false, false) | AnimationPoseStopped;
		graph.sequences[0].position = 0.966667f;
		graph.sequences[0].rate = 0.0f;
		graph.sequences[0].flags = 1u << 5; // outgoing full-body child, never SlotActive
		graph.sequences[1].flags = (1u << 5) | AnimationSequenceSlotActive;
		graph.sequences[1].rate = 1.25f;
		graph.blends[0].flags = (1u << 5) | AnimationBlendNativeSlotTiming;
		graph.blends[0].activeChildIndex = 0;
		graph.blends[0].nodeIndex = 25; // 50 ms remaining native release
		graph.fullBodyBlendOutTicks = 0; // valid native hold, distinct from missing timing
		const auto packet = MakePacket(PacketType::AnimationGraph, 1, 2, 3, graph);
		std::array<std::uint8_t, sizeof(packet)> wire{};
		std::memcpy(wire.data(), &packet, wire.size());
		Packet<AnimationGraphPayload> received{};
		std::memcpy(&received, wire.data(), wire.size());
		Check(IsValidHeader(received.header, wire.size()) && wire.size() <= MaxDatagramSize,
			"terminal pose and native release retain full graph packet budget");
		const auto& pose = received.payload.sequences[0];
		Check((pose.rootBoneOptions & AnimationPoseStopped) && pose.rate == 0.0f
			&& pose.position == graph.sequences[0].position && !(pose.flags & AnimationSequenceSlotActive),
			"stopped outgoing pose survives relay independently of incoming action");
		Check(AnimationRootAxisOption(pose.rootBoneOptions, 0, false) == 2
			&& AnimationRootAxisOption(pose.rootBoneOptions, 2, false) == 1,
			"terminal pose flag preserves authored root extraction");
		Check(NativeAnimationSlot(received.payload.blends[0].flags) == 0
			&& (received.payload.blends[0].flags & AnimationBlendNativeSlotTiming)
			&& received.payload.fullBodyBlendOutTicks == 0,
			"native zero auto-release is distinguishable from absent slot timing");
		for (float targetWeight : {0.0f, 0.25f, 0.5f, 0.9f})
		{
			const float remaining = received.payload.blends[0].nodeIndex * 0.002f;
			const float requested = NativeBlendTransitionTime(remaining, targetWeight);
			Check(std::fabs(requested * (1.0f - targetWeight) - remaining) < 0.00001f,
				"native SetActiveChild preserves actual remaining release across target weights");
		}
		Check(NativeBlendTransitionTime(0.0f, 0.5f) == 0.0f, "completed native release adds no fallback transition");
		Check(NativeBlendTransitionTime(0.1f, 1.0f) == 0.0f, "already reached base pose does not reopen fade");
		Check(NativeBlendTransitionTime(-1.0f, 0.0f) == 0.0f, "stale release clamps to completion");
	}

	void TestNativeAttackHoldTiming()
	{
		using namespace AliceCoopCore;
		using namespace AliceCoopProtocol;
		// Recorded protocol39 attack A had pendingOut=-1 on TX but +0.06 on RX.
		// Exercise capture -> serialized graph -> native config duration for that input.
		for (float pending : {-1.0f, 0.0f, 0.06f, 0.3f, 0.5f})
		{
			AnimationGraphPayload graph{};
			graph.blendCount = 1;
			graph.blends[0].flags = (1u << 5) | NativeFullBodyTimingFlags(0.0f, pending);
			graph.blends[0].activeChildIndex = 2;
			graph.fullBodyBlendTicks = 0; // native instant attack entry
			graph.fullBodyBlendOutTicks = static_cast<std::uint8_t>(std::lround((std::max)(0.0f, pending) * 500.0f));
			const auto packet = MakePacket(PacketType::AnimationGraph, 1, 2, 3, graph);
			Packet<AnimationGraphPayload> received{};
			std::memcpy(&received, &packet, sizeof(packet));
			const auto flags = received.payload.blends[0].flags;
			Check(IsValidHeader(received.header, sizeof(packet)) && sizeof(packet) <= MaxDatagramSize,
				"native hold flag fits existing animation datagram");
			Check(NativeAnimationSlot(flags) == 0 && (flags & AnimationBlendNativeSlotTiming)
				&& received.payload.fullBodyBlendTicks == 0,
				"held and released clips both retain valid zero native entry timing");
			Check(std::fabs(NativePendingBlendOut(flags, received.payload.fullBodyBlendOutTicks) - pending) < 0.001f,
				"attack hold sentinel survives without becoming an automatic blend-out");
		}
		Check(NativeFullBodyTimingFlags(0.0f, -1.0f) & AnimationBlendNativeHold,
			"negative native pending-out is valid timing, not absent metadata");
		Check(!NativeFullBodyTimingFlags(0.0f, std::numeric_limits<float>::quiet_NaN()),
			"invalid native pending-out does not advertise valid timing");
	}

	void TestAnimationPhaseCorrection()
	{
		using namespace AliceCoopCore;
		Check(NativeAnimationSlot(AliceCoopProtocol::AnimationSequenceCustom) == -1,
			"unclassified custom clips do not alias a native slot");
		Check(NativeAnimationSlot(static_cast<std::uint8_t>((4u << 5)
			| AliceCoopProtocol::AnimationSequenceSlotActive | AliceCoopProtocol::AnimationSequenceLooping)) == 3,
			"native additive slot survives active and looping flags");
		Check(sizeof(AliceCoopProtocol::Header) + sizeof(AliceCoopProtocol::AnimationGraphPayload)
			<= AliceCoopProtocol::MaxDatagramSize, "native slot metadata fits one datagram");
		auto forward = CorrectAnimationPhase(0.4f, 0.44f, 1.0f, false, 1.0f);
		Check(!forward.snap && forward.rate > 1.0f && forward.rate <= 1.1f, "small phase lag changes rate continuously");
		auto wrap = CorrectAnimationPhase(0.99f, 0.01f, 1.0f, true, 1.0f);
		Check(!wrap.snap && wrap.rate > 1.0f, "loop boundary uses nearest phase");
		Check(CorrectAnimationPhase(0.1f, 0.7f, 1.0f, false, 1.0f).snap, "large phase error seeks once");
		Check(NormalizeAnimationRate(4.0f, 2.0f, 0.5f) == 4.0f, "native mesh and asset multipliers are normalized");
		Check(NormalizeAnimationRate(0.0f, 1.0f, 1.0f) == 0.0f, "source pause remains paused");
		volatile float pausedMeshScale = 0.0f;
		Check(NormalizeAnimationRate(1.0f, pausedMeshScale, 1.0f) == 0.0f, "paused native mesh does not divide by zero");
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
    using AliceCoopDisplay::SceneDisplayAspect;
    // The 3D renderer consumes this separately from the GFx menu aspect. Both
    // a menu camera and gameplay must receive the same render-mode dimensions.
    const float sceneAspect = SceneDisplayAspect(3440.0f / 1440, 1920, 1080, true);
    const float sceneWidth = 1920 / (sceneAspect / (1920.0f / 1080));
    Check(sceneWidth == 1920, "3D scene keeps full width in 16:9 exclusive fullscreen");
    Check(sceneWidth / 1080 == sceneAspect,
        "3D destination rectangle matches projection aspect without horizontal squeezing");
    Check(SceneDisplayAspect(3440.0f / 1440, 3440, 1440, true) == 3440.0f / 1440,
        "3D native ultrawide fullscreen retains its aspect");
    Check(SceneDisplayAspect(2.389f, 1920, 1080, false) == 2.389f,
        "3D windowed and borderless compensation is unchanged");
    Check(SceneDisplayAspect(2.389f, 1920, 0, true) == 2.389f,
        "3D uninitialized viewport avoids division by zero");
    using AliceCoopDisplay::MovieDisplayAspect;
    // The live renderer used desktop/render aspect to shrink 1920 to 1428 pixels.
    const float renderAspect = 1920.0f / 1080;
    const float desktopAspect = 3440.0f / 1440;
    Check(static_cast<int>(1920 / (desktopAspect / renderAspect)) == 1428,
        "observed native fullscreen letterbox regression fixture");
    const float correctedAspect = SceneDisplayAspect(desktopAspect, 1920, 1080, true);
    Check(static_cast<int>(1920 / (correctedAspect / renderAspect)) == 1920,
        "fullscreen GFx renderer keeps full width in title, pause and HUD movies");

    Check(SceneDisplayAspect(3440.0f / 1440, 1920, 1080, true) == 1920.0f / 1080,
        "fullscreen GFx movies follow 16:9 render size after 21:9 desktop");
    Check(SceneDisplayAspect(3440.0f / 1440, 3440, 1440, true) == 3440.0f / 1440,
        "native 21:9 fullscreen keeps its aspect");
    Check(SceneDisplayAspect(1.777f, 3440, 1440, false) == 1.777f,
        "windowed and borderless retain native menu compensation");
    Check(SceneDisplayAspect(2.389f, 0, 1080, true) == 2.389f,
        "uninitialized viewport retains native aspect");
    const float movieAspect = MovieDisplayAspect(desktopAspect, renderAspect);
    Check(movieAspect / renderAspect == 1.0f,
        "fullscreen 16:9 video avoids stale desktop compensation and horizontal squeezing");
    Check(MovieDisplayAspect(desktopAspect, desktopAspect) == desktopAspect,
        "native ultrawide video keeps source fitting and letterboxing");
    Check(MovieDisplayAspect(desktopAspect, 0) == desktopAspect,
        "uninitialized video viewport keeps native aspect");
	Check(TestNativeAudioResultStorage(), "native audio DWORD result preserves adjacent stack storage");
	AliceCoopDisplay::StartupMode launchMode;
	Check(launchMode.Consume(), "display preference applies on initial viewport creation");
	for (int change = 0; change < 10; ++change)
		Check(!launchMode.Consume(), "subsequent fullscreen / resolution changes remain player controlled");
	AliceCoopDisplay::StartupMode nextProcess;
	Check(nextProcess.Consume(), "a new game launch applies the selected startup mode again");
	TestProtocolHeaders();
    TestNativeSoundArgument();
    TestPersistentArchiveInvalidation();
    TestGfxMovieStreamBoundary();
    TestSessionCheckpointIdentity();
    TestStreamingState();
	TestSequenceComparison();
	TestAnimationRootOptions();
	TestAimOffset();
	TestNativePawnStance();
	TestVisualNotifyAdvancement();
	TestNativeAttackHoldTiming();
	TestAnimationPhaseCorrection();
	TestNativeTerminalPoseAndRelease();
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
