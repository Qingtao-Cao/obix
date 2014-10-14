#
# Copyright NEXTDC LTD. 2014
# 

CMAKE_MINIMUM_REQUIRED(VERSION 2.6.2)

# CMP0011 should be set here, otherwise policy set in module won't affect
# here.
CMAKE_POLICY(SET CMP0011 OLD)

# Default CMAKE_INSTALL_PREFIX should be set before PROJECT()
SET(CMAKE_INSTALL_PREFIX "/usr" CACHE PATH "Install dir prefix")

# Default doc directory
IF(NOT PROJECT_DOC_DIR)
    SET(PROJECT_DOC_DIR "/usr/share/doc" CACHE PATH "Install dir for docs")
ENDIF(NOT PROJECT_DOC_DIR)

IF(NOT PROJECT_DOC_DIR_SUFFIX)
    SET(PROJECT_DOC_DIR_SUFFIX "obix-doc")
ENDIF(NOT PROJECT_DOC_DIR_SUFFIX)    

# Location of history files.
IF(NOT OBIX_VAR_LIB_DIR)
    SET(OBIX_VAR_LIB_DIR "/var/lib/obix" CACHE PATH "Var lib directory path")
ENDIF(NOT OBIX_VAR_LIB_DIR)

# Location of history files.
IF(NOT OBIX_HISTORY_FILES_DIR)
    SET(OBIX_HISTORY_FILES_DIR "${OBIX_VAR_LIB_DIR}/histories" CACHE PATH "Histories directory path")
ENDIF(NOT OBIX_HISTORY_FILES_DIR)

# Location of lighttpd conf directory.
IF(NOT LIGHTTPD_CONF_DIR)
    SET(LIGHTTPD_CONF_DIR "/etc/lighttpd/conf.d/" CACHE PATH "Lighttpd config directory path")
ENDIF(NOT LIGHTTPD_CONF_DIR)

# Location of obix config files
IF(NOT OBIX_ETC_DIR)
    SET(OBIX_ETC_DIR "/etc/obix" CACHE PATH "Location of oBIX config files")
ENDIF(NOT OBIX_ETC_DIR)

# Location of resources directory
IF(NOT OBIX_ETC_RES_DIR)
    SET(OBIX_ETC_RES_DIR "${OBIX_ETC_DIR}/res" CACHE PATH "Location of resources directory")
ENDIF(NOT OBIX_ETC_RES_DIR)

# Server file within resources
IF(NOT OBIX_ETC_SERVER_DIR)
    SET(OBIX_ETC_SERVER_DIR "${OBIX_ETC_RES_DIR}/server" CACHE PATH "Server file within resource")
ENDIF(NOT OBIX_ETC_SERVER_DIR)


# Compiler 
SET(CMAKE_C_COMPILER "gcc")


######################################
# Project details
#
PROJECT(oBIX)
SET(PRJ_SUMMARY "ONEDC oBIX")

SET(PRJ_DESCRIPTION
        "An open source project derived from the C oBIX Tools (CoT) project, an open source project dedicated to the development of embedded Building Automation solutions based on oBIX standard (http://www.obix.org). "
   )
SET(LICENSE "GPLv3+")


######################################
# Create an out-of-source build directory.
# Keeps source trees clean.
#
SET (CMAKE_BUILD_DIR "build")
ADD_SUBDIRECTORY(src ${CMAKE_BUILD_DIR})
ADD_SUBDIRECTORY(docs ${CMAKE_BUILD_DIR}/docs)

######################################
# Compiler flags 
#
SET (CMAKE_C_FLAGS "-Wall -Werror -O4 -std=c99")
SET (CMAKE_C_FLAGS_DEBUG "-g -Wall -Werror -DDEBUG -DDEBUG_REFCNT -DDEBUG_VALGRIND -DDEBUG_CACHE")
SET (CMAKE_C_FLAGS_MINSIZEREL  "-Os -DNDEBUG -Werror")
SET (CMAKE_C_FLAGS_RELEASE  "-O4 -Werror -DNDEBUG")
SET (CMAKE_C_FLAGS_RELWITHDEBINFO "-O2 -g -Werror")
SET (CMAKE_LINKER "/usr/bin/ld")


######################################
# Install the res directory structure into /etc/obix
#
FILE(MAKE_DIRECTORY "${CMAKE_BUILD_DIR}/obix")
FILE(MAKE_DIRECTORY "${CMAKE_BUILD_DIR}/lighttpd/conf.d")
INSTALL(DIRECTORY "${CMAKE_BUILD_DIR}/obix"
    DESTINATION "/etc")
INSTALL(DIRECTORY "${CMAKE_BUILD_DIR}/lighttpd"
    DESTINATION "/etc")
INSTALL(DIRECTORY "res"
    DESTINATION "${OBIX_ETC_DIR}")


######################################
# Link obix-fcgi.conf to /etc/lighttpd/conf.d/
INSTALL(CODE "
        EXECUTE_PROCESS(COMMAND ln -s ${OBIX_ETC_DIR}/res/obix-fcgi.conf ${LIGHTTPD_CONF_DIR})
        ")

######################################
# Install docs
#
SET(INSTALL_DOCS README.md COPYING)
INSTALL(FILES ${INSTALL_DOCS}
 DESTINATION "${PROJECT_DOC_DIR}/${PROJECT_DOC_DIR_SUFFIX}")

    
######################################
# Create /var/lib/obix/histories
#
FILE(MAKE_DIRECTORY "${CMAKE_BUILD_DIR}/histories")
INSTALL(DIRECTORY "${CMAKE_BUILD_DIR}/histories"
    DESTINATION "${OBIX_VAR_LIB_DIR}")

# chown then sym link /var/lib/obix/histories to /etc/obix/res/server
IF(CMAKE_HOST_UNIX)
    INSTALL(CODE "
        EXECUTE_PROCESS(COMMAND chown -R lighttpd:lighttpd ${OBIX_HISTORY_FILES_DIR})
        ")
    INSTALL(CODE "
        EXECUTE_PROCESS(COMMAND ln -s ${OBIX_HISTORY_FILES_DIR} ${OBIX_ETC_SERVER_DIR})
        ")
ENDIF(CMAKE_HOST_UNIX)


######################################
# make obix-clean
#
ADD_CUSTOM_TARGET(obix-clean
    COMMENT "Removing: build/, CMakeCache.txt, CMakeFiles/"
    COMMAND rm -rf ${CMAKE_BUILD_DIR} CMakeCache.txt CMakeFiles)


######################################
# Test Suites.
#
# TODO: add further testing using existing tests/scripts
#
SET(CMAKE_TESTS_DIR tests/scripts)
ENABLE_TESTING()
ADD_TEST("Dependencies" ${CMAKE_TESTS_DIR}/dependencies.sh)
ADD_TEST("Setup"        ${CMAKE_TESTS_DIR}/setup.sh)
ADD_TEST("Server_Start" ${CMAKE_TESTS_DIR}/server.sh start)

file(GLOB files "${CMAKE_TESTS_DIR}/unit-tests/*.test")
foreach(file ${files})
	execute_process(COMMAND "basename" "${file}" OUTPUT_VARIABLE file_name)
	add_test("${file_name}" "${file}" "${CMAKE_TESTS_DIR}")
endforeach()

ADD_TEST("Server_Stop"  ${CMAKE_TESTS_DIR}/server.sh stop)
