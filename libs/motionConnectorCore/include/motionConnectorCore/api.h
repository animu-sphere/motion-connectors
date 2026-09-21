// SPDX-License-Identifier: Apache-2.0
#pragma once

#if defined(MOTIONCONNECTORCORE_STATIC)
#define MOTIONCONNECTORCORE_API
#elif defined(_WIN32)
#if defined(MOTIONCONNECTORCORE_EXPORTS)
#define MOTIONCONNECTORCORE_API __declspec(dllexport)
#else
#define MOTIONCONNECTORCORE_API __declspec(dllimport)
#endif
#elif defined(__GNUC__) || defined(__clang__)
#define MOTIONCONNECTORCORE_API __attribute__((visibility("default")))
#else
#define MOTIONCONNECTORCORE_API
#endif