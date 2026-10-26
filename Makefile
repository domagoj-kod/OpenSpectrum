# SPDX-License-Identifier: GPL-3.0-or-later
# OpenSpectrum Makefile - Secure Cross-Platform Compilation

CC := gcc
CXX := g++

# Stamped into capture metadata and used by `make dist`. A bare token,
# stringized in C++ (OS_STRINGIFY): quotes would not survive the recursive make.
VERSION ?= $(shell git describe --tags --always 2>/dev/null || echo dev)
VERSION_DEF := -DOPENSPECTRUM_VERSION=$(VERSION)

SDL3_CFLAGS := $(shell pkg-config --cflags sdl3 2>/dev/null || echo "")
SDL3_LDFLAGS := $(shell pkg-config --libs sdl3 2>/dev/null || echo "-lSDL3")
# SDL adds -mwindows on Windows: a GUI .exe, whose --help and log go nowhere.
SDL3_LDFLAGS := $(filter-out -mwindows,$(SDL3_LDFLAGS))

ifeq ($(OS),Windows_NT)
    # Windows (MinGW/MSYS2)
    TARGET := openspectrum.exe

    BASE_CFLAGS := -g -D_FORTIFY_SOURCE=2 -fstack-protector-strong
    BASE_CXXFLAGS := -std=c++20 -Wall -Wextra -Wpedantic \
                    -Wshadow -Wconversion \
                    -Wformat=2 \
                    -D_FORTIFY_SOURCE=2 \
                    -fstack-protector-strong \
                    -static-libgcc -static-libstdc++ \
                    $(VERSION_DEF)

    BASE_LDFLAGS := -lrtlsdr $(SDL3_LDFLAGS) -static-libgcc -static-libstdc++

    # The app icon. Deferred (=): BUILD_DIR is defined further down.
    WINDRES ?= windres
    RES_OBJ = $(BUILD_DIR)/openspectrum_res.o
else
    TARGET := openspectrum

    BASE_CFLAGS := -g -D_FORTIFY_SOURCE=2 -fstack-protector-strong -fPIE
    BASE_CXXFLAGS := -std=c++20 -Wall -Wextra -Wpedantic \
                    -Wshadow -Wconversion \
                    -Wformat=2 \
                    -D_FORTIFY_SOURCE=2 \
                    -fstack-protector-strong -fPIE \
                    $(VERSION_DEF)

    BASE_LDFLAGS := -lrtlsdr -lpthread -lm $(SDL3_LDFLAGS) \
                   -Wl,-z,now \
                   -Wl,-z,relro \
                   -Wl,-z,noexecstack

    RES_OBJ =
endif

# USRP support, opt-in: keeps UHD and Boost out of the default build.
ifeq ($(UHD),1)
    BASE_CXXFLAGS += -DOPENSPECTRUM_UHD
    BASE_LDFLAGS += -luhd
    # UHD reports errors by throwing. Windows libuhd.dll uses the shared GCC
    # runtime, and an exception cannot cross into an .exe that linked its own
    # static copy.
    STATIC_RUNTIME := -static-libgcc -static-libstdc++
    BASE_CXXFLAGS := $(filter-out $(STATIC_RUNTIME),$(BASE_CXXFLAGS))
    BASE_LDFLAGS := $(filter-out $(STATIC_RUNTIME),$(BASE_LDFLAGS))
endif

# Clang as a second compiler, opt-in; GCC stays primary.
ifeq ($(CLANG),1)
    CXX := clang++
    LTO := -flto=thin
    BASE_LDFLAGS += -fuse-ld=lld
else
    LTO := -flto=auto
endif

.DEFAULT_GOAL := all
CFLAGS   := $(BASE_CFLAGS) -O0 -DOPENSPECTRUM_DEBUG
CXXFLAGS := $(BASE_CXXFLAGS) -O0 -DOPENSPECTRUM_DEBUG
LDFLAGS  := $(BASE_LDFLAGS)

# Not in CXXFLAGS: release/profile replace CXXFLAGS and would drop it.
DEPFLAGS := -MMD -MP

# Release footprint trim; 32-byte alignment keeps hot loops in one DSB line.
TRIM_CFLAGS  := -ffunction-sections -fdata-sections -fvisibility=hidden \
                -fvisibility-inlines-hidden -falign-functions=32 -falign-loops=32
TRIM_LDFLAGS := -Wl,--gc-sections

release:
	$(MAKE) CONFIG=release \
	       CFLAGS="$(BASE_CFLAGS) -O3 -DNDEBUG $(LTO) -march=haswell $(TRIM_CFLAGS)" \
	       CXXFLAGS="$(BASE_CXXFLAGS) -O3 -DNDEBUG $(LTO) -march=haswell $(TRIM_CFLAGS)" \
	       LDFLAGS="$(BASE_LDFLAGS) -s $(LTO) -march=haswell $(TRIM_CFLAGS) $(TRIM_LDFLAGS)" \
	       all

profile:
	$(MAKE) CONFIG=profile \
	       CFLAGS="$(BASE_CFLAGS) -O2 -pg" \
	       CXXFLAGS="$(BASE_CXXFLAGS) -O2 -pg" \
	       LDFLAGS="$(BASE_LDFLAGS) -pg" \
	       all

debug:
	$(MAKE) all

SRC_DIR := src
THIRD_PARTY := third_party
THIRD_PARTY_STB := third_party/stb
# One object dir per config, UHD=1 and CLANG=1: make tracks timestamps, not
# flags, so a shared dir relinks the other config's objects.
CONFIG ?= debug
BUILD_DIR := build/$(CONFIG)$(if $(filter 1,$(UHD)),-uhd)$(if $(filter 1,$(CLANG)),-clang)
HARDWARE_DIR := $(SRC_DIR)/hardware
SIGNAL_DIR := $(SRC_DIR)/signal
FFT_DIR := $(SRC_DIR)/fft
VIS_DIR := $(SRC_DIR)/visualization
UTILS_DIR := $(SRC_DIR)/utils
GUI_DIR := $(SRC_DIR)/gui

# -isystem keeps vendored warnings out. Project includes are paths from src/.
INCLUDES := -isystem $(THIRD_PARTY) -isystem $(THIRD_PARTY)/pocketfft \
            -isystem $(THIRD_PARTY_STB) -I$(SRC_DIR) $(SDL3_CFLAGS)

HARDWARE_SRCS := $(wildcard $(HARDWARE_DIR)/*.cpp)
ifneq ($(UHD),1)
    HARDWARE_SRCS := $(filter-out $(HARDWARE_DIR)/uhd_device.cpp,$(HARDWARE_SRCS))
endif
SIGNAL_SRCS := $(wildcard $(SIGNAL_DIR)/*.cpp)
FFT_SRCS := $(wildcard $(FFT_DIR)/*.cpp)
VIS_SRCS := $(wildcard $(VIS_DIR)/*.cpp)
UTILS_SRCS := $(wildcard $(UTILS_DIR)/*.cpp)
GUI_SRCS := $(wildcard $(GUI_DIR)/*.cpp)
CORE_SRCS := $(wildcard $(SRC_DIR)/control_state.cpp)
MAIN_SRC := $(SRC_DIR)/main.cpp

ALL_SRCS := $(HARDWARE_SRCS) $(SIGNAL_SRCS) $(FFT_SRCS) $(VIS_SRCS) $(UTILS_SRCS) $(GUI_SRCS) $(CORE_SRCS) $(MAIN_SRC)

HARDWARE_OBJS := $(patsubst $(HARDWARE_DIR)/%.cpp,$(BUILD_DIR)/%.o,$(HARDWARE_SRCS))
SIGNAL_OBJS := $(patsubst $(SIGNAL_DIR)/%.cpp,$(BUILD_DIR)/%.o,$(SIGNAL_SRCS))
FFT_OBJS := $(patsubst $(FFT_DIR)/%.cpp,$(BUILD_DIR)/%.o,$(FFT_SRCS))
VIS_OBJS := $(patsubst $(VIS_DIR)/%.cpp,$(BUILD_DIR)/%.o,$(VIS_SRCS))
UTILS_OBJS := $(patsubst $(UTILS_DIR)/%.cpp,$(BUILD_DIR)/%.o,$(UTILS_SRCS))
GUI_OBJS := $(patsubst $(GUI_DIR)/%.cpp,$(BUILD_DIR)/%.o,$(GUI_SRCS))
CORE_OBJS := $(patsubst $(SRC_DIR)/%.cpp,$(BUILD_DIR)/%.o,$(CORE_SRCS))
MAIN_OBJ := $(BUILD_DIR)/main.o

ALL_OBJS := $(HARDWARE_OBJS) $(SIGNAL_OBJS) $(FFT_OBJS) $(VIS_OBJS) \
            $(UTILS_OBJS) $(GUI_OBJS) $(CORE_OBJS) $(MAIN_OBJ)

# Each tests/*_test.cpp is a program of asserts, linked with all but main.o.
TEST_SRCS := $(wildcard tests/*_test.cpp)
TEST_OBJS := $(patsubst tests/%.cpp,$(BUILD_DIR)/tests/%.o,$(TEST_SRCS))
TEST_BINS := $(TEST_OBJS:.o=)
# Intermediates of a chained rule: make would delete them after each run.
.SECONDARY: $(TEST_OBJS)

DEPS := $(ALL_OBJS:.o=.d) $(TEST_OBJS:.o=.d)
-include $(DEPS)

# Copied, not linked here: ./$(TARGET) is shared by every config, and one built
# by another config would look up to date.
all: $(BUILD_DIR)/$(TARGET)
	@cp -f $< $(TARGET)

$(BUILD_DIR):
	@mkdir -p $(BUILD_DIR)

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

$(BUILD_DIR)/$(TARGET): $(ALL_OBJS) $(RES_OBJ)
	$(CXX) $^ -o $@ $(LDFLAGS)

check: $(TEST_BINS)
	@for t in $^; do echo ">> $$t"; ./$$t || exit 1; done

$(BUILD_DIR)/tests/%.o: tests/%.cpp | $(BUILD_DIR)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(DEPFLAGS) -c $< -o $@

$(BUILD_DIR)/tests/%_test: $(BUILD_DIR)/tests/%_test.o $(filter-out $(MAIN_OBJ),$(ALL_OBJS))
	$(CXX) $^ -o $@ $(LDFLAGS)

# windres ships only with MinGW.
ifeq ($(OS),Windows_NT)
$(RES_OBJ): packaging/openspectrum.rc packaging/openspectrum.ico | $(BUILD_DIR)
	$(WINDRES) $< -O coff -o $@
endif

# Linux: AppImage; Windows: zip with the DLLs. The scripts CI runs too.
dist:
ifeq ($(OS),Windows_NT)
	bash packaging/windows-bundle.sh "$(VERSION)"
else
	bash packaging/linux-appimage.sh "$(VERSION)"
endif

clean:
	rm -rf build $(TARGET)

.PHONY: all check clean dist release profile debug
