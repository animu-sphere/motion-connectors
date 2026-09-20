// SPDX-License-Identifier: Apache-2.0
#pragma once

#if defined(MOTIONCONNECTORTRACKING_STATIC)
#define MOTIONCONNECTORTRACKING_API
#elif defined(_WIN32)
#if defined(MOTIONCONNECTORTRACKING_EXPORTS)
#define MOTIONCONNECTORTRACKING_API __declspec(dllexport)
#else
#define MOTIONCONNECTORTRACKING_API __declspec(dllimport)
#endif
#elif defined(__GNUC__) || defined(__clang__)
#define MOTIONCONNECTORTRACKING_API __attribute__((visibility("default")))
#else
#define MOTIONCONNECTORTRACKING_API
#endif
