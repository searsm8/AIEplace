# Install script for directory: /home/msears/AIEplace/cpp/Limbo/limbo/makeutils

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
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/limbo/makeutils" TYPE FILE FILES
    "/home/msears/AIEplace/cpp/Limbo/limbo/makeutils/FindAR.mk"
    "/home/msears/AIEplace/cpp/Limbo/limbo/makeutils/FindBoost.mk"
    "/home/msears/AIEplace/cpp/Limbo/limbo/makeutils/FindClang.mk"
    "/home/msears/AIEplace/cpp/Limbo/limbo/makeutils/FindClangxx.mk"
    "/home/msears/AIEplace/cpp/Limbo/limbo/makeutils/FindCompiler.mk"
    "/home/msears/AIEplace/cpp/Limbo/limbo/makeutils/FindGcc.mk"
    "/home/msears/AIEplace/cpp/Limbo/limbo/makeutils/FindGfortran.mk"
    "/home/msears/AIEplace/cpp/Limbo/limbo/makeutils/FindGurobi.mk"
    "/home/msears/AIEplace/cpp/Limbo/limbo/makeutils/FindGxx.mk"
    "/home/msears/AIEplace/cpp/Limbo/limbo/makeutils/FindLPSolve.mk"
    "/home/msears/AIEplace/cpp/Limbo/limbo/makeutils/FindLemon.mk"
    "/home/msears/AIEplace/cpp/Limbo/limbo/makeutils/FindZlib.mk"
    "/home/msears/AIEplace/cpp/Limbo/limbo/makeutils/GCCVersion.mk"
    )
endif()

