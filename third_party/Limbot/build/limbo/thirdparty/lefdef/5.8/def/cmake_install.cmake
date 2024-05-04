# Install script for directory: /home/msears/AIEplace/cpp/Limbo/limbo/thirdparty/lefdef/5.8/def

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
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "/home/msears/AIEplace/cpp/Limbo/build/limbo/thirdparty/lefdef/5.8/def/libdef.a")
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/limbo/thirdparty/lefdef/5.8/def/def" TYPE FILE FILES
    "/home/msears/AIEplace/cpp/Limbo/limbo/thirdparty/lefdef/5.8/def/def/defiAlias.hpp"
    "/home/msears/AIEplace/cpp/Limbo/limbo/thirdparty/lefdef/5.8/def/def/defiDebug.hpp"
    "/home/msears/AIEplace/cpp/Limbo/limbo/thirdparty/lefdef/5.8/def/def/defiGroup.hpp"
    "/home/msears/AIEplace/cpp/Limbo/limbo/thirdparty/lefdef/5.8/def/def/defiNet.hpp"
    "/home/msears/AIEplace/cpp/Limbo/limbo/thirdparty/lefdef/5.8/def/def/defiPinCap.hpp"
    "/home/msears/AIEplace/cpp/Limbo/limbo/thirdparty/lefdef/5.8/def/def/defiRegion.hpp"
    "/home/msears/AIEplace/cpp/Limbo/limbo/thirdparty/lefdef/5.8/def/def/defiSlot.hpp"
    "/home/msears/AIEplace/cpp/Limbo/limbo/thirdparty/lefdef/5.8/def/def/defiVia.hpp"
    "/home/msears/AIEplace/cpp/Limbo/limbo/thirdparty/lefdef/5.8/def/def/defrSettings.hpp"
    "/home/msears/AIEplace/cpp/Limbo/limbo/thirdparty/lefdef/5.8/def/def/defiAssertion.hpp"
    "/home/msears/AIEplace/cpp/Limbo/limbo/thirdparty/lefdef/5.8/def/def/defiDefs.hpp"
    "/home/msears/AIEplace/cpp/Limbo/limbo/thirdparty/lefdef/5.8/def/def/defiIOTiming.hpp"
    "/home/msears/AIEplace/cpp/Limbo/limbo/thirdparty/lefdef/5.8/def/def/defiNonDefault.hpp"
    "/home/msears/AIEplace/cpp/Limbo/limbo/thirdparty/lefdef/5.8/def/def/defiPinProp.hpp"
    "/home/msears/AIEplace/cpp/Limbo/limbo/thirdparty/lefdef/5.8/def/def/defiRowTrack.hpp"
    "/home/msears/AIEplace/cpp/Limbo/limbo/thirdparty/lefdef/5.8/def/def/defiTimingDisable.hpp"
    "/home/msears/AIEplace/cpp/Limbo/limbo/thirdparty/lefdef/5.8/def/def/defrCallBacks.hpp"
    "/home/msears/AIEplace/cpp/Limbo/limbo/thirdparty/lefdef/5.8/def/def/defwWriterCalls.hpp"
    "/home/msears/AIEplace/cpp/Limbo/limbo/thirdparty/lefdef/5.8/def/def/defiBlockage.hpp"
    "/home/msears/AIEplace/cpp/Limbo/limbo/thirdparty/lefdef/5.8/def/def/defiFill.hpp"
    "/home/msears/AIEplace/cpp/Limbo/limbo/thirdparty/lefdef/5.8/def/def/defiKRDefs.hpp"
    "/home/msears/AIEplace/cpp/Limbo/limbo/thirdparty/lefdef/5.8/def/def/defiPartition.hpp"
    "/home/msears/AIEplace/cpp/Limbo/limbo/thirdparty/lefdef/5.8/def/def/defiProp.hpp"
    "/home/msears/AIEplace/cpp/Limbo/limbo/thirdparty/lefdef/5.8/def/def/defiScanchain.hpp"
    "/home/msears/AIEplace/cpp/Limbo/limbo/thirdparty/lefdef/5.8/def/def/defiUser.hpp"
    "/home/msears/AIEplace/cpp/Limbo/limbo/thirdparty/lefdef/5.8/def/def/defrData.hpp"
    "/home/msears/AIEplace/cpp/Limbo/limbo/thirdparty/lefdef/5.8/def/def/defwWriter.hpp"
    "/home/msears/AIEplace/cpp/Limbo/limbo/thirdparty/lefdef/5.8/def/def/defiComponent.hpp"
    "/home/msears/AIEplace/cpp/Limbo/limbo/thirdparty/lefdef/5.8/def/def/defiFPC.hpp"
    "/home/msears/AIEplace/cpp/Limbo/limbo/thirdparty/lefdef/5.8/def/def/defiMisc.hpp"
    "/home/msears/AIEplace/cpp/Limbo/limbo/thirdparty/lefdef/5.8/def/def/defiPath.hpp"
    "/home/msears/AIEplace/cpp/Limbo/limbo/thirdparty/lefdef/5.8/def/def/defiPropType.hpp"
    "/home/msears/AIEplace/cpp/Limbo/limbo/thirdparty/lefdef/5.8/def/def/defiSite.hpp"
    "/home/msears/AIEplace/cpp/Limbo/limbo/thirdparty/lefdef/5.8/def/def/defiUtil.hpp"
    "/home/msears/AIEplace/cpp/Limbo/limbo/thirdparty/lefdef/5.8/def/def/defrReader.hpp"
    )
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "/home/msears/AIEplace/cpp/Limbo/build/limbo/thirdparty/lefdef/5.8/def/libcdef.a")
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "/home/msears/AIEplace/cpp/Limbo/build/limbo/thirdparty/lefdef/5.8/def/libdefzlib.a")
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "/home/msears/AIEplace/cpp/Limbo/build/limbo/thirdparty/lefdef/5.8/def/libcdefzlib.a")
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/bin/thirdparty/lefdef/5.8/def/defrw" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/bin/thirdparty/lefdef/5.8/def/defrw")
    file(RPATH_CHECK
         FILE "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/bin/thirdparty/lefdef/5.8/def/defrw"
         RPATH "")
  endif()
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/bin/thirdparty/lefdef/5.8/def" TYPE EXECUTABLE FILES "/home/msears/AIEplace/cpp/Limbo/build/limbo/thirdparty/lefdef/5.8/def/bin/defrw")
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/bin/thirdparty/lefdef/5.8/def/defrw" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/bin/thirdparty/lefdef/5.8/def/defrw")
    if(CMAKE_INSTALL_DO_STRIP)
      execute_process(COMMAND "/usr/bin/strip" "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/bin/thirdparty/lefdef/5.8/def/defrw")
    endif()
  endif()
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/bin/thirdparty/lefdef/5.8/def/defdiff" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/bin/thirdparty/lefdef/5.8/def/defdiff")
    file(RPATH_CHECK
         FILE "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/bin/thirdparty/lefdef/5.8/def/defdiff"
         RPATH "")
  endif()
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/bin/thirdparty/lefdef/5.8/def" TYPE EXECUTABLE FILES "/home/msears/AIEplace/cpp/Limbo/build/limbo/thirdparty/lefdef/5.8/def/bin/defdiff")
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/bin/thirdparty/lefdef/5.8/def/defdiff" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/bin/thirdparty/lefdef/5.8/def/defdiff")
    if(CMAKE_INSTALL_DO_STRIP)
      execute_process(COMMAND "/usr/bin/strip" "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/bin/thirdparty/lefdef/5.8/def/defdiff")
    endif()
  endif()
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/bin/thirdparty/lefdef/5.8/def/defwrite" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/bin/thirdparty/lefdef/5.8/def/defwrite")
    file(RPATH_CHECK
         FILE "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/bin/thirdparty/lefdef/5.8/def/defwrite"
         RPATH "")
  endif()
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/bin/thirdparty/lefdef/5.8/def" TYPE EXECUTABLE FILES "/home/msears/AIEplace/cpp/Limbo/build/limbo/thirdparty/lefdef/5.8/def/bin/defwrite")
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/bin/thirdparty/lefdef/5.8/def/defwrite" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/bin/thirdparty/lefdef/5.8/def/defwrite")
    if(CMAKE_INSTALL_DO_STRIP)
      execute_process(COMMAND "/usr/bin/strip" "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/bin/thirdparty/lefdef/5.8/def/defwrite")
    endif()
  endif()
endif()

