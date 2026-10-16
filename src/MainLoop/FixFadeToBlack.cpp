#include "Common.hpp"
#include "Features.hpp"

namespace FixFadeToBlack
{
	constexpr float EngageAmount = 0.9999f;
	constexpr float ReleaseAmount = 0.999f;

	constexpr int MaxVolumeWalk = 512;

	static const FVector Zero{ 0.0f, 0.0f, 0.0f };

	// Zero the scene through the grading path
	static void Crush(FPostProcessSettings& pps)
	{
		pps.bOverride_EnableBloom = 1;
		pps.bEnableBloom = 0;
		pps.bOverride_Bloom_Scale = 1;
		pps.Bloom_Scale = 0.0f;
		pps.bOverride_EnableSceneEffect = 1;
		pps.bEnableSceneEffect = 1;
		pps.bOverride_Scene_HighLights = 1;
		pps.Scene_HighLights = FVector{ 10000.0f, 10000.0f, 10000.0f };
		pps.bOverride_Scene_MidTones = 1;
		pps.Scene_MidTones = FVector{ 8.0f, 8.0f, 8.0f };
		pps.bOverride_Scene_Shadows = 1;
		pps.Scene_Shadows = FVector{ 1.0f, 1.0f, 1.0f };
	}

	struct SavedPP
	{
		UObject* Owner;
		int32_t OwnerIndex;
		FPostProcessSettings* Settings;
		FPostProcessSettings Backup;
	};

	struct SavedAlpha
	{
		UObject* Owner;
		int32_t OwnerIndex;
		float* Alpha;
		float Backup;
	};

	struct SavedVolume
	{
		APostProcessVolume* Volume;
		int32_t Index;
	};

	static std::vector<SavedPP> g_savedSettings;
	static std::vector<SavedAlpha> g_savedAlphas;
	static std::vector<SavedVolume> g_sweptVolumes;
	static bool g_volumeListTrusted = false;

	static bool g_engaged = false;
	static ACamera* g_engagedCam = nullptr;
	static int32_t g_engagedCamIndex = -1;
	static uint32_t g_savedScaling = 0;
	static uint32_t g_savedInterp = 0;
	static FVector g_savedScale{};
	static FVector g_savedDesired{};
	static FVector g_savedOriginal{};

	static bool StillAlive(const UObject* owner, int32_t index)
	{
		TArray<UObject*>* objects = UObject::GObjObjects();

		if (!owner || !objects || !objects->IsValidIndex(index))
		{
			return false;
		}

		return objects->at(index) == owner;
	}

	// Crush a settings block, saving its original once per engagement
	static void SaveAndCrush(UObject* owner, FPostProcessSettings* pps)
	{
		if (!owner || !pps)
		{
			return;
		}

		for (const SavedPP& entry : g_savedSettings)
		{
			if (entry.Settings == pps)
			{
				Crush(*pps);
				return;
			}
		}

		g_savedSettings.push_back({ owner, owner->ObjectInternalInteger, pps, *pps });
		Crush(*pps);
	}

	// Force an override alpha to full, saving its original once per engagement
	static void SaveAndForceAlpha(UObject* owner, float* alpha)
	{
		if (!owner || !alpha)
		{
			return;
		}

		for (const SavedAlpha& entry : g_savedAlphas)
		{
			if (entry.Alpha == alpha)
			{
				*alpha = 1.0f;
				return;
			}
		}

		g_savedAlphas.push_back({ owner, owner->ObjectInternalInteger, alpha, *alpha });
		*alpha = 1.0f;
	}

	// Every volume in the world hangs off WorldInfo, sweeping GObjects for them costs ms a frame
	static int CrushVolumeList(AWorldInfo* worldInfo)
	{
		int walked = 0;

		for (APostProcessVolume* volume = worldInfo->HighestPriorityPostProcessVolume;
			volume && walked < MaxVolumeWalk;
			volume = volume->NextLowerPriorityVolume)
		{
			SaveAndCrush(volume, &volume->Settings);
			++walked;
		}

		return walked;
	}

	// Fallback for a world that does not keep that list
	static void SweepVolumes()
	{
		g_sweptVolumes.clear();

		for (APostProcessVolume* volume : UObject::FindAllOf<APostProcessVolume>(true))
		{
			if (volume)
			{
				g_sweptVolumes.push_back({ volume, volume->ObjectInternalInteger });
				SaveAndCrush(volume, &volume->Settings);
			}
		}
	}

	// Matinee can drive volume settings mid fade, so re-crush rather than sweep again
	static void RecrushSweptVolumes()
	{
		for (const SavedVolume& entry : g_sweptVolumes)
		{
			if (StillAlive(entry.Volume, entry.Index))
			{
				SaveAndCrush(entry.Volume, &entry.Volume->Settings);
			}
		}
	}

	// An empty list is only suspect until a level proves the engine keeps one
	static void CrushVolumes(AWorldInfo* worldInfo, bool firstFrame)
	{
		if (CrushVolumeList(worldInfo) > 0)
		{
			g_volumeListTrusted = true;
			return;
		}

		if (g_volumeListTrusted)
		{
			return;
		}

		if (firstFrame)
		{
			SweepVolumes();
		}
		else
		{
			RecrushSweptVolumes();
		}
	}

	// Only the camera actor being looked through can contribute grading to the view
	static void CrushCameraActor(AActor* target)
	{
		if (!target || !target->IsA<ACameraActor>())
		{
			return;
		}

		ACameraActor* cameraActor = static_cast<ACameraActor*>(target);
		SaveAndCrush(cameraActor, &cameraActor->CamOverridePostProcess);
		SaveAndForceAlpha(cameraActor, &cameraActor->CamOverridePostProcessAlpha);
	}

	// Every place the view grading can be sourced from
	static void CrushEverySource(AAlicePlayerController* pc, ACamera* cam, bool firstFrame)
	{
		if (pc->WorldInfo)
		{
			SaveAndCrush(pc->WorldInfo, &pc->WorldInfo->DefaultPostProcessSettings);
			CrushVolumes(pc->WorldInfo, firstFrame);
		}

		CrushCameraActor(cam->ViewTarget.Target);
		CrushCameraActor(cam->PendingViewTarget.Target);
		CrushCameraActor(cam->AnimCameraActor);

		// The engine rebuilds these two from the view target every frame
		SaveAndCrush(cam, &cam->CamPostProcessSettings);
		SaveAndForceAlpha(cam, &cam->CamOverridePostProcessAlpha);
	}

	// Only restore into objects that still exist, streamed out levels take their actors with them
	static void RestoreEverything()
	{
		for (const SavedPP& entry : g_savedSettings)
		{
			if (StillAlive(entry.Owner, entry.OwnerIndex))
			{
				*entry.Settings = entry.Backup;
			}
		}
		g_savedSettings.clear();

		for (const SavedAlpha& entry : g_savedAlphas)
		{
			if (StillAlive(entry.Owner, entry.OwnerIndex))
			{
				*entry.Alpha = entry.Backup;
			}
		}
		g_savedAlphas.clear();

		g_sweptVolumes.clear();
	}

	// Restores the camera the engagement started on, active or not
	static void Release()
	{
		g_engaged = false;

		if (StillAlive(g_engagedCam, g_engagedCamIndex))
		{
			g_engagedCam->bEnableColorScaling = g_savedScaling;
			g_engagedCam->bEnableColorScaleInterp = g_savedInterp;
			g_engagedCam->ColorScale = g_savedScale;
			g_engagedCam->DesiredColorScale = g_savedDesired;
			g_engagedCam->OriginalColorScale = g_savedOriginal;
		}

		g_engagedCam = nullptr;
		g_engagedCamIndex = -1;
		RestoreEverything();
	}

	void Tick()
	{
		AAlicePlayerController* pc = g_State.AlicePlayerController;

		if (!pc || !pc->PlayerCamera)
		{
			// The controller can go away mid fade, drop the saved state with it
			if (g_engaged)
			{
				Release();
			}
			return;
		}

		ACamera* cam = pc->PlayerCamera;

		// Only step in for fades to black
		bool blackFade = cam->FadeColor.R < 32 && cam->FadeColor.G < 32 && cam->FadeColor.B < 32;
		bool wantFull = cam->bEnableFading && blackFade && cam->FadeAmount >= (g_engaged ? ReleaseAmount : EngageAmount);

		if (wantFull)
		{
			if (g_engaged && cam != g_engagedCam)
			{
				Release();
			}

			bool firstFrame = !g_engaged;

			if (firstFrame)
			{
				g_engaged = true;
				g_engagedCam = cam;
				g_engagedCamIndex = cam->ObjectInternalInteger;
				g_savedScaling = cam->bEnableColorScaling;
				g_savedInterp = cam->bEnableColorScaleInterp;
				g_savedScale = cam->ColorScale;
				g_savedDesired = cam->DesiredColorScale;
				g_savedOriginal = cam->OriginalColorScale;
			}

			cam->bEnableColorScaling = 1;
			cam->bEnableColorScaleInterp = 0;
			cam->ColorScale = Zero;
			cam->DesiredColorScale = Zero;
			cam->OriginalColorScale = Zero;

			CrushEverySource(pc, cam, firstFrame);
		}
		else if (g_engaged)
		{
			Release();
		}
	}
}
