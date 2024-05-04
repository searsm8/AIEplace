# Install script for directory: /home/msears/AIEplace/cpp/Limbo/limbo/parsers/lef/bison

# Set the install prefix
if(NOT DEFINED CMAKE_INSTALL_PREFIX)
  set(CMAKE_INSTALL_PREFIX "/home/msears/AIEplace/cpp/Limbo")
endif()
string(REGEX REPLACE "/$" "" CMAKE_INSTALL_PREFIX "${CMAKE_INSTALL_PREFIX}")

# Set the install configuration name.
if(NOT DEFINED CMAKE_INSTALL_CONFIG_NAME)
  if(BUILD_TYPE)
    string(REGEX REPLACE "^[^A-Za-z0-9_]+" ""
           CMAKE_INSTALL_CONFIG_NAME "${BUILD_TYPE}")
  else()
    set(CMAKE_INSTALL_CONFIG_NAME "Release")
  endif()
  message(STATUS "Install configuration: \"${CMAKE_INSTALL_CONFIG_NAME}\"")
endif()

# Set the component getting installed.
if(NOT CMAKE_INSTALL_COMPONENT)
  if(COMPONENT)
    message(STATUS "Install component: \"${COMPONENT}\"")
    set(CMAKE_INSTALL_COMPONENT "${COMPONENT}")
  else()
    set(CMAKE_INSTALL_COMPONENT)
  endif()
endif()

# Install shared libraries without execute permission?
if(NOT DEFINED CMAKE_INSTALL_SO_NO_EXE)
  set(CMAKE_INSTALL_SO_NO_EXE "1")
endif()

# Is this installation the result of a crosscompile?
if(NOT DEFINED CMAKE_CROSSCOMPILING)
  set(CMAKE_CROSSCOMPILING "FALSE")
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "/home/msears/AIEplace/cpp/Limbo/build/limbo/parsers/lef/bison/liblefparser.a")
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/limbo/parsers/lef/bison" TYPE FILE FILES
    "/home/msears/AIEplace/cpp/Limbo/limbo/parsers/lef/bison/LefDataBase.h"
    "/home/msears/AIEplace/cpp/Limbo/limbo/parsers/lef/bison/LefDriver.h"
    "/home/msears/AIEplace/cpp/Limbo/limbo/parsers/lef/bison/lefiArray.hpp"
    "/home/msears/AIEplace/cpp/Limbo/limbo/parsers/lef/bison/lefiCrossTalk.hpp"
    "/home/msears/AIEplace/cpp/Limbo/limbo/parsers/lef/bison/lefiDebug.hpp"
    "/home/msears/AIEplace/cpp/Limbo/limbo/parsers/lef/bison/lefiDefs.hpp"
    "/home/msears/AIEplace/cpp/Limbo/limbo/parsers/lef/bison/lefiKRDefs.hpp"
    "/home/msears/AIEplace/cpp/Limbo/limbo/parsers/lef/bison/lefiLayer.hpp"
    "/home/msears/AIEplace/cpp/Limbo/limbo/parsers/lef/bison/lefiMacro.hpp"
    "/home/msears/AIEplace/cpp/Limbo/limbo/parsers/lef/bison/lefiMisc.hpp"
    "/home/msears/AIEplace/cpp/Limbo/limbo/parsers/lef/bison/lefiNonDefault.hpp"
    "/home/msears/AIEplace/cpp/Limbo/limbo/parsers/lef/bison/lefiProp.hpp"
    "/home/msears/AIEplace/cpp/Limbo/limbo/parsers/lef/bison/lefiPropType.hpp"
    "/home/msears/AIEplace/cpp/Limbo/limbo/parsers/lef/bison/lefiUnits.hpp"
    "/home/msears/AIEplace/cpp/Limbo/limbo/parsers/lef/bison/lefiUser.hpp"
    "/home/msears/AIEplace/cpp/Limbo/limbo/parsers/lef/bison/lefiUtil.hpp"
    "/home/msears/AIEplace/cpp/Limbo/limbo/parsers/lef/bison/lefiVia.hpp"
    "/home/msears/AIEplace/cpp/Limbo/limbo/parsers/lef/bison/lefiViaRule.hpp"
    )
endif()

