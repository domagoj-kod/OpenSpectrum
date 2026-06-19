# SPDX-License-Identifier: GPL-3.0-or-later
# OpenSpectrum Makefile - Secure Cross-Platform Compilation

# Compiler selection
CC := gcc
CXX := g++

# Build version stamped into the binary (IqLogger capture metadata) and reused
# by `make dist`. git-describe at the top level; override with `make VERSION=v3.0.1`.
# Passed as a bare token and stringized in C++ (OS_STRINGIFY) so the recursive
# CXXFLAGS overrides in the release/profile targets don't fight nested quoting.
VERSION ?= $(shell git describe --tags --always 2>/dev/null || echo dev)
VERSION_DEF := -DOPENSPECTRUM_VERSION=$(VERSION)

# SDL3 flags (detect via pkg-config, fallback to manual)
SDL3_CFLAGS := $(shell pkg-config --cflags sdl3 2>/dev/null || echo "")
SDL3_LDFLAGS := $(shell pkg-config --libs sdl3 2>/dev/null || echo "-lSDL3")

# Platform-specific configuration
ifeq ($(OS),Windows_NT)
    # Windows (MinGW/MSYS2)
    TARGET := openspectrum.exe

    BASE_CFLAGS := -g -D_FORTIFY_SOURCE=2 -fstack-protector-strong
    BASE_CXXFLAGS := -std=c++20 -Wall -Wextra -Wpedantic \
                    -Wshadow -Wconversion \
                    -Wformat=2 -Wformat-security \
                    -Wformat-nonliteral \
                    -D_FORTIFY_SOURCE=2 \
                    -fstack-protector-strong \
                    -static-libgcc -static-libstdc++ \
                    $(VERSION_DEF)

    BASE_LDFLAGS := -lrtlsdr $(SDL3_LDFLAGS) -static-libgcc -static-libstdc++

    # Embed the app icon into the .exe via windres (MinGW-only). Deferred (=) so
    # $(BUILD_DIR), defined later, expands at use time. The build rule is gated
    # on OS below; RES_OBJ is empty on every other platform.
    WINDRES ?= windres
    RES_OBJ = $(BUILD_DIR)/openspectrum_res.o
else
    # Linux and other Unix-like systems
    TARGET := openspectrum

    BASE_CFLAGS := -g -D_FORTIFY_SOURCE=2 -fstack-protector-strong -fPIE
    BASE_CXXFLAGS := -std=c++20 -Wall -Wextra -Wpedantic \
                    -Wshadow -Wconversion \
                    -Wformat=2 -Wformat-security \
                    -Wformat-nonliteral \
                    -D_FORTIFY_SOURCE=2 \
                    -fstack-protector-strong -fPIE \
                    $(VERSION_DEF)

    BASE_LDFLAGS := -lrtlsdr -lpthread -lm $(SDL3_LDFLAGS) \
                   -Wl,-z,now \
                   -Wl,-z,relro \
                   -Wl,-z,noexecstack

    # No Windows resource object off-Windows; keeps the shared link line clean.
    RES_OBJ =
endif

# Default to debug build (safe default)
.DEFAULT_GOAL := all
CFLAGS   := $(BASE_CFLAGS) -O0 -DOPENSPECTRUM_DEBUG
CXXFLAGS := $(BASE_CXXFLAGS) -O0 -DOPENSPECTRUM_DEBUG
LDFLAGS  := $(BASE_LDFLAGS)

# Header-dependency tracking. -MMD emits a .d file per object listing the headers
# it pulled in (excluding system headers); -MP adds a phony target for each header
# so deleting/renaming one doesn't wedge the build with a "no rule to make" error.
# The .d files are -included below, so editing a header rebuilds every .cpp that
# includes it. Kept OUT of CXXFLAGS on purpose: release/profile targets
# replace CXXFLAGS via recursive make, which would otherwise strip this.
DEPFLAGS := -MMD -MP

# Footprint-trim flags shared by the release target.
# - function/data-sections + --gc-sections: drop every symbol the linker can
#   prove unreachable, shrinking I-cache pressure.
# - visibility=hidden: only main() (and SDL3 hooks) need external linkage.
#   Strips export tables, lets LTO be more aggressive.
# - align-loops/functions=32: hot loops fit in one DSB fetch line.
TRIM_CFLAGS  := -ffunction-sections -fdata-sections -fvisibility=hidden \
                -fvisibility-inlines-hidden -falign-functions=32 -falign-loops=32
TRIM_LDFLAGS := -Wl,--gc-sections

# Release target overrides
release:
	$(MAKE) CFLAGS="$(BASE_CFLAGS) -O3 -DNDEBUG -flto=auto -march=haswell $(TRIM_CFLAGS)" \
	       CXXFLAGS="$(BASE_CXXFLAGS) -O3 -DNDEBUG -flto=auto -march=haswell $(TRIM_CFLAGS)" \
	       LDFLAGS="$(BASE_LDFLAGS) -flto=auto -march=haswell $(TRIM_CFLAGS) $(TRIM_LDFLAGS)" \
	       all

# Profile target overrides
profile:
	$(MAKE) CFLAGS="$(BASE_CFLAGS) -O2 -pg" \
	       CXXFLAGS="$(BASE_CXXFLAGS) -O2 -pg" \
	       LDFLAGS="$(BASE_LDFLAGS) -pg" \
	       all

# Debug target (already set as default)
debug:
	$(MAKE) all

# Directories
SRC_DIR := src
THIRD_PARTY := third_party
THIRD_PARTY_STB := third_party/stb
INCLUDE_DIR := include
BUILD_DIR := build
HARDWARE_DIR := $(SRC_DIR)/hardware
SIGNAL_DIR := $(SRC_DIR)/signal
FFT_DIR := $(SRC_DIR)/fft
VIS_DIR := $(SRC_DIR)/visualization
UTILS_DIR := $(SRC_DIR)/utils
GUI_DIR := $(SRC_DIR)/gui

# Include paths
INCLUDES := -I$(THIRD_PARTY) -I$(THIRD_PARTY)/pocketfft -I$(THIRD_PARTY_STB) -I$(HARDWARE_DIR) -I$(INCLUDE_DIR) \
            -I$(SIGNAL_DIR) -I$(FFT_DIR) -I$(VIS_DIR) -I$(UTILS_DIR) \
            -I$(GUI_DIR) -I$(INCLUDE_DIR)/openspectrum $(SDL3_CFLAGS)

# Source files
HARDWARE_SRCS := $(wildcard $(HARDWARE_DIR)/*.cpp)
SIGNAL_SRCS := $(wildcard $(SIGNAL_DIR)/*.cpp)
FFT_SRCS := $(wildcard $(FFT_DIR)/*.cpp)
VIS_SRCS := $(wildcard $(VIS_DIR)/*.cpp)
UTILS_SRCS := $(wildcard $(UTILS_DIR)/*.cpp)
GUI_SRCS := $(wildcard $(GUI_DIR)/*.cpp)
CORE_SRCS := $(wildcard $(SRC_DIR)/control_state.cpp)
MAIN_SRC := $(SRC_DIR)/main.cpp

# All source files
ALL_SRCS := $(HARDWARE_SRCS) $(SIGNAL_SRCS) $(FFT_SRCS) $(VIS_SRCS) $(UTILS_SRCS) $(GUI_SRCS) $(CORE_SRCS) $(MAIN_SRC)

# Object files
HARDWARE_OBJS := $(patsubst $(HARDWARE_DIR)/%.cpp,$(BUILD_DIR)/%.o,$(HARDWARE_SRCS))
SIGNAL_OBJS := $(patsubst $(SIGNAL_DIR)/%.cpp,$(BUILD_DIR)/%.o,$(SIGNAL_SRCS))
FFT_OBJS := $(patsubst $(FFT_DIR)/%.cpp,$(BUILD_DIR)/%.o,$(FFT_SRCS))
VIS_OBJS := $(patsubst $(VIS_DIR)/%.cpp,$(BUILD_DIR)/%.o,$(VIS_SRCS))
UTILS_OBJS := $(patsubst $(UTILS_DIR)/%.cpp,$(BUILD_DIR)/%.o,$(UTILS_SRCS))
GUI_OBJS := $(patsubst $(GUI_DIR)/%.cpp,$(BUILD_DIR)/%.o,$(GUI_SRCS))
CORE_OBJS := $(patsubst $(SRC_DIR)/%.cpp,$(BUILD_DIR)/%.o,$(CORE_SRCS))
MAIN_OBJ := $(BUILD_DIR)/main.o

# Aggregate object list, reused by the link target and the dependency include.
ALL_OBJS := $(HARDWARE_OBJS) $(SIGNAL_OBJS) $(FFT_OBJS) $(VIS_OBJS) \
            $(UTILS_OBJS) $(GUI_OBJS) $(CORE_OBJS) $(MAIN_OBJ)

# Per-object dependency files (one .d beside each .o). The leading '-' silences
# the missing-file noise on a clean build before any .d exists; `make clean`
# wipes BUILD_DIR, so they're removed alongside the objects.
DEPS := $(ALL_OBJS:.o=.d)
-include $(DEPS)

all: $(TARGET)

# Build directory
$(BUILD_DIR):
	@mkdir -p $(BUILD_DIR)

# Object file rules
$(BUILD_DIR)/%.o: $(HARDWARE_DIR)/%.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(DEPFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: $(SIGNAL_DIR)/%.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(DEPFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: $(FFT_DIR)/%.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(DEPFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: $(VIS_DIR)/%.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(DEPFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: $(UTILS_DIR)/%.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(DEPFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: $(GUI_DIR)/%.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(DEPFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(DEPFLAGS) -c $< -o $@

$(MAIN_OBJ): $(MAIN_SRC) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(DEPFLAGS) -c $< -o $@

# Final target. $(RES_OBJ) is the windres-compiled icon on Windows, empty
# elsewhere, so $^ links it only where it exists.
$(TARGET): $(ALL_OBJS) $(RES_OBJ)
	$(CXX) $^ -o $@ $(LDFLAGS)

# Windows: compile the icon resource (.rc -> COFF object). Gated on OS because
# windres ships only with MinGW; off-Windows RES_OBJ is empty and this rule is
# never read. Depends on the .ico so re-icon'ing rebuilds the resource.
ifeq ($(OS),Windows_NT)
$(RES_OBJ): packaging/openspectrum.rc packaging/openspectrum.ico | $(BUILD_DIR)
	$(WINDRES) $< -O coff -o $@
endif

# Package a release into a distributable, self-contained bundle for the current
# platform (Linux -> AppImage, Windows/MSYS2 -> zip with bundled DLLs). Output
# lands in dist/. Same scripts CI runs, so local and CI packaging stay identical.
# Override the version with: make dist VERSION=v2.5.0 (VERSION is defined at the
# top of this file so it's also compiled into the binary).
dist:
ifeq ($(OS),Windows_NT)
	bash packaging/windows-bundle.sh "$(VERSION)"
else
	bash packaging/linux-appimage.sh "$(VERSION)"
endif

# Clean
clean:
	rm -rf $(BUILD_DIR) $(TARGET)

# Phony targets
.PHONY: all clean dist release profile debug
