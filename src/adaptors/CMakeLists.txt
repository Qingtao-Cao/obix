#
# Copyright NEXTDC LTD. 2014
# 

# oBIX libs and client directory
INCLUDE_DIRECTORIES(${CMAKE_CURRENT_SOURCE_DIR}/../libs)
INCLUDE_DIRECTORIES(${CMAKE_CURRENT_SOURCE_DIR}/../client)

set (CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../cmake")

#
# libcsv-devel, libcurl-devel and libmodbus-devel are required for
# the adaptors directory to compile properly, so cmake should die if
# they cannot be found.
set(CSV_FIND_REQUIRED yes)
set(CURL_FIND_REQUIRED yes)
set(MODBUS_FIND_REQUIRED yes)

include(FindMODBUS)
IF (MODBUS_FOUND)
	INCLUDE_DIRECTORIES(${MODBUS_INCLUDE_DIR})
ENDIF (MODBUS_FOUND)

include(FindCSV)
IF (CSV_FOUND)
	INCLUDE_DIRECTORIES(${CSV_INCLUDE_DIR})
ENDIF (CSV_FOUND)

include(FindCURL)
IF (CURL_FOUND)
	INCLUDE_DIRECTORIES(${CURL_INCLUDE_DIR})
ENDIF (CURL_FOUND)

FIND_PACKAGE(LibXml2 REQUIRED)
IF (LIBXML2_FOUND)
    INCLUDE_DIRECTORIES(${LIBXML2_INCLUDE_DIR})
    SET (LIBS ${LIBS} ${LIBXML2_LIBRARIES})
ENDIF (LIBXML2_FOUND)

# Standard build flags
SET (CMAKE_C_FLAGS "-g -Wall -Werror")

# Debug build flags. cmake -DCMAKE_BUILD_TYPE=Debug .
SET (CMAKE_C_FLAGS_DEBUG "-g -Wall -Werror -DDEBUG -DDEBUG_VALGRIND -DDEBUG_CACHE")

# Location to install header files
SET(INCLUDE_DIR "${CMAKE_INSTALL_PREFIX}/include")

ADD_EXECUTABLE(example_adaptor example_adaptor.c)
TARGET_LINK_LIBRARIES(example_adaptor xml2 curl pthread libobix-common libobix-client ${LIBS})
INSTALL(TARGETS example_adaptor DESTINATION "${CMAKE_INSTALL_PREFIX}/bin")

ADD_EXECUTABLE(mg_adaptor mg_adaptor.c)
TARGET_LINK_LIBRARIES(mg_adaptor modbus xml2 curl pthread libobix-common libobix-client ${LIBS})
INSTALL(TARGETS mg_adaptor DESTINATION "${CMAKE_INSTALL_PREFIX}/bin")

ADD_EXECUTABLE(bms_adaptor csv_ext.c bms_adaptor.c)
TARGET_LINK_LIBRARIES(bms_adaptor csv xml2 curl pthread libobix-common libobix-client ${LIBS})
INSTALL(TARGETS bms_adaptor DESTINATION "${CMAKE_INSTALL_PREFIX}/bin")

# Install header files
FILE(GLOB HEADER_FILES "*.h")
INSTALL(FILES ${HEADER_FILES}
    DESTINATION "${INCLUDE_DIR}/obix")
