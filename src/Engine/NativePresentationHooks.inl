#include "Coop/CoopClient.hpp"
#include "Coop/Detail/ProcessEventCallbacks.hpp"
#include "NativeSoundArgument.hpp"
#include <array>

namespace
{
	struct NativeScriptFramePrefix
	{
		std::uint32_t header[4];
		UFunction* node;
		UObject* object;
		const std::uint8_t* code;
		void* locals;
	};
	static_assert(offsetof(NativeScriptFramePrefix, node) == 0x10);
	static_assert(offsetof(NativeScriptFramePrefix, locals) == 0x1c);
	static_assert(offsetof(AEmitterPool_execSpawnEmitter_Params, ReturnValue) == 0x24);
	static_assert(offsetof(ADecalManager_execSpawnDecal_Params, ReturnValue) == 0x60);
	// FFrame ABI verified in this executable's ProcessInternal: Node +0x10,
	// Object +0x14, Code +0x18, Locals +0x1c. Hook the reflected script
	// dispatcher so VM-internal calls are observed as well as ProcessEvent.
	safetyhook::InlineHook g_nativeScriptDispatcher;
    safetyhook::InlineHook g_nativeAudioCreateHook;
    safetyhook::InlineHook g_nativeAudioPlayHook;
    safetyhook::InlineHook g_nativePlaySoundHook;
    UFunction* g_nativeActorPlaySound = nullptr;
    thread_local UObject* g_nativeAudioSource = nullptr;
    thread_local unsigned g_nativeSoundArgumentDepth = 0;
    thread_local USoundCue* g_nativeEvaluatedSound = nullptr;
    std::array<UFunction*, 4> g_nativeSoundChoiceFunctions{};


	UFunction* g_nativeProjectileShutdown = nullptr;
	UFunction* g_nativeProjectileExplode = nullptr;
	UFunction* g_nativeEmitterSpawn = nullptr;
	UFunction* g_aliceEmitterSpawn = nullptr;
	UFunction* g_actorEmitterSetTemplate = nullptr;
	UFunction* g_spawnableEmitterSetTemplate = nullptr;
	UFunction* g_nativeDecalSpawn = nullptr;
	thread_local UObject* g_nativeEffectSource = nullptr;
	thread_local unsigned g_nativeEmitterSpawnDepth = 0;
	thread_local unsigned g_nativeActorTemplateDepth = 0;

    USoundCue* ReadNativeSoundCue(UObject* object, NativeScriptFramePrefix* frame)
    {
        if (!frame) return nullptr;
        std::uint32_t cue = 0;
        if (!frame->code && frame->node == g_nativeActorPlaySound
            && MemoryHelper::IsReadable(frame->locals, sizeof(cue)))
            memcpy(&cue, frame->locals, sizeof(cue));
        else
        {
            if (!MemoryHelper::IsReadable(frame->code, 5)) return nullptr;
            const auto read = [](std::uint32_t address, std::uint32_t& word)
            {
                if (!MemoryHelper::IsReadable(reinterpret_cast<void*>(address), 4)) return false;
                memcpy(&word, reinterpret_cast<void*>(address), 4); return true;
            };
            const auto offset = [](std::uint32_t address, std::int32_t& value)
            {
                if (!MemoryHelper::IsReadable(reinterpret_cast<void*>(address), sizeof(UProperty))) return false;
                auto* property = reinterpret_cast<UProperty*>(address);
                auto* objects = UObject::GObjObjects();
                const auto index = property->ObjectInternalInteger;
                if (!objects || index < 0 || index >= objects->size() || objects->at(index) != property) return false;
                if (!property->IsA(UProperty::StaticClass())) return false;
                value = property->Offset; return true;
            };
            const std::size_t size = MemoryHelper::IsReadable(frame->code, 32) ? 32 : 5;
            if (!AliceCoopNative::ReadSoundArgument({frame->code, size},
                reinterpret_cast<std::uint32_t>(frame->object ? frame->object : object),
                reinterpret_cast<std::uint32_t>(frame->locals), read, offset, cue)) return nullptr;
        }
        return reinterpret_cast<USoundCue*>(cue);
    }

    void __fastcall NativeAudioCreateHook(UObject* object, int, void* frame, void* result)
    {
        g_nativeAudioCreateHook.thiscall<void>(object, frame, result);
        if (AliceCoop::IsEnabled() && result)
            AliceCoop::OnLocalNativeAudioCreated(g_nativeAudioSource, object,
                *static_cast<UAudioComponent**>(result));
    }

    void __fastcall NativePlaySoundHook(UObject* object, int, void* frame, void* result)
    {
        auto* cue = AliceCoop::IsEnabled() ? ReadNativeSoundCue(object, static_cast<NativeScriptFramePrefix*>(frame)) : nullptr;
        auto* previous = g_nativeEvaluatedSound;
        g_nativeEvaluatedSound = nullptr; ++g_nativeSoundArgumentDepth;
        g_nativePlaySoundHook.thiscall<void>(object, frame, result);
        --g_nativeSoundArgumentDepth;
        if (!cue) cue = g_nativeEvaluatedSound;
        g_nativeEvaluatedSound = previous;
        if (cue) AliceCoop::OnLocalNativeWeaponSound(g_nativeAudioSource, object, cue);
    }

    void __fastcall NativeAudioPlayHook(UObject* object, int, void* frame, void* result)
    {
        g_nativeAudioPlayHook.thiscall<void>(object, frame, result);
        if (AliceCoop::IsEnabled() && object && object->IsA(UAudioComponent::StaticClass()))
            AliceCoop::OnLocalNativeAudioCreated(g_nativeAudioSource, object->Outer,
                reinterpret_cast<UAudioComponent*>(object));
    }

	void __fastcall NativeScriptDispatcherHook(UObject* object, int,
		void* frame, void* result)
	{
		auto* scriptFrame = static_cast<NativeScriptFramePrefix*>(frame);
		UFunction* function = scriptFrame->node;
		void* locals = scriptFrame->locals;
		const bool enabled = AliceCoop::IsEnabled();
		const bool emitterSpawn = enabled && locals
			&& (function == g_nativeEmitterSpawn || function == g_aliceEmitterSpawn);
		UObject* const previousSource = g_nativeEffectSource;
        UObject* const previousAudioSource = g_nativeAudioSource;
        if (enabled && object && (object->IsA(AWeaponForAlice::StaticClass())
            || object->IsA(AAliceGameProjectile::StaticClass()) || object->IsA(AAliceClonePawn::StaticClass())))
            g_nativeAudioSource = object;
		if (enabled && object
			&& (object->IsA(AAliceGameProjectile::StaticClass())
				|| object->IsA(AHobbyHorse::StaticClass())))
			g_nativeEffectSource = object;
		const bool actorTemplate = enabled && locals
			&& (function == g_actorEmitterSetTemplate || function == g_spawnableEmitterSetTemplate);
		if (actorTemplate) ++g_nativeActorTemplateDepth;
		const bool decalSpawn = enabled && locals && function == g_nativeDecalSpawn;
		ADecalManager_execSpawnDecal_Params decal{};
		if (decalSpawn)
			memcpy(&decal, locals, offsetof(ADecalManager_execSpawnDecal_Params, ReturnValue));
		AEmitterPool_execSpawnEmitter_Params spawn{};
		if (emitterSpawn)
		{
			memcpy(&spawn, locals, offsetof(AEmitterPool_execSpawnEmitter_Params, ReturnValue));
			++g_nativeEmitterSpawnDepth;
		}
		g_nativeScriptDispatcher.thiscall<void>(object, frame, result);
        if (g_nativeSoundArgumentDepth && result
            && std::find(g_nativeSoundChoiceFunctions.begin(), g_nativeSoundChoiceFunctions.end(), function)
                != g_nativeSoundChoiceFunctions.end())
            g_nativeEvaluatedSound = *static_cast<USoundCue**>(result);
		if (emitterSpawn)
		{
			if (--g_nativeEmitterSpawnDepth == 0 && result)
				AliceCoop::OnLocalNativeEmitterSpawn(g_nativeEffectSource,
					*static_cast<UParticleSystemComponent**>(result),
					spawn.EmitterTemplate, spawn.SpawnLocation, spawn.SpawnRotation);
		}
		if (actorTemplate && --g_nativeActorTemplateDepth == 0 && object && object->IsA(AEmitter::StaticClass()))
			AliceCoop::OnLocalNativeActorEmitterSpawn(g_nativeEffectSource, reinterpret_cast<AEmitter*>(object));
		if (decalSpawn && result && *static_cast<UDecalComponent**>(result))
			AliceCoop::OnLocalNativeDecalSpawn(g_nativeEffectSource, decal);
		if (enabled && object
			&& (function == g_nativeProjectileShutdown || function == g_nativeProjectileExplode))
			AliceCoop::OnLocalWeaponProjectileEnd(
				reinterpret_cast<AAliceGameProjectile*>(object), nullptr, nullptr);
		g_nativeEffectSource = previousSource;
        g_nativeAudioSource = previousAudioSource;
	}

	void InstallNativeScriptDispatcher()
	{
		if (g_nativeScriptDispatcher || !AliceCoop::IsEnabled()) return;
		g_nativeProjectileShutdown = UFunction::FindFunction("Function AliceGame.AliceGameProjectile.ShutDown");
		g_nativeProjectileExplode = UFunction::FindFunction("Function AliceGame.AliceGameProjectile.Explode");
		g_nativeEmitterSpawn = UFunction::FindFunction("Function Engine.EmitterPool.SpawnEmitter");
		g_aliceEmitterSpawn = UFunction::FindFunction("Function AliceGame.AliceEmitterPool.SpawnEmitter");
		g_actorEmitterSetTemplate = UFunction::FindFunction("Function Engine.Emitter.SetTemplate");
		g_spawnableEmitterSetTemplate = UFunction::FindFunction("Function Engine.EmitterSpawnable.SetTemplate");
		g_nativeDecalSpawn = UFunction::FindFunction("Function Engine.DecalManager.SpawnDecal");
        auto* createAudio = UFunction::FindFunction("Function Engine.Actor.CreateAudioComponent");
        auto* playAudio = UFunction::FindFunction("Function Engine.AudioComponent.Play");
        g_nativeActorPlaySound = UFunction::FindFunction("Function Engine.Actor.PlaySound");
        // Hook only distinct verified native execs, never the common script VM.
        if (!g_nativeAudioCreateHook && createAudio && (createAudio->FunctionFlags & 0x400) && createAudio->iNative
            && createAudio->Func.Dummy && g_nativeEmitterSpawn
            && createAudio->Func.Dummy != g_nativeEmitterSpawn->Func.Dummy)
            g_nativeAudioCreateHook = HookHelper::CreateHook(reinterpret_cast<void*>(createAudio->Func.Dummy), &NativeAudioCreateHook);
        if (!g_nativePlaySoundHook && g_nativeActorPlaySound && (g_nativeActorPlaySound->FunctionFlags & 0x400) && g_nativeActorPlaySound->iNative
            && g_nativeActorPlaySound->Func.Dummy && createAudio
            && g_nativeActorPlaySound->Func.Dummy != createAudio->Func.Dummy
            && g_nativeEmitterSpawn && g_nativeActorPlaySound->Func.Dummy != g_nativeEmitterSpawn->Func.Dummy)
            g_nativePlaySoundHook = HookHelper::CreateHook(reinterpret_cast<void*>(g_nativeActorPlaySound->Func.Dummy), &NativePlaySoundHook);
        if (!g_nativeAudioPlayHook && playAudio && (playAudio->FunctionFlags & 0x400) && playAudio->iNative && playAudio->Func.Dummy
            && createAudio && playAudio->Func.Dummy != createAudio->Func.Dummy && g_nativeActorPlaySound
            && playAudio->Func.Dummy != g_nativeActorPlaySound->Func.Dummy && g_nativeEmitterSpawn
            && playAudio->Func.Dummy != g_nativeEmitterSpawn->Func.Dummy)
            g_nativeAudioPlayHook = HookHelper::CreateHook(reinterpret_cast<void*>(playAudio->Func.Dummy), &NativeAudioPlayHook);
        AliceCoop::LogProcessEventBridgeDiagnostic(std::string("NATIVEAUDIO hooks create=")
            + (g_nativeAudioCreateHook ? "yes" : "no") + ", play=" + (g_nativeAudioPlayHook ? "yes" : "no")
            + ", sound=" + (g_nativePlaySoundHook ? "yes." : "no."));
        const char* choiceNames[] = {
            "Function AliceGame.AlicePhysicalMaterialProperty.DetermineWeaponSound",
            "Function AliceGame.AlicePhysicalMaterialProperty.DetermineWeaponSoundFromWeapon",
            "Function AliceGame.AlicePhysicalMaterialProperty.DetermineWeaponSoundForHitSpinningUmbrella",
            "Function AliceGame.AlicePhysicalMaterialProperty.DetermineWeaponSoundFromWeaponForHitSpinningUmbrella"
        };
        for (std::size_t i = 0; i < g_nativeSoundChoiceFunctions.size(); ++i)
        {
            auto* fn = UFunction::FindFunction(choiceNames[i]);
            // All four selectors are script in the shipped AliceGame package.
            // Capture their actual return inside PlaySound's argument evaluation.
            g_nativeSoundChoiceFunctions[i] = fn && g_nativeEmitterSpawn
                && fn->Func.Dummy == g_nativeEmitterSpawn->Func.Dummy ? fn : nullptr;
        }

		if (!g_nativeEmitterSpawn || !g_nativeEmitterSpawn->Func.Dummy
			|| !g_aliceEmitterSpawn
			|| g_aliceEmitterSpawn->Func.Dummy != g_nativeEmitterSpawn->Func.Dummy)
		{
			AliceCoop::LogProcessEventBridgeDiagnostic("NATIVEFX script dispatcher ABI check failed.");
			return;
		}
		// Emitter.SetTemplate is native in this executable (execSetTemplate),
		// while EmitterSpawnable's override is script and calls that native base.
		// Observe the override after it completes; an optional capture path must
		// never prevent the already verified pool dispatcher from installing.
		g_actorEmitterSetTemplate = nullptr;
		if (!g_spawnableEmitterSetTemplate
			|| g_spawnableEmitterSetTemplate->Func.Dummy != g_nativeEmitterSpawn->Func.Dummy)
		{
			g_spawnableEmitterSetTemplate = nullptr;
			AliceCoop::LogProcessEventBridgeDiagnostic("NATIVEFX actor capture unavailable; pool capture retained.");
		}
		if (!g_nativeDecalSpawn
			|| g_nativeDecalSpawn->Func.Dummy != g_nativeEmitterSpawn->Func.Dummy)
		{
			g_nativeDecalSpawn = nullptr;
			AliceCoop::LogProcessEventBridgeDiagnostic("NATIVEFX decal capture unavailable; pool capture retained.");
		}
		g_nativeScriptDispatcher = HookHelper::CreateHook(
			reinterpret_cast<void*>(g_nativeEmitterSpawn->Func.Dummy), &NativeScriptDispatcherHook);
		AliceCoop::LogProcessEventBridgeDiagnostic("NATIVEFX internal script dispatcher installed.");
	}

}
