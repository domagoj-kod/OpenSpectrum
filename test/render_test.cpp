// SPDX-License-Identifier: MIT
// Test to verify hardware-accelerated SDL2 rendering

#include <SDL2/SDL.h>
#include <iostream>
#include <string>

int main() {
  std::cout << "=== SDL2 Hardware Acceleration Test ===" << std::endl;

  // Initialize SDL video subsystem
  if (SDL_Init(SDL_INIT_VIDEO) < 0) {
    std::cerr << "SDL_Init failed: " << SDL_GetError() << std::endl;
    return 1;
  }

  std::cout << "\nSDL Video initialized successfully" << std::endl;

  // Get display mode info
  int display_count = SDL_GetNumVideoDisplays();
  std::cout << "Number of displays: " << display_count << std::endl;

  for (int i = 0; i < display_count; ++i) {
    SDL_DisplayMode mode;
    if (SDL_GetCurrentDisplayMode(i, &mode) == 0) {
      std::cout << "  Display " << i << ": " << mode.w << "x" << mode.h << " @ "
                << mode.refresh_rate << "Hz" << std::endl;
    }
  }

  // Try to create window with OpenGL support
  std::cout << "\n--- Testing SDL Renderer ---" << std::endl;

  SDL_Window *window = SDL_CreateWindow(
      "Hardware Acceleration Test", SDL_WINDOWPOS_CENTERED,
      SDL_WINDOWPOS_CENTERED, 800, 600, SDL_WINDOW_SHOWN | SDL_WINDOW_OPENGL);

  if (!window) {
    std::cerr << "SDL_CreateWindow failed: " << SDL_GetError() << std::endl;
    SDL_Quit();
    return 1;
  }

  std::cout << "Window created successfully" << std::endl;

  // Query available render drivers
  int num_renderers = SDL_GetNumRenderDrivers();
  std::cout << "\nAvailable render drivers (" << num_renderers
            << "):" << std::endl;

  for (int i = 0; i < num_renderers; ++i) {
    SDL_RendererInfo info;
    if (SDL_GetRenderDriverInfo(i, &info) == 0) {
      std::cout << "  [" << i << "] " << info.name << " (flags: " << std::hex
                << "0x" << info.flags << std::dec << ")" << std::endl;

      if (info.flags & SDL_RENDERER_SOFTWARE) {
        std::cout << "       - SOFTWARE" << std::endl;
      }
      if (info.flags & SDL_RENDERER_ACCELERATED) {
        std::cout << "       - ACCELERATED (Hardware)" << std::endl;
      }
      if (info.flags & SDL_RENDERER_PRESENTVSYNC) {
        std::cout << "       - VSYNC supported" << std::endl;
      }
      if (info.flags & SDL_RENDERER_TARGETTEXTURE) {
        std::cout << "       - Target texture supported" << std::endl;
      }
    }
  }

  // Try to create hardware-accelerated renderer
  std::cout << "\n--- Attempting Hardware-Accelerated Renderer ---"
            << std::endl;

  SDL_Renderer *renderer =
      SDL_CreateRenderer(window, -1,
                         SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC |
                             SDL_RENDERER_TARGETTEXTURE);

  if (renderer) {
    std::cout << "Hardware-accelerated renderer created successfully!"
              << std::endl;

    // Query renderer info
    SDL_RendererInfo info;
    if (SDL_GetRendererInfo(renderer, &info) == 0) {
      std::cout << "Renderer: " << info.name << std::endl;
      std::cout << "Max texture size: " << info.max_texture_width << "x"
                << info.max_texture_height << std::endl;
      std::cout << "Flags: " << std::hex << "0x" << info.flags << std::dec
                << std::endl;

      std::cout << "\nCapabilities:" << std::endl;
      std::cout << "  Hardware accelerated: "
                << ((info.flags & SDL_RENDERER_ACCELERATED) ? "YES" : "NO")
                << std::endl;
      std::cout << "  Software fallback: "
                << ((info.flags & SDL_RENDERER_SOFTWARE) ? "YES" : "NO")
                << std::endl;
      std::cout << "  VSYNC supported: "
                << ((info.flags & SDL_RENDERER_PRESENTVSYNC) ? "YES" : "NO")
                << std::endl;
      std::cout << "  Target texture: "
                << ((info.flags & SDL_RENDERER_TARGETTEXTURE) ? "YES" : "NO")
                << std::endl;

      bool hardware_ok = (info.flags & SDL_RENDERER_ACCELERATED) != 0;
      std::cout << "\n=== RESULT: "
                << (hardware_ok ? "HARDWARE ACCELERATION ACTIVE ==="
                                : "SOFTWARE RENDERING (SLOW) ===")
                << std::endl;

      // If hardware acceleration failed, explain possible reasons
      if (!hardware_ok) {
        std::cout << "\nPossible reasons for software fallback:" << std::endl;
        std::cout << "  1. Running in WSL2 without GPU passthrough"
                  << std::endl;
        std::cout << "  2. No GPU drivers installed" << std::endl;
        std::cout << "  3. SDL2 not compiled with OpenGL/GLX support"
                  << std::endl;
        std::cout << "  4. Display server (X11/Wayland) not supporting "
                     "hardware acceleration"
                  << std::endl;
      }
    }

    SDL_DestroyRenderer(renderer);
  } else {
    std::cout << "Failed to create hardware-accelerated renderer: "
              << SDL_GetError() << std::endl;
    std::cout << "Trying software renderer as fallback..." << std::endl;

    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
    if (renderer) {
      std::cout << "Software renderer created (performance will be limited)"
                << std::endl;
      SDL_DestroyRenderer(renderer);
    } else {
      std::cerr << "Failed to create any renderer: " << SDL_GetError()
                << std::endl;
    }
  }

  // Check OpenGL support
  std::cout << "\n--- OpenGL Support Check ---" << std::endl;
  int major, minor;
  if (SDL_GL_GetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, &major) == 0 &&
      SDL_GL_GetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, &minor) == 0) {
    std::cout << "GL context version: " << major << "." << minor << std::endl;
  }

  // Check for GLX/EGL
  std::cout << "\n--- Native Graphics APIs ---" << std::endl;
  std::cout << "SDL_VIDEO_DRIVER: "
            << (SDL_getenv("SDL_VIDEODRIVER") ? SDL_getenv("SDL_VIDEODRIVER")
                                              : "(not set)")
            << std::endl;

  SDL_DestroyWindow(window);
  SDL_Quit();

  return 0;
}
