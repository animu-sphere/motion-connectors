// SPDX-License-Identifier: Apache-2.0
#pragma once

#if defined(MOTIONCONNECTORVMC_STATIC)
#define MOTIONCONNECTORVMC_API
#elif defined(_WIN32)
#if defined(MOTIONCONNECTORVMC_EXPORTS)
#define MOTIONCONNECTORVMC_API __declspec(dllexport)
#else
#define MOTIONCONNECTORVMC_API __declspec(dllimport)
#endif
#elif defined(__GNUC__) || defined(__clang__)
#define MOTIONCONNECTORVMC_API __attribute__((visibility("default")))
#else
#define MOTIONCONNECTORVMC_API
#endif
