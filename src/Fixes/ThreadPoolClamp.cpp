#include "Common.hpp"
#include "Features.hpp"

SafetyHookMid ThreadPoolClamp{};

static void OnThreadPoolCount(safetyhook::Context& ctx)
{
    const auto cap = static_cast<uintptr_t>(MaxPoolThreads);
    if (ctx.eax > cap)
        ctx.eax = cap;
}

void ApplyThreadPoolClamp()
{
	if (MaxPoolThreads == -1) return;

    ThreadPoolClamp = safetyhook::create_mid(GetAddress(Addr::ThreadPoolCount), OnThreadPoolCount);
}
