#include "Common.hpp"
#include "Features.hpp"

namespace FixFadeToBlack
{
	constexpr float EngageAmount = 0.999f;
	constexpr float ReleaseAmount = 0.995f;

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

	static std::vector<std::pair<FPostProcessSettings*, FPostProcessSettings>> g_savedSettings;
	static std::vector<std::pair<float*, float>> g_savedAlphas;

	// Crush a settings block, saving its original once per engagement
	static void SaveAndCrush(FPostProcessSettings* pps)
	{
		for (auto& entry : g_savedSettings)
		{
			if (entry.first == pps)
			{
				Crush(*pps);
				return;
			}
		}

		g_savedSettings.push_back({ pps, *pps });
		Crush(*pps);
	}

	// Force an override alpha to full, saving its original once per engagement
	static void SaveAndForceAlpha(float* alpha)
	{
		for (auto& entry : g_savedAlphas)
		{
			if (entry.first == alpha)
			{
				*alpha = 1.0f;
				return;
			}
		}

		g_savedAlphas.push_back({ alpha, *alpha });
		*alpha = 1.0f;
	}

	// Every place the view grading can be sourced from
	static void CrushEverySource(AAlicePlayerController* pc, ACamera* cam)
	{
		if (pc->WorldInfo)
		{
			SaveAndCrush(&pc->WorldInfo->DefaultPostProcessSettings);
		}

		for (APostProcessVolume* vol : UObject::FindAllOf<APostProcessVolume>(true))
		{
			if (vol)
			{
				SaveAndCrush(&vol->Settings);
			}
		}

		for (ACameraActor* ca : UObject::FindAllOf<ACameraActor>(true))
		{
			if (ca)
			{
				SaveAndCrush(&ca->CamOverridePostProcess);
				SaveAndForceAlpha(&ca->CamOverridePostProcessAlpha);
			}
		}

		SaveAndCrush(&cam->CamPostProcessSettings);
		SaveAndForceAlpha(&cam->CamOverridePostProcessAlpha);
	}

	// Only restore into objects that still exist, streamed out levels take their actors with them
	static void RestoreEverything()
	{
		std::vector<FPostProcessSettings*> alive;

		for (APostProcessVolume* vol : UObject::FindAllOf<APostProcessVolume>(true))
		{
			if (vol)
			{
				alive.push_back(&vol->Settings);
			}
		}

		for (ACameraActor* ca : UObject::FindAllOf<ACameraActor>(true))
		{
			if (ca)
			{
				alive.push_back(&ca->CamOverridePostProcess);
			}
		}

		AAlicePlayerController* pc = g_State.AlicePlayerController;
		if (pc && pc->WorldInfo)
		{
			alive.push_back(&pc->WorldInfo->DefaultPostProcessSettings);
		}

		if (pc && pc->PlayerCamera)
		{
			alive.push_back(&pc->PlayerCamera->CamPostProcessSettings);
		}

		for (auto& entry : g_savedSettings)
		{
			for (FPostProcessSettings* p : alive)
			{
				if (p == entry.first)
				{
					*entry.first = entry.second;
					break;
				}
			}
		}
		g_savedSettings.clear();

		for (auto& entry : g_savedAlphas)
		{
			FPostProcessSettings* owner = reinterpret_cast<FPostProcessSettings*>(entry.first + 1);
			for (FPostProcessSettings* p : alive)
			{
				if (p == owner)
				{
					*entry.first = entry.second;
					break;
				}
			}
		}

		g_savedAlphas.clear();
	}

	void Tick()
	{
		AAlicePlayerController* pc = g_State.AlicePlayerController;
		if (!pc || !pc->PlayerCamera)
			return;

		ACamera* cam = pc->PlayerCamera;

		static bool engaged = false;
		static uint32_t savedScaling = 0;
		static FVector savedScale{};
		static FVector savedDesired{};

		// Only step in for fades to black
		bool blackFade = cam->FadeColor.R < 32 && cam->FadeColor.G < 32 && cam->FadeColor.B < 32;
		bool wantFull = cam->bEnableFading && blackFade && cam->FadeAmount >= (engaged ? ReleaseAmount : EngageAmount);

		if (wantFull)
		{
			if (!engaged)
			{
				engaged = true;
				savedScaling = cam->bEnableColorScaling;
				savedScale = cam->ColorScale;
				savedDesired = cam->DesiredColorScale;
			}

			cam->bEnableColorScaling = 1;
			cam->ColorScale = FVector{ 0.0f, 0.0f, 0.0f };
			cam->DesiredColorScale = FVector{ 0.0f, 0.0f, 0.0f };

			CrushEverySource(pc, cam);
		}
		else if (engaged)
		{
			engaged = false;
			cam->bEnableColorScaling = savedScaling;
			cam->ColorScale = savedScale;
			cam->DesiredColorScale = savedDesired;
			RestoreEverything();
		}
	}
}
