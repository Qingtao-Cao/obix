# oBIX tests


## Pre-requisites

You need obix-server and lighttpd installed and configured. Either via rpm installation or the make install target.

## Automatically running the tests

Run the tests automatically via CMake:

1. Enable CMakeTests in CMakeLists.txt

~~~~
ENABLE_TESTING()

~~~~
~~~~
$ cmake .
$ make
$ make test
~~~~

    Currently, this starts the server on localhost:4242.

## Manually running the tests

1. Run tests/scripts/setup.sh to update paths in config files
2. Start the server: lighttpd -D -f tests/lighttpd/lighttpd.conf
3. First run? Add example devices: tests/scripts/signUp
4. Run any individual script you want.

## TODO

1. Get autotests to do something more than just start the server :)
1. Get rid of the abs path
