# ONEDC oBIX Libraries

This repository contains the library components of the ONEDC oBIX platform.

# Package contents

Below is a short description of the main files in the package:

* README.md

    This file. Contrains project description.
* COPYING

    Contains licensing terms for the project.

* src/

    Folder with project source files.

* pkgconfig/

    Folder with libobix package config input file.


# Coding Style

The coding style adopted by Linux kernel source code is encouraged and
should be used as much as possible. It can be found at:

    <Linux kernel source>/Documentation/CodingStyle

Generally speaking, good codes speak for themselves instead of using
verbose, distracting long variable names.

The one deviation from the Linux kernel coding style is the size of the
table key. As consistent with the value used in the original CoT package
the size of the tab key is set to four instead of eight.

Following content can be put in the ~/.vimrc to this end:

	set ts=4
	set incsearch
	set hlsearch
	set showmatch
	let c_space_errors=1
	syntax enable
	syntax on

In particular, enabling "c_space_errors" option helps to highlight any
white space errors.

# Build Instructions

To build the obix-libs source code:

    $ cd <path to oBIX library source>
    $ cmake .
    $ make

To install obix-libs headers and objects:

    $ cd <path to oBIX library source>
    $ cmake .
    $ make
    $ sudo make install

This will install: 

* header files into /usr/include
* library files into /usr/{lib/lib64} (depending on arch)

To change the installation path prefix (default is /usr):

* Permanent: Edit CMAKE_INSTALL_PREFIX in the root level CMakeLists.txt, and rebuild.

* Temporary: Set the CMAKE_INSTALL_PREFIX on the command line. This will be saved in CMakeCache.txt. 

    $ cmake -DCMAKE_INSTALL_PREFIX="/usr/local"
    

Note: CMake does not have a make clean target. There is an oBIX make obix-clean target that performs a similar function. Any time you make changes to CMakeLists.txt you will need to make obix-clean or manually remove CMakeCache.txt and the build directory.

