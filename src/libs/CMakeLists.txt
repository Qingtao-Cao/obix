#
# Copyright NEXTDC LTD. 2014
# 

# Provides access to CMAKE_INSTALL_LIBDIR -> lib or lib64
# requires cmake >= 2.8, therefore not in RHEL6
#INCLUDE(GNUInstallDirs)

# Location for lib files. For lib64 use -DLIB_SUFFIX="/usr/lib64"
IF(NOT LIB_DIR)
    SET(LIB_DIR "${CMAKE_INSTALL_PREFIX}/lib" CACHE PATH "Library path")
ENDIF(NOT LIB_DIR)

# Default doc directory
IF(NOT PROJECT_DOC_DIR)
    SET(PROJECT_DOC_DIR "/usr/share/doc" CACHE PATH "Install dir for docs")
ENDIF(NOT PROJECT_DOC_DIR)
    
IF(NOT PROJECT_DOC_DIR_SUFFIX)
    SET(PROJECT_DOC_DIR_SUFFIX "obix-libs" CACHE STRING "Dir suffix for doc install. Can be set to obix-$version via -D")
ENDIF(NOT PROJECT_DOC_DIR_SUFFIX)

FIND_PACKAGE(LibXml2 REQUIRED)
IF(LIBXML2_FOUND)
    INCLUDE_DIRECTORIES(${LIBXML2_INCLUDE_DIR})
    SET (LIBS ${LIBS} ${LIBXML2_LIBRARIES})
ENDIF(LIBXML2_FOUND)

# Standard build flags
SET (CMAKE_C_FLAGS "-g -Wall -Werror")

# Debug build flags. cmake -DCMAKE_BUILD_TYPE=Debug .
SET (CMAKE_C_FLAGS_DEBUG "-g -Wall -Werror -DDEBUG -DDEBUG_REFCNT -DDEBUG_VALGRIND -DDEBUG_CACHE")

SET(INCLUDE_DIR "${CMAKE_INSTALL_PREFIX}/include")

FILE(GLOB OBIX_LIB_FILES *.c)
ADD_LIBRARY(obix SHARED ${OBIX_LIB_FILES})
ADD_LIBRARY(obix-static STATIC ${OBIX_LIB_FILES})

# The library target "obix-static" has a default OUTPUT_NAME of "obix-static".
# Changing it to "obix". This will give us libobix.a
SET_TARGET_PROPERTIES(obix-static PROPERTIES OUTPUT_NAME "obix")

# Install .a and .so into /usr/lib. 
INSTALL(TARGETS obix DESTINATION "${LIB_DIR}")
INSTALL(TARGETS obix-static DESTINATION "${LIB_DIR}")

# Install header files
FILE(GLOB HEADER_FILES "*.h")
INSTALL(FILES ${HEADER_FILES}
    DESTINATION "${INCLUDE_DIR}/obix")

# Install the docs
SET(INSTALL_DOCS README.md)
INSTALL(FILES ${INSTALL_DOCS}
   DESTINATION "${PROJECT_DOC_DIR}/${PROJECT_DOC_DIR_SUFFIX}")
 
# Ensure the two libs don't clobber each other during the build.
SET_TARGET_PROPERTIES(obix PROPERTIES CLEAN_DIRECT_OUTPUT 1)
SET_TARGET_PROPERTIES(obix-static PROPERTIES CLEAN_DIRECT_OUTPUT 1)

