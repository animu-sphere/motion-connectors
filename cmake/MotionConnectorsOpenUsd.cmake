# SPDX-License-Identifier: Apache-2.0
#
# MotionConnectorsOpenUsd.cmake -- the workspace's OpenUSD pin, enforced in one place.
#
# OpenUSD is 26.08 and nothing else (docs/architecture/DEPENDENCIES.md §1).
# No connector opens a stage, but every connector links usd-motion-plugins'
# motionCore, which is built against one OpenUSD release, and
# usd-avatar-runtime composes every package into one OpenUSD process -- so
# this is the release the rest of the ecosystem pins (usd-motion-plugins'
# cmake/UsdMotionOpenUsd.cmake).
#
# Every entry point that resolves OpenUSD includes this immediately after its
# `find_package(pxr ...)`: the root project, and each connector built
# standalone. A component built standalone by `ost` never composes the root
# project, so the pin travels with the find_package call, not with the root.
#
# WHY NOT `find_package(pxr 26.08 EXACT ...)`: OpenUSD installs no
# pxrConfigVersion.cmake, so any version argument makes find_package fail with
# "no config version file" whichever OpenUSD is present. pxrConfig.cmake does
# set the version variables directly, and those are what this tests.
#
# There is no OpenExec probe, and there never is one: no connector evaluates
# anything (docs/architecture/DEPENDENCIES.md §5).
#
# Sets, for callers that report build metadata:
#   MOTIONCONNECTORS_OPENUSD_RELEASE   - "26.08"
include_guard(GLOBAL)

# The single supported point. `PXR_VERSION` is OpenUSD's own packed form: 2608
# is 26.08. The display form is built from MINOR/PATCH because OpenUSD's
# PXR_MAJOR_VERSION is 0 -- "0.26.8", the value pxrConfig.cmake publishes, is
# not what anyone calls this release.
set(MOTIONCONNECTORS_OPENUSD_REQUIRED_PXR_VERSION 2608)
set(MOTIONCONNECTORS_OPENUSD_REQUIRED_RELEASE "26.08")

if(NOT pxr_FOUND)
    message(FATAL_ERROR
        "MotionConnectorsOpenUsd.cmake was included before OpenUSD was resolved. "
        "Include it after find_package(pxr REQUIRED CONFIG).")
endif()

if(NOT DEFINED PXR_VERSION)
    message(FATAL_ERROR
        "This OpenUSD install publishes no PXR_VERSION, so its version cannot "
        "be verified. motion-connectors requires OpenUSD "
        "${MOTIONCONNECTORS_OPENUSD_REQUIRED_RELEASE} exactly.\n"
        "  pxrConfig.cmake: ${pxr_DIR}")
endif()

if(DEFINED PXR_MINOR_VERSION AND DEFINED PXR_PATCH_VERSION)
    # 26 + 8 -> "26.08"; OpenUSD zero-pads the month in every name it uses.
    string(REGEX REPLACE "^([0-9])$" "0\\1" _motionconnectors_patch "${PXR_PATCH_VERSION}")
    set(MOTIONCONNECTORS_OPENUSD_RELEASE "${PXR_MINOR_VERSION}.${_motionconnectors_patch}")
    unset(_motionconnectors_patch)
else()
    set(MOTIONCONNECTORS_OPENUSD_RELEASE "${PXR_VERSION}")
endif()

if(NOT PXR_VERSION EQUAL MOTIONCONNECTORS_OPENUSD_REQUIRED_PXR_VERSION)
    message(FATAL_ERROR
        "Unsupported OpenUSD: found ${MOTIONCONNECTORS_OPENUSD_RELEASE} "
        "(PXR_VERSION ${PXR_VERSION}), require "
        "${MOTIONCONNECTORS_OPENUSD_REQUIRED_RELEASE} "
        "(PXR_VERSION ${MOTIONCONNECTORS_OPENUSD_REQUIRED_PXR_VERSION}) exactly.\n"
        "  pxrConfig.cmake: ${pxr_DIR}\n"
        "OpenUSD guarantees no ABI stability across releases, so a plugin "
        "built against another release could not be loaded beside the rest "
        "of the ecosystem. See docs/architecture/DEPENDENCIES.md.")
endif()

# include_guard(GLOBAL) makes this the only time the line is printed per
# configure, however many members include the module.
message(STATUS
    "OpenUSD ${MOTIONCONNECTORS_OPENUSD_RELEASE} (PXR_VERSION ${PXR_VERSION})")
