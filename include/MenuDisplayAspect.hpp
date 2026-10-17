#pragma once

namespace AliceCoopDisplay
{
    constexpr float SceneDisplayAspect(float nativeMonitorAspect, int width, int height,
        bool fullscreen)
    {
        // A fullscreen render mode can differ from the startup desktop. The scene
        // family feeds both the 3D destination rectangle and canvas projection.
        return fullscreen && width > 0 && height > 0
            ? static_cast<float>(width) / static_cast<float>(height)
            : nativeMonitorAspect;
    }

    constexpr float MovieDisplayAspect(float nativeMonitorAspect, float viewportAspect)
    {
        // Called only in the native fullscreen branch, before movie fitting.
        // Source movie dimensions and the normal letterbox fit remain native.
        return viewportAspect > 0 ? viewportAspect : nativeMonitorAspect;
    }
}
