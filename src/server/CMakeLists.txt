#
# Copyright NEXTDC LTD. 2014
# 

# oBIX libs directory
INCLUDE_DIRECTORIES(${CMAKE_CURRENT_SOURCE_DIR}/../libs)

# Default doc directory
IF(NOT PROJECT_DOC_DIR)
    SET(PROJECT_DOC_DIR "/usr/share/doc" CACHE PATH "Install dir for docs")
ENDIF(NOT PROJECT_DOC_DIR)
    
IF(NOT PROJECT_DOC_DIR_SUFFIX)
    SET(PROJECT_DOC_DIR_SUFFIX "obix-server" CACHE STRING "Dir suffix for doc install. Can be set to obix-$version via -D")
ENDIF(NOT PROJECT_DOC_DIR_SUFFIX)

FIND_PACKAGE(LibXml2 REQUIRED)
IF (LIBXML2_FOUND)
    INCLUDE_DIRECTORIES(${LIBXML2_INCLUDE_DIR})
    SET (LIBS ${LIBS} ${LIBXML2_LIBRARIES})
ENDIF (LIBXML2_FOUND)

# Standard build flags
SET (CMAKE_C_FLAGS_DEBUG "-g -Wall")

# Debug build flags. cmake -DCMAKE_BUILD_TYPE=Debug .
SET (CMAKE_C_FLAGS_DEBUG "-g -Wall -DDEBUG -DDEBUG_REFCNT -DDEBUG_VALGRIND -DDEBUG_CACHE")

# Location to install header files
SET(INCLUDE_DIR "${CMAKE_INSTALL_PREFIX}/include")

ADD_EXECUTABLE(obix-fcgi batch.c history.c libxml_config.c obix_fcgi.c post_handler.c response.c server.c watch.c xml_storage.c)

TARGET_LINK_LIBRARIES(obix-fcgi fcgi rt pthread libobix ${LIBS})

# Install binary 
INSTALL(TARGETS obix-fcgi DESTINATION "${CMAKE_INSTALL_PREFIX}/bin")

# Install header files
FILE(GLOB HEADER_FILES "*.h")
INSTALL(FILES ${HEADER_FILES}
    DESTINATION "${INCLUDE_DIR}/obix")
    
# Install the docs
# SET(INSTALL_DOCS README.md)
# INSTALL(FILES ${INSTALL_DOCS}
#  DESTINATION "${PROJECT_DOC_DIR}/${PROJECT_DOC_DIR_SUFFIX}")
