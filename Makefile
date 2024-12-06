# Compiler
CXX = g++
# Compiler flags
CXXFLAGS = -Wall -std=c++17
# Include directories
INCLUDES = -I.

# Source files
SRC = Init.cpp Machine.cpp main.cpp Scheduler.cpp Simulator.cpp Task.cpp VM.cpp

# Object files
OBJ = $(SRC:.cpp=.o)

# Executable
TARGET = simulator

# Default target
all: $(TARGET)

# Default target
scheduler: $(OBJ)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o scheduler $(OBJ)

# Build target
$(TARGET): $(OBJ)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o $(TARGET) $(OBJ)

# Compile source files into object files
%.o: %.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

run:
	./simulator -v 3 Input.md

hour:
	./simulator -v 3 GentlerHour

nice:
	./simulator -v 3 Nice

spikey:
	./simulator -v 3 Spikey

bigsmall:
	./simulator -v 3 BigSmall

match:
	./simulator -v 3 MatchMe-1

spikey2:
	./simulator -v 3 Spikey2

tallshort:
	./simulator -v 3 TallAndShort