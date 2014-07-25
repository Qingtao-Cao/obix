# Pull in libs and server
ADD_SUBDIRECTORY(libs libs)
ADD_SUBDIRECTORY(server server)

# Dummy binary to keep rpmlint from complaining about obix rpm having no binary.
ADD_EXECUTABLE(obix-echo obix-echo.c)
INSTALL(TARGETS obix-echo DESTINATION "${CMAKE_INSTALL_PREFIX}/bin")

