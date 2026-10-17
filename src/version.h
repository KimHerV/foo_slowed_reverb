// SPDX-License-Identifier: MIT
// Copyright (c) 2026 KimHerV

#pragma once
// Single source of the version number: used by the component info (main.cpp) and the DLL version resource.

#define SR_VERSION_MAJOR 1
#define SR_VERSION_MINOR 0
#define SR_VERSION_PATCH 0

#define SR_STRINGIFY2(x) #x
#define SR_STRINGIFY(x) SR_STRINGIFY2(x)
#define SR_VERSION_STR SR_STRINGIFY(SR_VERSION_MAJOR) "." SR_STRINGIFY(SR_VERSION_MINOR) "." SR_STRINGIFY(SR_VERSION_PATCH)

#define SR_AUTHOR "KimHerV"
