# Third-party dependency

This optional component links the Khronos OpenXR loader, using the headers and
loader from [OpenXR-SDK](https://github.com/KhronosGroup/OpenXR-SDK).
Minimum SDK version: 1.1.36. Deterministic validation uses release-1.1.36.
The SDK is not vendored or downloaded by this project's CMake configuration.

OpenXR-SDK is copyright The Khronos Group Inc. and contributors, licensed
under Apache-2.0. Its bundled JsonCpp has its own MIT license; consult the
SDK's LICENSE and src/external/jsoncpp/LICENSE files when distributing it.
