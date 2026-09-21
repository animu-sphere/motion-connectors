// SPDX-License-Identifier: Apache-2.0
#pragma once

#if defined(MOTIONCONNECTORMOCOPI_STATIC)
#define MOTIONCONNECTORMOCOPI_API
#elif defined(_WIN32)
#if defined(MOTIONCONNECTORMOCOPI_EXPORTS)
#define MOTIONCONNECTORMOCOPI_API __declspec(dllexport)
#else
#define MOTIONCONNECTORMOCOPI_API __declspec(dllimport)
#endif
#elif defined(__GNUC__) || defined(__clang__)
#define MOTIONCONNECTORMOCOPI_API __attribute__((visibility("default")))
#else
#define MOTIONCONNECTORMOCOPI_API
#endif
