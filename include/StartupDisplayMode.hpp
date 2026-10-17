#pragma once

namespace AliceCoopDisplay
{
    // A launch preference is not a permanent override of the in-game settings.
    class StartupMode
    {
        bool pending = true;
    public:
        bool Consume()
        {
            const bool apply = pending;
            pending = false;
            return apply;
        }
    };
}
