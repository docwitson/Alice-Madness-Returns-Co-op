# Contributing

Alice Co-op is experimental reverse-engineering and compatibility work. Small,
focused changes with reproducible test cases are preferred.

1. Open an issue describing the map, checkpoint, host/client order and exact
   observed behavior.
2. Build the `Release|x86` configuration with Visual Studio 2022.
3. Preserve protocol compatibility when practical. If a breaking protocol
   change is necessary, update `src/Coop/Protocol.hpp`, the package manifest and
   changelog together.
4. Do not commit game files, saves, crash dumps, private IP addresses, tokens,
   generated logs or proprietary assets.
5. Test with two local windows before a two-computer playtest.
6. Submit source changes under GPL-2.0 and retain upstream notices.

See [docs/DEVELOPMENT.md](docs/DEVELOPMENT.md) for build details.
