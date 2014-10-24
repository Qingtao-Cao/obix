# - Try to find libcurl
#
# Once done this will define
#
#  CURL_FOUND - system has CURL
#  CURL_INCLUDE_DIR - the CURL include directory
#  CURL_LIBRARIES - Link these to use CURL

# Copyright (c) 2006, Jasem Mutlaq <mutlaqja@ikarustech.com>
# Based on FindLibfacile by Carsten Niehaus, <cniehaus@gmx.de>
#
# Redistribution and use is allowed according to the terms of the BSD license.
# For details see the accompanying COPYING-CMAKE-SCRIPTS file.

if (CURL_INCLUDE_DIR AND CURL_LIBRARIES)

  # in cache already
  set(CURL_FOUND TRUE)
  message(STATUS "Found libcurl: ${CURL_LIBRARIES}")

else (CURL_INCLUDE_DIR AND CURL_LIBRARIES)

  find_path(CURL_INCLUDE_DIR curl.h
    PATH_SUFFIXES curl
    ${_obIncDir}
    ${GNUWIN32_DIR}/include
  )

  find_library(CURL_LIBRARIES NAMES curl
    PATHS
    ${_obLinkDir}
    ${GNUWIN32_DIR}/lib
  )

 set(CMAKE_REQUIRED_INCLUDES ${CURL_INCLUDE_DIR})
 set(CMAKE_REQUIRED_LIBRARIES ${CURL_LIBRARIES})

  if(CURL_INCLUDE_DIR AND CURL_LIBRARIES)
    set(CURL_FOUND TRUE)
  else (CURL_INCLUDE_DIR AND CURL_LIBRARIES)
    set(CURL_FOUND FALSE)
  endif(CURL_INCLUDE_DIR AND CURL_LIBRARIES)

  if (CURL_FOUND)
    if (NOT CURL_FIND_QUIETLY)
      message(STATUS "Found libcurl: ${CURL_LIBRARIES}")
    endif (NOT CURL_FIND_QUIETLY)
  else (CURL_FOUND)
    if (CURL_FIND_REQUIRED)
      message(FATAL_ERROR "libcurl not found. Please install libcurl-devel.")
    endif (CURL_FIND_REQUIRED)
  endif (CURL_FOUND)

  mark_as_advanced(CURL_INCLUDE_DIR CURL_LIBRARIES)
  
endif (CURL_INCLUDE_DIR AND CURL_LIBRARIES)
