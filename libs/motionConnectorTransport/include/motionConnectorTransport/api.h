// SPDX-License-Identifier: Apache-2.0
#pragma once

#if defined(MOTIONCONNECTORTRANSPORT_STATIC)
#define MOTIONCONNECTORTRANSPORT_API
#elif defined(_WIN32)
#if defined(MOTIONCONNECTORTRANSPORT_EXPORTS)
#define MOTIONCONNECTORTRANSPORT_API __declspec(dllexport)
#else
#define MOTIONCONNECTORTRANSPORT_API __declspec(dllimport)
#endif
#elif defined(__GNUC__) || defined(__clang__)
#define MOTIONCONNECTORTRANSPORT_API __attribute__((visibility("default")))
#else
#define MOTIONCONNECTORTRANSPORT_API
#endif
