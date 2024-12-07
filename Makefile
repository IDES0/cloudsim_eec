# Compiler
CXX = g++
# Compiler flags
CXXFLAGS = -Wall -std=c++17
# Include directories
INCLUDES = -I.

# Common object files
COMMON_OBJ = Init.o Machine.o main.o Simulator.o Task.o VM.o

# Different scheduler implementations
SCHEDULER_SOURCES = Best.cpp Brute.cpp Greedy.cpp
SCHEDULER_OBJ = Best.o Brute.o Greedy.o

# Executable
TARGET = simulator

# Default target
all: $(SCHEDULER_OBJ)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o best_scheduler $(COMMON_OBJ) Best.o
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o brute_scheduler $(COMMON_OBJ) Brute.o
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o greedy_scheduler $(COMMON_OBJ) Greedy.o

# Compile source files into object files
%.o: %.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

best: Best.o $(COMMON_OBJ)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o best_scheduler $(COMMON_OBJ) Best.o

brute: Brute.o $(COMMON_OBJ)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o brute_scheduler $(COMMON_OBJ) Brute.o

greedy: Greedy.o $(COMMON_OBJ)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o greedy_scheduler $(COMMON_OBJ) Greedy.o

clean:
	rm -f *.o best_scheduler brute_scheduler greedy_scheduler

run:
	./simulator -v 3 Input.md

run_best:
	./best_scheduler -v 3 Input.md

run_brute:
	./brute_scheduler -v 3 Input.md

run_greedy:
	./greedy_scheduler -v 3 Input.md

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