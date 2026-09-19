// SPDX-License-Identifier: Apache-2.0
#pragma once

#if defined(MOTIONCONNECTOROSC_STATIC)
#define MOTIONCONNECTOROSC_API
#elif defined(_WIN32)
#if defined(MOTIONCONNECTOROSC_EXPORTS)
#define MOTIONCONNECTOROSC_API __declspec(dllexport)
#else
#define MOTIONCONNECTOROSC_API __declspec(dllimport)
#endif
#elif defined(__GNUC__) || defined(__clang__)
#define MOTIONCONNECTOROSC_API __attribute__((visibility("default")))
#else
#define MOTIONCONNECTOROSC_API
#endif
