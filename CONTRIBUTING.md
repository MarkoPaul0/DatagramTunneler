# Contributing to DatagramTunneler

Thanks for helping improve DatagramTunneler. Small, focused contributions are
easiest to review and release.

## Before you start

- Search existing issues and pull requests before opening a new one.
- Use an issue to discuss larger changes, new public commands, protocol
  changes, or cross-platform behaviour before implementing them.
- Keep direct command-line workflows backwards compatible unless the change is
  explicitly versioned and documented.

## Development setup

DatagramTunneler requires a C++20-capable compiler and CMake 3.16 or newer.

```sh
cmake -S . -B build-cmake -DBUILD_TESTING=ON
cmake --build build-cmake
ctest --test-dir build-cmake --output-on-failure
```

The test suite covers protocol framing, command and configuration parsing, and
a loopback multicast round trip when Python 3 is available.

## Pull requests

- Branch from the current `master` branch.
- Keep each pull request limited to one coherent change.
- Add or update tests for behaviour changes.
- Update the README or other user documentation when commands, configuration,
  packaging, or supported platforms change.
- Describe the motivation, implementation, validation performed, and any
  platform-specific considerations in the pull request.

## Code and review expectations

- Follow the existing C++20 and CMake conventions in the surrounding code.
- Prefer clear ownership, RAII, explicit error handling, and small focused
  changes over broad rewrites.
- Do not include generated build directories or release artifacts in commits.
- Be respectful and constructive in all project interactions; the
  [Code of Conduct](CODE_OF_CONDUCT.md) applies to all contributors.

## Reporting security issues

Do not open public issues for suspected vulnerabilities. Follow the
[security policy](SECURITY.md) instead.
