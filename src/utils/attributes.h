// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// OS_HOT clusters per-frame functions and protects them from --gc-sections.
// OS_COLD moves one-shot and error paths out of the hot working set. Both are
// dropped on compilers without support.
#if defined(__GNUC__) || defined(__clang__)
#define OS_HOT __attribute__((hot))
#define OS_COLD __attribute__((cold))
#else
#define OS_HOT
#define OS_COLD
#endif
