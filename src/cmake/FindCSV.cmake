# - Try to find libcsv
#
# Once done this will define
#
#  CSV_FOUND - system has CSV
#  CSV_INCLUDE_DIR - the CSV include directory
#  CSV_LIBRARIES - Link these to use CSV

# Copyright (c) 2006, Jasem Mutlaq <mutlaqja@ikarustech.com>
# Based on FindLibfacile by Carsten Niehaus, <cniehaus@gmx.de>
#
# Redistribution and use is allowed according to the terms of the BSD license.
# For details see the accompanying COPYING-CMAKE-SCRIPTS file.

if (CSV_INCLUDE_DIR AND CSV_LIBRARIES)

  # in cache already
  set(CSV_FOUND TRUE)
  message(STATUS "Found libcsv: ${CSV_LIBRARIES}")

else (CSV_INCLUDE_DIR AND CSV_LIBRARIES)

  find_path(CSV_INCLUDE_DIR csv.h
    PATH_SUFFIXES csv
    ${_obIncDir}
    ${GNUWIN32_DIR}/include
  )

  find_library(CSV_LIBRARIES NAMES csv
    PATHS
    ${_obLinkDir}
    ${GNUWIN32_DIR}/lib
  )

 set(CMAKE_REQUIRED_INCLUDES ${CSV_INCLUDE_DIR})
 set(CMAKE_REQUIRED_LIBRARIES ${CSV_LIBRARIES})

  if(CSV_INCLUDE_DIR AND CSV_LIBRARIES)
    set(CSV_FOUND TRUE)
  else (CSV_INCLUDE_DIR AND CSV_LIBRARIES)
    set(CSV_FOUND FALSE)
  endif(CSV_INCLUDE_DIR AND CSV_LIBRARIES)

  if (CSV_FOUND)
    if (NOT CSV_FIND_QUIETLY)
      message(STATUS "Found libcsv: ${CSV_LIBRARIES}")
    endif (NOT CSV_FIND_QUIETLY)
  else (CSV_FOUND)
    if (CSV_FIND_REQUIRED)
      message(FATAL_ERROR "libcsv not found. Please install libcsv-devel.")
    endif (CSV_FIND_REQUIRED)
  endif (CSV_FOUND)

  mark_as_advanced(CSV_INCLUDE_DIR CSV_LIBRARIES)
  
endif (CSV_INCLUDE_DIR AND CSV_LIBRARIES)
