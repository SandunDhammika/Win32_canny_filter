CXX := g++
CXXFLAGS := -std=c++17 -O2 -Wall -Wextra -municode
LDFLAGS := -mwindows -lgdi32 -luser32 -lcomctl32

TARGET := canny_gui.exe
SRC := main.cpp
OBJ := $(SRC:.cpp=.o)

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CXX) $(OBJ) -o $@ $(LDFLAGS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	del /Q $(OBJ) $(TARGET) 2>NUL || exit 0
