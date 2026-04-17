CXX = g++
CXXFLAGS = -Wall -Wextra -Iinclude
TARGET = build/app
SRC = $(wildcard src/*.cpp)

all: $(TARGET)

$(TARGET): $(SRC)
	mkdir -p build
	$(CXX) $(CXXFLAGS) $^ -o $@

clean:
	rm -rf build