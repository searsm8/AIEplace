# Install script for directory: /home/msears/AIEplace/cpp/Limbo/limbo/thirdparty/lefdef/5.8/lef

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
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "/home/msears/AIEplace/cpp/Limbo/build/limbo/thirdparty/lefdef/5.8/lef/liblef.a")
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/limbo/thirdparty/lefdef/5.8/lef/lef" TYPE FILE FILES
    "/home/msears/AIEplace/cpp/Limbo/limbo/thirdparty/lefdef/5.8/lef/lef/crypt.hpp"
    "/home/msears/AIEplace/cpp/Limbo/limbo/thirdparty/lefdef/5.8/lef/lef/lefiCrossTalk.hpp"
    "/home/msears/AIEplace/cpp/Limbo/limbo/thirdparty/lefdef/5.8/lef/lef/lefiDefs.hpp"
    "/home/msears/AIEplace/cpp/Limbo/limbo/thirdparty/lefdef/5.8/lef/lef/lefiKRDefs.hpp"
    "/home/msears/AIEplace/cpp/Limbo/limbo/thirdparty/lefdef/5.8/lef/lef/lefiMacro.hpp"
    "/home/msears/AIEplace/cpp/Limbo/limbo/thirdparty/lefdef/5.8/lef/lef/lefiNonDefault.hpp"
    "/home/msears/AIEplace/cpp/Limbo/limbo/thirdparty/lefdef/5.8/lef/lef/lefiPropType.hpp"
    "/home/msears/AIEplace/cpp/Limbo/limbo/thirdparty/lefdef/5.8/lef/lef/lefiUser.hpp"
    "/home/msears/AIEplace/cpp/Limbo/limbo/thirdparty/lefdef/5.8/lef/lef/lefiVia.hpp"
    "/home/msears/AIEplace/cpp/Limbo/limbo/thirdparty/lefdef/5.8/lef/lef/lefrCallBacks.hpp"
    "/home/msears/AIEplace/cpp/Limbo/limbo/thirdparty/lefdef/5.8/lef/lef/lefrReader.hpp"
    "/home/msears/AIEplace/cpp/Limbo/limbo/thirdparty/lefdef/5.8/lef/lef/lefwWriterCalls.hpp"
    "/home/msears/AIEplace/cpp/Limbo/limbo/thirdparty/lefdef/5.8/lef/lef/lefiArray.hpp"
    "/home/msears/AIEplace/cpp/Limbo/limbo/thirdparty/lefdef/5.8/lef/lef/lefiDebug.hpp"
    "/home/msears/AIEplace/cpp/Limbo/limbo/thirdparty/lefdef/5.8/lef/lef/lefiEncryptInt.hpp"
    "/home/msears/AIEplace/cpp/Limbo/limbo/thirdparty/lefdef/5.8/lef/lef/lefiLayer.hpp"
    "/home/msears/AIEplace/cpp/Limbo/limbo/thirdparty/lefdef/5.8/lef/lef/lefiMisc.hpp"
    "/home/msears/AIEplace/cpp/Limbo/limbo/thirdparty/lefdef/5.8/lef/lef/lefiProp.hpp"
    "/home/msears/AIEplace/cpp/Limbo/limbo/thirdparty/lefdef/5.8/lef/lef/lefiUnits.hpp"
    "/home/msears/AIEplace/cpp/Limbo/limbo/thirdparty/lefdef/5.8/lef/lef/lefiUtil.hpp"
    "/home/msears/AIEplace/cpp/Limbo/limbo/thirdparty/lefdef/5.8/lef/lef/lefiViaRule.hpp"
    "/home/msears/AIEplace/cpp/Limbo/limbo/thirdparty/lefdef/5.8/lef/lef/lefrData.hpp"
    "/home/msears/AIEplace/cpp/Limbo/limbo/thirdparty/lefdef/5.8/lef/lef/lefrSettings.hpp"
    "/home/msears/AIEplace/cpp/Limbo/limbo/thirdparty/lefdef/5.8/lef/lef/lefwWriter.hpp"
    )
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "/home/msears/AIEplace/cpp/Limbo/build/limbo/thirdparty/lefdef/5.8/lef/libclef.a")
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "/home/msears/AIEplace/cpp/Limbo/build/limbo/thirdparty/lefdef/5.8/lef/liblefzlib.a")
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "/home/msears/AIEplace/cpp/Limbo/build/limbo/thirdparty/lefdef/5.8/lef/libclefzlib.a")
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/bin/thirdparty/lefdef/5.8/lef/lefrw" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/bin/thirdparty/lefdef/5.8/lef/lefrw")
    file(RPATH_CHECK
         FILE "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/bin/thirdparty/lefdef/5.8/lef/lefrw"
         RPATH "")
  endif()
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/bin/thirdparty/lefdef/5.8/lef" TYPE EXECUTABLE FILES "/home/msears/AIEplace/cpp/Limbo/build/limbo/thirdparty/lefdef/5.8/lef/bin/lefrw")
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/bin/thirdparty/lefdef/5.8/lef/lefrw" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/bin/thirdparty/lefdef/5.8/lef/lefrw")
    if(CMAKE_INSTALL_DO_STRIP)
      execute_process(COMMAND "/usr/bin/strip" "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/bin/thirdparty/lefdef/5.8/lef/lefrw")
    endif()
  endif()
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/bin/thirdparty/lefdef/5.8/lef/lefdiff" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/bin/thirdparty/lefdef/5.8/lef/lefdiff")
    file(RPATH_CHECK
         FILE "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/bin/thirdparty/lefdef/5.8/lef/lefdiff"
         RPATH "")
  endif()
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/bin/thirdparty/lefdef/5.8/lef" TYPE EXECUTABLE FILES "/home/msears/AIEplace/cpp/Limbo/build/limbo/thirdparty/lefdef/5.8/lef/bin/lefdiff")
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/bin/thirdparty/lefdef/5.8/lef/lefdiff" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/bin/thirdparty/lefdef/5.8/lef/lefdiff")
    if(CMAKE_INSTALL_DO_STRIP)
      execute_process(COMMAND "/usr/bin/strip" "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/bin/thirdparty/lefdef/5.8/lef/lefdiff")
    endif()
  endif()
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/bin/thirdparty/lefdef/5.8/lef/lefwrite" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/bin/thirdparty/lefdef/5.8/lef/lefwrite")
    file(RPATH_CHECK
         FILE "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/bin/thirdparty/lefdef/5.8/lef/lefwrite"
         RPATH "")
  endif()
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/bin/thirdparty/lefdef/5.8/lef" TYPE EXECUTABLE FILES "/home/msears/AIEplace/cpp/Limbo/build/limbo/thirdparty/lefdef/5.8/lef/bin/lefwrite")
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/bin/thirdparty/lefdef/5.8/lef/lefwrite" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/bin/thirdparty/lefdef/5.8/lef/lefwrite")
    if(CMAKE_INSTALL_DO_STRIP)
      execute_process(COMMAND "/usr/bin/strip" "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/bin/thirdparty/lefdef/5.8/lef/lefwrite")
    endif()
  endif()
endif()

