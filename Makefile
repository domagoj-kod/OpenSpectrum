# SPDX-License-Identifier: GPL-3.0-or-later
# OpenSpectrum Makefile - Secure Cross-Platform Compilation

# Compiler selection
CC := gcc
CXX := g++

# SDL2 flags (detect via pkg-config, fallback to manual)
SDL2_CFLAGS := $(shell pkg-config --cflags sdl2 2>/dev/null || echo "")
SDL2_LDFLAGS := $(shell pkg-config --libs sdl2 2>/dev/null || echo "-lSDL2")

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

    BASE_LDFLAGS := -lrtlsdr $(SDL2_LDFLAGS) -static-libgcc -static-libstdc++
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

    BASE_LDFLAGS := -lrtlsdr -lpthread -lm $(SDL2_LDFLAGS) \
                   -Wl,-z,now \
                   -Wl,-z,relro \
                   -Wl,-z,noexecstack
endif

# Default to debug build (safe default)
.DEFAULT_GOAL := all
CFLAGS   := $(BASE_CFLAGS) -O0 -DOPENSPECTRUM_DEBUG
CXXFLAGS := $(BASE_CXXFLAGS) -O0 -DOPENSPECTRUM_DEBUG
LDFLAGS  := $(BASE_LDFLAGS)

# Release target overrides
release:
	$(MAKE) CFLAGS="$(BASE_CFLAGS) -O3 -DNDEBUG -flto -march=haswell" \
	       CXXFLAGS="$(BASE_CXXFLAGS) -O3 -DNDEBUG -flto -march=haswell" \
	       LDFLAGS="$(BASE_LDFLAGS) -flto -march=haswell" \
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
THIRD_PARTY := third_party/kissfft
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
INCLUDES := -I$(THIRD_PARTY) -I$(THIRD_PARTY_STB) -I$(HARDWARE_DIR) -I$(INCLUDE_DIR) \
            -I$(SIGNAL_DIR) -I$(FFT_DIR) -I$(VIS_DIR) -I$(UTILS_DIR) \
            -I$(GUI_DIR) -I$(INCLUDE_DIR)/openspectrum $(SDL2_CFLAGS)

# Source files
HARDWARE_SRCS := $(wildcard $(HARDWARE_DIR)/*.cpp)
SIGNAL_SRCS := $(wildcard $(SIGNAL_DIR)/*.cpp)
FFT_SRCS := $(wildcard $(FFT_DIR)/*.cpp)
VIS_SRCS := $(wildcard $(VIS_DIR)/*.cpp)
UTILS_SRCS := $(wildcard $(UTILS_DIR)/*.cpp)
GUI_SRCS := $(wildcard $(GUI_DIR)/*.cpp)
CORE_SRCS := $(wildcard $(SRC_DIR)/control_state.cpp)
KISSFFT_SRCS := $(wildcard $(THIRD_PARTY)/*.c)
MAIN_SRC := $(SRC_DIR)/main.cpp

# All source files
ALL_SRCS := $(HARDWARE_SRCS) $(SIGNAL_SRCS) $(FFT_SRCS) $(VIS_SRCS) $(UTILS_SRCS) $(GUI_SRCS) $(CORE_SRCS) $(MAIN_SRC)
ALL_C_SRCS := $(KISSFFT_SRCS)

# Object files
HARDWARE_OBJS := $(patsubst $(HARDWARE_DIR)/%.cpp,$(BUILD_DIR)/%.o,$(HARDWARE_SRCS))
SIGNAL_OBJS := $(patsubst $(SIGNAL_DIR)/%.cpp,$(BUILD_DIR)/%.o,$(SIGNAL_SRCS))
FFT_OBJS := $(patsubst $(FFT_DIR)/%.cpp,$(BUILD_DIR)/%.o,$(FFT_SRCS))
VIS_OBJS := $(patsubst $(VIS_DIR)/%.cpp,$(BUILD_DIR)/%.o,$(VIS_SRCS))
UTILS_OBJS := $(patsubst $(UTILS_DIR)/%.cpp,$(BUILD_DIR)/%.o,$(UTILS_SRCS))
GUI_OBJS := $(patsubst $(GUI_DIR)/%.cpp,$(BUILD_DIR)/%.o,$(GUI_SRCS))
CORE_OBJS := $(patsubst $(SRC_DIR)/%.cpp,$(BUILD_DIR)/%.o,$(CORE_SRCS))
KISSFFT_OBJS := $(patsubst $(THIRD_PARTY)/%.c,$(BUILD_DIR)/%.o,$(KISSFFT_SRCS))
MAIN_OBJ := $(BUILD_DIR)/main.o

all: $(TARGET)

# Build directory
$(BUILD_DIR):
	@mkdir -p $(BUILD_DIR)

# Object file rules
$(BUILD_DIR)/%.o: $(HARDWARE_DIR)/%.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

$(BUILD_DIR)/%.o: $(SIGNAL_DIR)/%.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

$(BUILD_DIR)/%.o: $(FFT_DIR)/%.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

$(BUILD_DIR)/%.o: $(VIS_DIR)/%.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

$(BUILD_DIR)/%.o: $(UTILS_DIR)/%.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

$(BUILD_DIR)/%.o: $(GUI_DIR)/%.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

$(BUILD_DIR)/%.o: $(THIRD_PARTY)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

$(MAIN_OBJ): $(MAIN_SRC) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

# Final target
$(TARGET): $(HARDWARE_OBJS) $(SIGNAL_OBJS) $(FFT_OBJS) $(VIS_OBJS) $(UTILS_OBJS) $(GUI_OBJS) $(CORE_OBJS) $(KISSFFT_OBJS) $(MAIN_OBJ)
	$(CXX) $^ -o $@ $(LDFLAGS)

# Clean
clean:
	rm -rf $(BUILD_DIR) $(TARGET)

# Phony targets
.PHONY: all clean
