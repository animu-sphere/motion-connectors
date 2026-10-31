/* SPDX-License-Identifier: Apache-2.0 */
#ifndef MOTIONCONNECTORCABI_API_H
#define MOTIONCONNECTORCABI_API_H

#if defined(_WIN32)
#if defined(MOTIONCONNECTORCABI_EXPORTS)
#define MOTIONCONNECTORCABI_API __declspec(dllexport)
#else
#define MOTIONCONNECTORCABI_API __declspec(dllimport)
#endif
#elif defined(__GNUC__) || defined(__clang__)
#define MOTIONCONNECTORCABI_API __attribute__((visibility("default")))
#else
#define MOTIONCONNECTORCABI_API
#endif

#endif /* MOTIONCONNECTORCABI_API_H */
