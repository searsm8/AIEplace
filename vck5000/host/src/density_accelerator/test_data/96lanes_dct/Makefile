CXX      := g++
CXXFLAGS := -O3 -std=c++17 -Wall -Wextra -MMD -MP
LDLIBS   := -lfftw3f -lm

TARGET := fftw_program
SOURCES := generate_data.cpp
OBJECTS := $(SOURCES:.cpp=.o)
DEPS    := $(OBJECTS:.o=.d)

.PHONY: all clean run

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CXX) $(OBJECTS) -o $@ $(LDLIBS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET) $(OBJECTS) $(DEPS)

-include $(DEPS)
