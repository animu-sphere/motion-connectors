// SPDX-License-Identifier: Apache-2.0
#pragma once

#if defined(MOTIONCONNECTORWEBSOCKET_STATIC)
#define MOTIONCONNECTORWEBSOCKET_API
#elif defined(_WIN32)
#if defined(MOTIONCONNECTORWEBSOCKET_EXPORTS)
#define MOTIONCONNECTORWEBSOCKET_API __declspec(dllexport)
#else
#define MOTIONCONNECTORWEBSOCKET_API __declspec(dllimport)
#endif
#elif defined(__GNUC__) || defined(__clang__)
#define MOTIONCONNECTORWEBSOCKET_API __attribute__((visibility("default")))
#else
#define MOTIONCONNECTORWEBSOCKET_API
#endif
