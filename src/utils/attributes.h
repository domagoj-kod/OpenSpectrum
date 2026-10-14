// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Compiler hints for code layout and section assignment.
// OS_HOT clusters per-frame functions on contiguous I-cache lines and protects
// them from --gc-sections elimination. OS_COLD moves rare init / error paths
// into a separate text section that doesn't compete for the hot working set.
// Both are dropped silently on compilers that don't support them.
#if defined(__GNUC__) || defined(__clang__)
#define OS_HOT __attribute__((hot))
#define OS_COLD __attribute__((cold))
#else
#define OS_HOT
#define OS_COLD
#endif
