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
include limbo/thirdparty/lefdef/5.8/lef/CMakeFiles/clefzlib.dir/depend.make

# Include the progress variables for this target.
include limbo/thirdparty/lefdef/5.8/lef/CMakeFiles/clefzlib.dir/progress.make

# Include the compile flags for this target's objects.
include limbo/thirdparty/lefdef/5.8/lef/CMakeFiles/clefzlib.dir/flags.make

limbo/thirdparty/lefdef/5.8/lef/CMakeFiles/clefzlib.dir/clefzlib/clefzlib.c.o: limbo/thirdparty/lefdef/5.8/lef/CMakeFiles/clefzlib.dir/flags.make
limbo/thirdparty/lefdef/5.8/lef/CMakeFiles/clefzlib.dir/clefzlib/clefzlib.c.o: ../limbo/thirdparty/lefdef/5.8/lef/clefzlib/clefzlib.c
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green --progress-dir=/home/msears/AIEplace/cpp/Limbo/build/CMakeFiles --progress-num=$(CMAKE_PROGRESS_1) "Building C object limbo/thirdparty/lefdef/5.8/lef/CMakeFiles/clefzlib.dir/clefzlib/clefzlib.c.o"
	cd /home/msears/AIEplace/cpp/Limbo/build/limbo/thirdparty/lefdef/5.8/lef && /usr/bin/cc $(C_DEFINES) $(C_INCLUDES) $(C_FLAGS) -o CMakeFiles/clefzlib.dir/clefzlib/clefzlib.c.o   -c /home/msears/AIEplace/cpp/Limbo/limbo/thirdparty/lefdef/5.8/lef/clefzlib/clefzlib.c

limbo/thirdparty/lefdef/5.8/lef/CMakeFiles/clefzlib.dir/clefzlib/clefzlib.c.i: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green "Preprocessing C source to CMakeFiles/clefzlib.dir/clefzlib/clefzlib.c.i"
	cd /home/msears/AIEplace/cpp/Limbo/build/limbo/thirdparty/lefdef/5.8/lef && /usr/bin/cc $(C_DEFINES) $(C_INCLUDES) $(C_FLAGS) -E /home/msears/AIEplace/cpp/Limbo/limbo/thirdparty/lefdef/5.8/lef/clefzlib/clefzlib.c > CMakeFiles/clefzlib.dir/clefzlib/clefzlib.c.i

limbo/thirdparty/lefdef/5.8/lef/CMakeFiles/clefzlib.dir/clefzlib/clefzlib.c.s: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green "Compiling C source to assembly CMakeFiles/clefzlib.dir/clefzlib/clefzlib.c.s"
	cd /home/msears/AIEplace/cpp/Limbo/build/limbo/thirdparty/lefdef/5.8/lef && /usr/bin/cc $(C_DEFINES) $(C_INCLUDES) $(C_FLAGS) -S /home/msears/AIEplace/cpp/Limbo/limbo/thirdparty/lefdef/5.8/lef/clefzlib/clefzlib.c -o CMakeFiles/clefzlib.dir/clefzlib/clefzlib.c.s

# Object files for target clefzlib
clefzlib_OBJECTS = \
"CMakeFiles/clefzlib.dir/clefzlib/clefzlib.c.o"

# External object files for target clefzlib
clefzlib_EXTERNAL_OBJECTS =

limbo/thirdparty/lefdef/5.8/lef/libclefzlib.a: limbo/thirdparty/lefdef/5.8/lef/CMakeFiles/clefzlib.dir/clefzlib/clefzlib.c.o
limbo/thirdparty/lefdef/5.8/lef/libclefzlib.a: limbo/thirdparty/lefdef/5.8/lef/CMakeFiles/clefzlib.dir/build.make
limbo/thirdparty/lefdef/5.8/lef/libclefzlib.a: limbo/thirdparty/lefdef/5.8/lef/CMakeFiles/clefzlib.dir/link.txt
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green --bold --progress-dir=/home/msears/AIEplace/cpp/Limbo/build/CMakeFiles --progress-num=$(CMAKE_PROGRESS_2) "Linking C static library libclefzlib.a"
	cd /home/msears/AIEplace/cpp/Limbo/build/limbo/thirdparty/lefdef/5.8/lef && $(CMAKE_COMMAND) -P CMakeFiles/clefzlib.dir/cmake_clean_target.cmake
	cd /home/msears/AIEplace/cpp/Limbo/build/limbo/thirdparty/lefdef/5.8/lef && $(CMAKE_COMMAND) -E cmake_link_script CMakeFiles/clefzlib.dir/link.txt --verbose=$(VERBOSE)

# Rule to build all files generated by this target.
limbo/thirdparty/lefdef/5.8/lef/CMakeFiles/clefzlib.dir/build: limbo/thirdparty/lefdef/5.8/lef/libclefzlib.a

.PHONY : limbo/thirdparty/lefdef/5.8/lef/CMakeFiles/clefzlib.dir/build

limbo/thirdparty/lefdef/5.8/lef/CMakeFiles/clefzlib.dir/clean:
	cd /home/msears/AIEplace/cpp/Limbo/build/limbo/thirdparty/lefdef/5.8/lef && $(CMAKE_COMMAND) -P CMakeFiles/clefzlib.dir/cmake_clean.cmake
.PHONY : limbo/thirdparty/lefdef/5.8/lef/CMakeFiles/clefzlib.dir/clean

limbo/thirdparty/lefdef/5.8/lef/CMakeFiles/clefzlib.dir/depend:
	cd /home/msears/AIEplace/cpp/Limbo/build && $(CMAKE_COMMAND) -E cmake_depends "Unix Makefiles" /home/msears/AIEplace/cpp/Limbo /home/msears/AIEplace/cpp/Limbo/limbo/thirdparty/lefdef/5.8/lef /home/msears/AIEplace/cpp/Limbo/build /home/msears/AIEplace/cpp/Limbo/build/limbo/thirdparty/lefdef/5.8/lef /home/msears/AIEplace/cpp/Limbo/build/limbo/thirdparty/lefdef/5.8/lef/CMakeFiles/clefzlib.dir/DependInfo.cmake --color=$(COLOR)
.PHONY : limbo/thirdparty/lefdef/5.8/lef/CMakeFiles/clefzlib.dir/depend

