// SPDX-License-Identifier: Apache-2.0
#pragma once

#if defined(MOTIONCONNECTORVRCHATOSC_STATIC)
#define MOTIONCONNECTORVRCHATOSC_API
#elif defined(_WIN32)
#if defined(MOTIONCONNECTORVRCHATOSC_EXPORTS)
#define MOTIONCONNECTORVRCHATOSC_API __declspec(dllexport)
#else
#define MOTIONCONNECTORVRCHATOSC_API __declspec(dllimport)
#endif
#elif defined(__GNUC__) || defined(__clang__)
#define MOTIONCONNECTORVRCHATOSC_API __attribute__((visibility("default")))
#else
#define MOTIONCONNECTORVRCHATOSC_API
#endif
