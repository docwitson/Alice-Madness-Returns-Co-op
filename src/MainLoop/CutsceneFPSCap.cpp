#include "Common.hpp"
#include "Features.hpp"

namespace CutsceneFPSCap
{
    static constexpr char  ONLY_CUTSCENE[] = "Chapter1_L1_Cin_115";
    static constexpr float WINDOW_START = 25.0f;
    static constexpr float WINDOW_END = 30.0f;
    static constexpr float CAP_FPS = 31.0f;

    static USeqAct_Interp* s_target = nullptr;
    static USeqAct_Interp* s_lastSeen = nullptr;

    void OnInterpStarted(USeqAct_Interp* mat)
    {
        if (!mat || mat == s_lastSeen)
            return;

        s_lastSeen = mat;
        if (mat->GetFullName().find(ONLY_CUTSCENE) != std::string::npos)
        {
            s_target = mat;
        }
    }

    void OnInterpFinished(USeqAct_Interp* mat)
    {
        if (mat == s_target)
        {
            s_target = nullptr;
        }
        if (mat == s_lastSeen)
        {
            s_lastSeen = nullptr;
        }
    }

    void Tick()
    {
        static bool  capActive = false, savedSmooth = false;
        static float savedMax = 0.0f;

        UAliceGameEngine* engine = g_State.AliceEngine;
        if (!engine) return;

        USeqAct_Interp* mat = s_target;
        if (mat)
        {
            bool alive = false;
            for (USeqAct_Interp* m : UObject::FindAllOf<USeqAct_Interp>())
            {
                if (m == mat)
                {
                    alive = true;
                    break;
                }
            }

            if (!alive)
            {
                s_target = nullptr;
                s_lastSeen = nullptr;
                mat = nullptr;
            }
        }
        bool inWindow = mat && mat->bIsPlaying && mat->Position >= WINDOW_START && mat->Position < WINDOW_END;

        if (inWindow && !capActive)
        {
            savedSmooth = engine->bSmoothFrameRate;
            savedMax = engine->MaxSmoothedFrameRate;

            engine->bSmoothFrameRate = true;
            engine->MaxSmoothedFrameRate = CAP_FPS;

            capActive = true;
        }
        else if (!inWindow && capActive)
        {
            engine->bSmoothFrameRate = savedSmooth;
            engine->MaxSmoothedFrameRate = savedMax;
            capActive = false;
        }
    }
}
