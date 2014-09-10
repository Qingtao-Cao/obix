				oBIX-Adaptors version 1.0
 
Copyright (c) 2013-2014 Qingtao Cao [harry.cao@nextdc.com]    

oBIX-adaptors is free software licensed under GPLv3 License.
Text of the license can be found in the COPYING file.

This file contains a description of the project in the following sections:

1. Build Instructions
2. Package Contents


--1-- Build Instructions

The package has the following library dependencies:
 - libcurl
 - libcsv
 - libxml2
 - libmodbus-devel
 - kernel-devel
 - ONEDC/obix.git

Note, the oBIX client side APIs have a tight connection with the server implementation and therefore they are provided by the oBIX server package that can be downloaded by below command:

	$ git clone https://github.com/ONEDC/obix.git

Follow below commands to build the oBIX-adaptors package properly:

	$ cd /work
	$ mkdir install
	$ git clone https://github.com/ONEDC/obix-adaptors

	$ mkdir -p /work/build/obix-adaptors

	$ cd /work/obix-adaptors
	$ autoreconf -i

	$ cd /work/build/obix-adaptors
	$ /work/obix-adaptors/configure --prefix=/work/install
	$ make install

Then all binaries will be installed under various folders in /work/install.


--2-- Package Contents

Below is a short description of the main files in the package.

README		This file. Contains project description.

COPYING		Contains licensing terms for the project.
 
src/		Folder with project source files.
 
	./adaptors/		Source code of various oBIX adaptors
 
	./tools/		Some useful simple programs to aid development or testing.
    
res/		Various configuration files for different adaptors

