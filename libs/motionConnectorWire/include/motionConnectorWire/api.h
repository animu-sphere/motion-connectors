// SPDX-License-Identifier: Apache-2.0
#pragma once

#if defined(MOTIONCONNECTORWIRE_STATIC)
#define MOTIONCONNECTORWIRE_API
#elif defined(_WIN32)
#if defined(MOTIONCONNECTORWIRE_EXPORTS)
#define MOTIONCONNECTORWIRE_API __declspec(dllexport)
#else
#define MOTIONCONNECTORWIRE_API __declspec(dllimport)
#endif
#elif defined(__GNUC__) || defined(__clang__)
#define MOTIONCONNECTORWIRE_API __attribute__((visibility("default")))
#else
#define MOTIONCONNECTORWIRE_API
#endif
