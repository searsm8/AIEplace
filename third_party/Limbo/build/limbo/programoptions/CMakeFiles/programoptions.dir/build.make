# CMAKE generated file: DO NOT EDIT!
# Generated by "Unix Makefiles" Generator, CMake Version 3.16

# Delete rule output on recipe failure.
.DELETE_ON_ERROR:


#=============================================================================
# Special targets provided by cmake.

# Disable implicit rules so canonical targets will work.
.SUFFIXES:


# Remove some rules from gmake that .SUFFIXES does not remove.
SUFFIXES =

.SUFFIXES: .hpux_make_needs_suffix_list


# Suppress display of executed commands.
$(VERBOSE).SILENT:


# A target that is always out of date.
cmake_force:

.PHONY : cmake_force

#=============================================================================
# Set environment variables for the build.

# The shell in which to execute make rules.
SHELL = /bin/sh

# The CMake executable.
CMAKE_COMMAND = /usr/bin/cmake

# The command to remove a file.
RM = /usr/bin/cmake -E remove -f

# Escaping for special characters.
EQUALS = =

# The top-level source directory on which CMake was run.
CMAKE_SOURCE_DIR = /home/msears/AIEplace/cpp/Limbo

# The top-level build directory on which CMake was run.
CMAKE_BINARY_DIR = /home/msears/AIEplace/cpp/Limbo/build

# Include any dependencies generated for this target.
include limbo/programoptions/CMakeFiles/programoptions.dir/depend.make

# Include the progress variables for this target.
include limbo/programoptions/CMakeFiles/programoptions.dir/progress.make

# Include the compile flags for this target's objects.
include limbo/programoptions/CMakeFiles/programoptions.dir/flags.make

limbo/programoptions/CMakeFiles/programoptions.dir/ProgramOptions.cpp.o: limbo/programoptions/CMakeFiles/programoptions.dir/flags.make
limbo/programoptions/CMakeFiles/programoptions.dir/ProgramOptions.cpp.o: ../limbo/programoptions/ProgramOptions.cpp
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green --progress-dir=/home/msears/AIEplace/cpp/Limbo/build/CMakeFiles --progress-num=$(CMAKE_PROGRESS_1) "Building CXX object limbo/programoptions/CMakeFiles/programoptions.dir/ProgramOptions.cpp.o"
	cd /home/msears/AIEplace/cpp/Limbo/build/limbo/programoptions && /usr/bin/c++  $(CXX_DEFINES) $(CXX_INCLUDES) $(CXX_FLAGS) -o CMakeFiles/programoptions.dir/ProgramOptions.cpp.o -c /home/msears/AIEplace/cpp/Limbo/limbo/programoptions/ProgramOptions.cpp

limbo/programoptions/CMakeFiles/programoptions.dir/ProgramOptions.cpp.i: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green "Preprocessing CXX source to CMakeFiles/programoptions.dir/ProgramOptions.cpp.i"
	cd /home/msears/AIEplace/cpp/Limbo/build/limbo/programoptions && /usr/bin/c++ $(CXX_DEFINES) $(CXX_INCLUDES) $(CXX_FLAGS) -E /home/msears/AIEplace/cpp/Limbo/limbo/programoptions/ProgramOptions.cpp > CMakeFiles/programoptions.dir/ProgramOptions.cpp.i

limbo/programoptions/CMakeFiles/programoptions.dir/ProgramOptions.cpp.s: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green "Compiling CXX source to assembly CMakeFiles/programoptions.dir/ProgramOptions.cpp.s"
	cd /home/msears/AIEplace/cpp/Limbo/build/limbo/programoptions && /usr/bin/c++ $(CXX_DEFINES) $(CXX_INCLUDES) $(CXX_FLAGS) -S /home/msears/AIEplace/cpp/Limbo/limbo/programoptions/ProgramOptions.cpp -o CMakeFiles/programoptions.dir/ProgramOptions.cpp.s

# Object files for target programoptions
programoptions_OBJECTS = \
"CMakeFiles/programoptions.dir/ProgramOptions.cpp.o"

# External object files for target programoptions
programoptions_EXTERNAL_OBJECTS =

limbo/programoptions/libprogramoptions.a: limbo/programoptions/CMakeFiles/programoptions.dir/ProgramOptions.cpp.o
limbo/programoptions/libprogramoptions.a: limbo/programoptions/CMakeFiles/programoptions.dir/build.make
limbo/programoptions/libprogramoptions.a: limbo/programoptions/CMakeFiles/programoptions.dir/link.txt
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green --bold --progress-dir=/home/msears/AIEplace/cpp/Limbo/build/CMakeFiles --progress-num=$(CMAKE_PROGRESS_2) "Linking CXX static library libprogramoptions.a"
	cd /home/msears/AIEplace/cpp/Limbo/build/limbo/programoptions && $(CMAKE_COMMAND) -P CMakeFiles/programoptions.dir/cmake_clean_target.cmake
	cd /home/msears/AIEplace/cpp/Limbo/build/limbo/programoptions && $(CMAKE_COMMAND) -E cmake_link_script CMakeFiles/programoptions.dir/link.txt --verbose=$(VERBOSE)

# Rule to build all files generated by this target.
limbo/programoptions/CMakeFiles/programoptions.dir/build: limbo/programoptions/libprogramoptions.a

.PHONY : limbo/programoptions/CMakeFiles/programoptions.dir/build

limbo/programoptions/CMakeFiles/programoptions.dir/clean:
	cd /home/msears/AIEplace/cpp/Limbo/build/limbo/programoptions && $(CMAKE_COMMAND) -P CMakeFiles/programoptions.dir/cmake_clean.cmake
.PHONY : limbo/programoptions/CMakeFiles/programoptions.dir/clean

limbo/programoptions/CMakeFiles/programoptions.dir/depend:
	cd /home/msears/AIEplace/cpp/Limbo/build && $(CMAKE_COMMAND) -E cmake_depends "Unix Makefiles" /home/msears/AIEplace/cpp/Limbo /home/msears/AIEplace/cpp/Limbo/limbo/programoptions /home/msears/AIEplace/cpp/Limbo/build /home/msears/AIEplace/cpp/Limbo/build/limbo/programoptions /home/msears/AIEplace/cpp/Limbo/build/limbo/programoptions/CMakeFiles/programoptions.dir/DependInfo.cmake --color=$(COLOR)
.PHONY : limbo/programoptions/CMakeFiles/programoptions.dir/depend
