# SPDX-License-Identifier: GPL-3.0-or-later
# OpenSpectrum Makefile - Secure Cross-Platform Compilation

# Compiler selection
CC := gcc
CXX := g++

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
                    -static-libgcc -static-libstdc++

    BASE_LDFLAGS := -lrtlsdr $(SDL3_LDFLAGS) -static-libgcc -static-libstdc++
else
    # Linux and other Unix-like systems
    TARGET := openspectrum

    BASE_CFLAGS := -g -D_FORTIFY_SOURCE=2 -fstack-protector-strong -fPIE
    BASE_CXXFLAGS := -std=c++20 -Wall -Wextra -Wpedantic \
                    -Wshadow -Wconversion \
                    -Wformat=2 -Wformat-security \
                    -Wformat-nonliteral \
                    -D_FORTIFY_SOURCE=2 \
                    -fstack-protector-strong -fPIE

    BASE_LDFLAGS := -lrtlsdr -lpthread -lm $(SDL3_LDFLAGS) \
                   -Wl,-z,now \
                   -Wl,-z,relro \
                   -Wl,-z,noexecstack
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
# includes it. Kept OUT of CXXFLAGS on purpose: release/profile/PGO targets
# replace CXXFLAGS via recursive make, which would otherwise strip this.
DEPFLAGS := -MMD -MP

# Footprint-trim flags shared by release and PGO targets.
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

# PGO data directory (absolute path; GCC writes .gcda files here at runtime)
PGO_DIR := $(CURDIR)/pgo-data

# PGO stage 1: build instrumented binary that writes .gcda profile data.
# Run the resulting binary against a representative workload, then `profile-use`.
profile-gen:
	mkdir -p $(PGO_DIR)
	$(MAKE) CFLAGS="$(BASE_CFLAGS) -O3 -DNDEBUG -march=haswell $(TRIM_CFLAGS) -fprofile-generate=$(PGO_DIR) -fprofile-update=atomic" \
	       CXXFLAGS="$(BASE_CXXFLAGS) -O3 -DNDEBUG -march=haswell $(TRIM_CFLAGS) -fprofile-generate=$(PGO_DIR) -fprofile-update=atomic" \
	       LDFLAGS="$(BASE_LDFLAGS) -march=haswell $(TRIM_CFLAGS) $(TRIM_LDFLAGS) -fprofile-generate=$(PGO_DIR)" \
	       all

# PGO stage 2: rebuild using the collected profile. LTO + PGO together let
# the linker reorder blocks/functions according to actual hot paths.
# -fprofile-correction tolerates minor source edits between gen and use.
profile-use:
	$(MAKE) CFLAGS="$(BASE_CFLAGS) -O3 -DNDEBUG -flto=auto -march=haswell $(TRIM_CFLAGS) -fprofile-use=$(PGO_DIR) -fprofile-correction" \
	       CXXFLAGS="$(BASE_CXXFLAGS) -O3 -DNDEBUG -flto=auto -march=haswell $(TRIM_CFLAGS) -fprofile-use=$(PGO_DIR) -fprofile-correction" \
	       LDFLAGS="$(BASE_LDFLAGS) -flto=auto -march=haswell $(TRIM_CFLAGS) $(TRIM_LDFLAGS) -fprofile-use=$(PGO_DIR) -fprofile-correction" \
	       all

# Wipe collected profile data (use when source has changed significantly).
profile-clean:
	rm -rf $(PGO_DIR)

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

# Final target
$(TARGET): $(ALL_OBJS)
	$(CXX) $^ -o $@ $(LDFLAGS)

# Package a release into a distributable, self-contained bundle for the current
# platform (Linux -> AppImage, Windows/MSYS2 -> zip with bundled DLLs). Output
# lands in dist/. Same scripts CI runs, so local and CI packaging stay identical.
# Override the version with: make dist VERSION=v2.5.0
VERSION ?= $(shell git describe --tags --always 2>/dev/null || echo dev)
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
.PHONY: all clean dist release profile profile-gen profile-use profile-clean debug
