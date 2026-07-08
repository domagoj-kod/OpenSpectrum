// SPDX-License-Identifier: GPL-3.0-or-later

// Isolated translation unit for the app's only Win32 call. <windows.h> is kept
// OUT of every other TU on purpose: its <wingdi.h> defines `ERROR` as a macro,
// which clashes with LogLevel::ERROR and breaks LOG_ERROR anywhere both are
// seen. NOGDI drops wingdi entirely; LEAN/NOMINMAX keep the rest of the surface
// minimal (NOMINMAX guarded — MinGW's os_defines.h already defines it).

#include "win_console.h"

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOGDI
#define NOGDI
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cstdio>

namespace openspectrum {

void attach_parent_console() {
  // ATTACH_PARENT_PROCESS binds to the launching console if there is one, and
  // fails (harmlessly) when there isn't — it never creates a window. So logs
  // appear when run from PowerShell/cmd, and a double-click stays console-free.
  if (AttachConsole(ATTACH_PARENT_PROCESS)) {
    (void)std::freopen("CONOUT$", "w", stdout);
    (void)std::freopen("CONOUT$", "w", stderr);
  }
}

} // namespace openspectrum

#else

namespace openspectrum {
void attach_parent_console() {} // no-op: POSIX stdio already reaches the shell
} // namespace openspectrum

#endif
