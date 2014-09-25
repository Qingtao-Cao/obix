#
# Copyright NEXTDC LTD. 2014
# 

# oBIX libs directory
INCLUDE_DIRECTORIES(${CMAKE_CURRENT_SOURCE_DIR}/../libs)

FIND_PACKAGE(LibXml2 REQUIRED)
IF (LIBXML2_FOUND)
    INCLUDE_DIRECTORIES(${LIBXML2_INCLUDE_DIR})
    SET (LIBS ${LIBS} ${LIBXML2_LIBRARIES})
ENDIF (LIBXML2_FOUND)

set (CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../cmake")

# Standard build flags
SET (CMAKE_C_FLAGS "-g -Wall -Werror")

# Debug build flags. cmake -DCMAKE_BUILD_TYPE=Debug .
SET (CMAKE_C_FLAGS_DEBUG "-g -Wall -Werror -DDEBUG -DDEBUG_REFCNT -DDEBUG_VALGRIND -DDEBUG_CACHE")

# Location to install header files
SET(INCLUDE_DIR "${CMAKE_INSTALL_PREFIX}/include")

ADD_EXECUTABLE(obix-fcgi batch.c history.c obix_fcgi.c obix_request.c server.c watch.c xml_storage.c)

TARGET_LINK_LIBRARIES(obix-fcgi fcgi rt pthread libobix-common ${LIBS})

# Install binary 
INSTALL(TARGETS obix-fcgi DESTINATION "${CMAKE_INSTALL_PREFIX}/bin")

# Install header files
FILE(GLOB HEADER_FILES "*.h")
INSTALL(FILES ${HEADER_FILES}
    DESTINATION "${INCLUDE_DIR}/obix")
    
