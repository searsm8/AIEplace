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
include limbo/thirdparty/CThreadPool/CMakeFiles/CThreadPool_thpool.dir/depend.make

# Include the progress variables for this target.
include limbo/thirdparty/CThreadPool/CMakeFiles/CThreadPool_thpool.dir/progress.make

# Include the compile flags for this target's objects.
include limbo/thirdparty/CThreadPool/CMakeFiles/CThreadPool_thpool.dir/flags.make

limbo/thirdparty/CThreadPool/CMakeFiles/CThreadPool_thpool.dir/thpool.c.o: limbo/thirdparty/CThreadPool/CMakeFiles/CThreadPool_thpool.dir/flags.make
limbo/thirdparty/CThreadPool/CMakeFiles/CThreadPool_thpool.dir/thpool.c.o: ../limbo/thirdparty/CThreadPool/thpool.c
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green --progress-dir=/home/msears/AIEplace/cpp/Limbo/build/CMakeFiles --progress-num=$(CMAKE_PROGRESS_1) "Building C object limbo/thirdparty/CThreadPool/CMakeFiles/CThreadPool_thpool.dir/thpool.c.o"
	cd /home/msears/AIEplace/cpp/Limbo/build/limbo/thirdparty/CThreadPool && /usr/bin/cc $(C_DEFINES) $(C_INCLUDES) $(C_FLAGS) -o CMakeFiles/CThreadPool_thpool.dir/thpool.c.o   -c /home/msears/AIEplace/cpp/Limbo/limbo/thirdparty/CThreadPool/thpool.c

limbo/thirdparty/CThreadPool/CMakeFiles/CThreadPool_thpool.dir/thpool.c.i: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green "Preprocessing C source to CMakeFiles/CThreadPool_thpool.dir/thpool.c.i"
	cd /home/msears/AIEplace/cpp/Limbo/build/limbo/thirdparty/CThreadPool && /usr/bin/cc $(C_DEFINES) $(C_INCLUDES) $(C_FLAGS) -E /home/msears/AIEplace/cpp/Limbo/limbo/thirdparty/CThreadPool/thpool.c > CMakeFiles/CThreadPool_thpool.dir/thpool.c.i

limbo/thirdparty/CThreadPool/CMakeFiles/CThreadPool_thpool.dir/thpool.c.s: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green "Compiling C source to assembly CMakeFiles/CThreadPool_thpool.dir/thpool.c.s"
	cd /home/msears/AIEplace/cpp/Limbo/build/limbo/thirdparty/CThreadPool && /usr/bin/cc $(C_DEFINES) $(C_INCLUDES) $(C_FLAGS) -S /home/msears/AIEplace/cpp/Limbo/limbo/thirdparty/CThreadPool/thpool.c -o CMakeFiles/CThreadPool_thpool.dir/thpool.c.s

# Object files for target CThreadPool_thpool
CThreadPool_thpool_OBJECTS = \
"CMakeFiles/CThreadPool_thpool.dir/thpool.c.o"

# External object files for target CThreadPool_thpool
CThreadPool_thpool_EXTERNAL_OBJECTS =

limbo/thirdparty/CThreadPool/libCThreadPool_thpool.a: limbo/thirdparty/CThreadPool/CMakeFiles/CThreadPool_thpool.dir/thpool.c.o
limbo/thirdparty/CThreadPool/libCThreadPool_thpool.a: limbo/thirdparty/CThreadPool/CMakeFiles/CThreadPool_thpool.dir/build.make
limbo/thirdparty/CThreadPool/libCThreadPool_thpool.a: limbo/thirdparty/CThreadPool/CMakeFiles/CThreadPool_thpool.dir/link.txt
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green --bold --progress-dir=/home/msears/AIEplace/cpp/Limbo/build/CMakeFiles --progress-num=$(CMAKE_PROGRESS_2) "Linking C static library libCThreadPool_thpool.a"
	cd /home/msears/AIEplace/cpp/Limbo/build/limbo/thirdparty/CThreadPool && $(CMAKE_COMMAND) -P CMakeFiles/CThreadPool_thpool.dir/cmake_clean_target.cmake
	cd /home/msears/AIEplace/cpp/Limbo/build/limbo/thirdparty/CThreadPool && $(CMAKE_COMMAND) -E cmake_link_script CMakeFiles/CThreadPool_thpool.dir/link.txt --verbose=$(VERBOSE)

# Rule to build all files generated by this target.
limbo/thirdparty/CThreadPool/CMakeFiles/CThreadPool_thpool.dir/build: limbo/thirdparty/CThreadPool/libCThreadPool_thpool.a

.PHONY : limbo/thirdparty/CThreadPool/CMakeFiles/CThreadPool_thpool.dir/build

limbo/thirdparty/CThreadPool/CMakeFiles/CThreadPool_thpool.dir/clean:
	cd /home/msears/AIEplace/cpp/Limbo/build/limbo/thirdparty/CThreadPool && $(CMAKE_COMMAND) -P CMakeFiles/CThreadPool_thpool.dir/cmake_clean.cmake
.PHONY : limbo/thirdparty/CThreadPool/CMakeFiles/CThreadPool_thpool.dir/clean

limbo/thirdparty/CThreadPool/CMakeFiles/CThreadPool_thpool.dir/depend:
	cd /home/msears/AIEplace/cpp/Limbo/build && $(CMAKE_COMMAND) -E cmake_depends "Unix Makefiles" /home/msears/AIEplace/cpp/Limbo /home/msears/AIEplace/cpp/Limbo/limbo/thirdparty/CThreadPool /home/msears/AIEplace/cpp/Limbo/build /home/msears/AIEplace/cpp/Limbo/build/limbo/thirdparty/CThreadPool /home/msears/AIEplace/cpp/Limbo/build/limbo/thirdparty/CThreadPool/CMakeFiles/CThreadPool_thpool.dir/DependInfo.cmake --color=$(COLOR)
.PHONY : limbo/thirdparty/CThreadPool/CMakeFiles/CThreadPool_thpool.dir/depend

