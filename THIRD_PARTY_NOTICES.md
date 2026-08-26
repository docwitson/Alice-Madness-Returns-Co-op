# Third-party notices

Alice Co-op includes or builds on the following projects. Copyright remains with
the respective authors. The links below are provided to make the origin and
license of each component explicit.

| Component | Use | License |
| --- | --- | --- |
| [MadnessPatch](https://github.com/Wemino/MadnessPatch) | Base injection DLL, fixes and build foundation | GPL-2.0 |
| [Dear ImGui](https://github.com/ocornut/imgui) 1.92.8 | In-game status and control overlays | MIT |
| [SafetyHook](https://github.com/cursey/safetyhook) | Runtime hooks | Boost Software License 1.0 |
| [Zydis](https://github.com/zyantific/zydis) | Disassembly support bundled with SafetyHook | MIT |
| [SDL](https://github.com/libsdl-org/SDL) 3.4.12 | Controller support inherited from MadnessPatch | zlib |
| [mINI](https://github.com/metayeti/mINI) 0.9.18 | INI parsing | MIT |

The vendored source files retain their original copyright and license headers.
Redistribution notices are also collected in [`third_party/licenses`](third_party/licenses)
and included in release archives. SDL's complete license text additionally
remains in [`include/SDL3/SDL_copying.h`](include/SDL3/SDL_copying.h).

The Unreal Engine 3 SDK declarations in `include/SDK` are inherited from the
MadnessPatch source tree and are used solely to interoperate with the installed
game. They do not contain retail game assets or an executable game build.

Release binaries depend on the Microsoft Visual C++ runtime. The Microsoft
Visual C++ Redistributable is not bundled; users obtain it separately from
Microsoft when required.
