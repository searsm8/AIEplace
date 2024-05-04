# Install script for directory: /home/msears/AIEplace/cpp/Limbo/limbo/solvers

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
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/limbo/solvers" TYPE FILE FILES
    "/home/msears/AIEplace/cpp/Limbo/limbo/solvers/DualMinCostFlow.h"
    "/home/msears/AIEplace/cpp/Limbo/limbo/solvers/MinCostFlow.h"
    "/home/msears/AIEplace/cpp/Limbo/limbo/solvers/MultiKnapsackLagRelax.h"
    "/home/msears/AIEplace/cpp/Limbo/limbo/solvers/Numerical.h"
    "/home/msears/AIEplace/cpp/Limbo/limbo/solvers/Solvers.h"
    )
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/limbo/solvers/api" TYPE FILE FILES
    "/home/msears/AIEplace/cpp/Limbo/limbo/solvers/api/CsdpEasySdpApi.h"
    "/home/msears/AIEplace/cpp/Limbo/limbo/solvers/api/GurobiApi.h"
    "/home/msears/AIEplace/cpp/Limbo/limbo/solvers/api/LPSolveApi.h"
    )
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/limbo/solvers/lpmcf" TYPE FILE FILES
    "/home/msears/AIEplace/cpp/Limbo/limbo/solvers/lpmcf/Lgf.h"
    "/home/msears/AIEplace/cpp/Limbo/limbo/solvers/lpmcf/LpDualMcf.h"
    )
endif()

