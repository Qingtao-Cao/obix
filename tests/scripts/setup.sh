#!/bin/bash

# Create dir structure for obix fastcgi server
if [ ! -d tests/tests ]
then
	mkdir -p tests/tests
fi

# Create a history directory for server
if [ ! -d tests/res/server/histories/ ]
then
	mkdir -p tests/res/server/histories/
fi

# Check the server has been built
if [ -f build/server/obix-fcgi ]
then
	cp -f build/server/obix-fcgi tests/tests/obix-fcgi
else
	echo "Cannot find obix-fcgi. Exiting."
	exit 1
fi

# Create dir structure for lighttpd
mkdir -p tests/lighttpd/log tests/lighttpd/run tests/lighttpd/lib tests/lighttpd/lib
