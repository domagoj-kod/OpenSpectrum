CC := gcc
CFLAGS := -O2 -g

CXX := g++
CXXFLAGS := -std=c++17 -Wall -Wextra -O2 -g
LDFLAGS := -lrtlsdr -lpthread

# Directories
SRC_DIR := src
THIRD_PARTY := third_party/kissfft
BUILD_DIR := build
HARDWARE_DIR := $(SRC_DIR)/hardware

# Include paths
INCLUDES := -I$(THIRD_PARTY) -I$(HARDWARE_DIR)

# Source files
HARDWARE_SRCS := $(wildcard $(HARDWARE_DIR)/*.cpp)
KISSFFT_SRCS := $(wildcard $(THIRD_PARTY)/*.c)
TEST_SRCS := $(SRC_DIR)/main.cpp

# Object files
HARDWARE_OBJS := $(patsubst $(HARDWARE_DIR)/%.cpp,$(BUILD_DIR)/%.o,$(HARDWARE_SRCS))
KISSFFT_OBJS := $(patsubst $(THIRD_PARTY)/%.c,$(BUILD_DIR)/%.o,$(KISSFFT_SRCS))
TEST_OBJ := $(BUILD_DIR)/main.o

# Targets
TARGETS := main

all: $(TARGETS)

# Build directory
$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

# Object files
$(BUILD_DIR)/%.o: $(HARDWARE_DIR)/%.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

$(BUILD_DIR)/%.o: $(THIRD_PARTY)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

$(TEST_OBJ): $(SRC_DIR)/main.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

TEST_TARGET := rtl_sdr_test
TEST_SRC := test/rtl_sdr_test.cpp

$(TEST_TARGET): $(HARDWARE_OBJS) $(KISSFFT_OBJS) $(BUILD_DIR)/rtl_sdr_test.o
	$(CXX) $^ -o $@ $(LDFLAGS)

$(BUILD_DIR)/rtl_sdr_test.o: test/rtl_sdr_test.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

# Test harness
main: $(HARDWARE_OBJS) $(KISSFFT_OBJS) $(TEST_OBJ)
	$(CXX) $^ -o $@ $(LDFLAGS)

# Clean
clean:
	rm -rf $(BUILD_DIR) $(TARGETS)

.PHONY: all clean