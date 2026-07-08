// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace openspectrum {

// If the program was launched from a terminal, reattach stdout/stderr to it so
// LOG_* output is visible. On Windows the SDL_main entry point produces a
// GUI-subsystem .exe with no attached console; this reconnects to the parent
// console *only if one already exists*. It never spawns a window, so a
// double-click / Explorer launch stays a single clean window with no terminal.
// A no-op on non-Windows platforms (their stdio is already wired to the shell).
void attach_parent_console();

} // namespace openspectrum
