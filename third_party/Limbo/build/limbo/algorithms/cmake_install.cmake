# Install script for directory: /home/msears/AIEplace/cpp/Limbo/limbo/algorithms

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
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/limbo/algorithms" TYPE FILE FILES
    "/home/msears/AIEplace/cpp/Limbo/limbo/algorithms/GraphUtility.h"
    "/home/msears/AIEplace/cpp/Limbo/limbo/algorithms/MaxClique.h"
    "/home/msears/AIEplace/cpp/Limbo/limbo/algorithms/MaxIndependentSet.h"
    )
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/limbo/algorithms/coloring" TYPE FILE FILES
    "/home/msears/AIEplace/cpp/Limbo/limbo/algorithms/coloring/BacktrackColoring.h"
    "/home/msears/AIEplace/cpp/Limbo/limbo/algorithms/coloring/ChromaticNumber.h"
    "/home/msears/AIEplace/cpp/Limbo/limbo/algorithms/coloring/Coloring.h"
    "/home/msears/AIEplace/cpp/Limbo/limbo/algorithms/coloring/GraphSimplification.h"
    "/home/msears/AIEplace/cpp/Limbo/limbo/algorithms/coloring/GreedyColoring.h"
    "/home/msears/AIEplace/cpp/Limbo/limbo/algorithms/coloring/ILPColoring.h"
    "/home/msears/AIEplace/cpp/Limbo/limbo/algorithms/coloring/ILPColoringLemonCbc.h"
    "/home/msears/AIEplace/cpp/Limbo/limbo/algorithms/coloring/ILPColoringUpdated.h"
    "/home/msears/AIEplace/cpp/Limbo/limbo/algorithms/coloring/LPColoring.h"
    "/home/msears/AIEplace/cpp/Limbo/limbo/algorithms/coloring/LPColoringOld.h"
    "/home/msears/AIEplace/cpp/Limbo/limbo/algorithms/coloring/MISColoring.h"
    "/home/msears/AIEplace/cpp/Limbo/limbo/algorithms/coloring/SDPColoringCsdp.h"
    )
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/limbo/algorithms/partition" TYPE FILE FILES
    "/home/msears/AIEplace/cpp/Limbo/limbo/algorithms/partition/FM.h"
    "/home/msears/AIEplace/cpp/Limbo/limbo/algorithms/partition/FMMultiWay.h"
    )
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/limbo/algorithms/placement" TYPE FILE FILES "/home/msears/AIEplace/cpp/Limbo/limbo/algorithms/placement/GreedySearch.h")
endif()

