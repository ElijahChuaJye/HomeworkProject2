# Compiler settings
CXX = g++
# Enforce C++17 standard, enable major warnings, and apply maximum optimization for performance
CXXFLAGS = -std=c++17 -Wall -Wextra -O3

# Executable name required by the assignment
TARGET = simplify

# Find all .cpp files in the current directory
SRCS = $(wildcard *.cpp)
# Replace .cpp extension with .o for object files
OBJS = $(SRCS:.cpp=.o)

# Default rule triggered when running just 'make'
all: $(TARGET)

# Rule to link object files into the final executable
$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(OBJS)

# Rule to compile source files into object files
%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Rule to clean up build artifacts
clean:
	rm -f $(OBJS) $(TARGET)

# Phony targets to prevent conflicts with files named 'all' or 'clean'
.PHONY: all clean