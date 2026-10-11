#include "Coop/CoopClient.hpp"
#include "Coop/Detail/ProcessEventCallbacks.hpp"

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
