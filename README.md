# oBIX Server

Providing oBIX Server implementation for C language
 
Copyright (c) 2014 Tyler Watson [tyler.watson@nextdc.com]    
Copyright (c) 2013-2014 Qingtao Cao [harry.cao@nextdc.com]    
Copyright (c) 2009 Andrey Litvinov [litvinov.andrey@gmail.com]
 
oBIX Server is free software licensed under the GPLv3+ License. The text for this license can be found in the COPYING file.

# 1. Project Overview

oBIX Server is an open source project derived from the C oBIX Tools (CoT) project, an open source project dedicated to the development of embedded Building Automation solutions based on oBIX standard (http://www.obix.org). 

The whole project is written in C and has tiny resource requirements. It allows the project to run on embedded Linux platforms, such as OpenWrt (http://openwrt.org).

This package only contains the implementation of oBIX Server. The common library that can be shared among oBIX Server and oBIX client is in a separate package.

All client software such as libraries and adapter programs are still using old XML parser provided by libupnp whereas the server implementation has adopted the new XML parser from libxml2, they are organised and distributed as separate packages.

## 1.1 C oBIX Server
 
C oBIX Server is a stand-alone application intended for storing building automation data from various sources. It is a core for automation systems, providing a unified Web services interface to control connected devices (security sensors, heating / ventilation systems, lights, switches, etc.). 

Devices are connected to the server through oBIX adapters. These adapters are separate applications, which convert device-specific protocol into oBIX messages. The server provides the same oBIX interface for devices (device adapters) and UI management applications. 

The main difference between this oBIX Server and other available implementations (such as oFMS http://www.stok.fi/eng/ofms/index.html or oX http://obix-server.sourceforge.net) is it is written in C and can be run on cheap low-resource platforms and more importantly, faster.

The list of currently implemented oBIX features includes:
 - Read, write and invoke requests handling
 - Lobby object
 - Batch operation
 - HTTP protocol binding
 - WatchService (see docs/WATCH.md)
 - Histories (see docs/HISTORY.md).

The list of things that are NOT yet supported:
 - Alarms
 - Feeds
 - Writable points (simple writable objects can be used instead)
 - Permission based degradation
 - Localisation
 - SOAP binding.

If a feature doesn’t appear in either of the lists above, it is probably not implemented. In addition to standard oBIX functionality, the server has the following additional features:
 - SignUp operation, accessible from the Lobby object. It allows clients (device adapters) to publish new data to the server
 - Device lobby, showing all devices registered to the server
 - Long polling mode for Watch objects. This mode allows clients to reduce the number of poll requests and at the same time, receive immediate updates from the server. Additional information about this feature can be found
  at http://c-obix-tools.googlecode.com/files/long_polling.pdf.

# 2. System Requirements
 
This project has been created for running on Linux platforms. It was tested on various platforms, including embedded devices with OpenWrt (http://openwrt.org) installed. An example of a tested embedded platform is Asus WL-500g Premium V2 router (32 MB of RAM and 240 MHz CPU). Any other device capable of running OpenWrt can be used instead.

Other Linux distributions for embedded devices were not tested but may possibly be used if all project dependencies are satisfied.

The project has the following libraries or packages dependencies:
 - kernel-devel
 - fcgi-devel
 - libxml2-devel
 - glibc-devel
 - gcc
 - mock
 - cmake

oBIX Server is implemented as a FastCGI (http://www.fastcgi.com) application and requires a Web server with FastCGI support. It has been tested with Lighttpd (http://www.lighttpd.net/), but theoretically can be used with any other Web server. The only requirement is the chosen Web server supports the FastCGI multiplexing feature (the ability to handle multiple requests simultaneously through one FastCGI connection).

OpenWrt SDK is required in order to cross-compile the project for OpenWrt platform. Further instructions on cross-compiling can be found at ./res/OpenWrt-SDK/README file. Precompiled binaries for OpenWrt 7.09 are available from the Download section of the project home page (http://code.google.com/p/c-obix-tools/).
 
# 3. Package Contents
 
Below is a short description of the main files in the package:

* README.md

    This file. Contains project description.

* COPYING

    Contains licensing terms for the project.
 
* src/

    * docs/ - Further documentation.
    * libs/ - Source for oBIX library.
    * server/ - Source for oBIX server.
	* tools/ - Some useful simple programs to aid in development or testing.

* res/

    Resources folder. Various configuration files. See below.

* tests/

    * Setup required for make test.
    * lighttpd/ - A self contained lighttpd instance for testing oBIX.
    * res/ - A copy of the resource configuration files for testing oBIX.
    * scripts/ - Various shell scripts used for testing oBIX.
    * tests/ - Location for the obix-fgci executable used for testing oBIX. Duplicate directory due to bug in way lighttpd deals with paths.

## 3.1 Layout of Resource Folder

	res/
	├── obix-fcgi.conf
	├── OpenWrt-SDK
	│  	└── README
	└── server
    	├── core
	    │   ├── server_lobby.xml
    	│   └── server_sys_objects.xml
	    ├── devices
    	│   └── server_test_device.xml
	    ├── histories
	    ├── server_config.xml
    	└── sys
        	├── server_about.xml
	        ├── server_def.xml
    	    ├── server_devices.xml
        	├── server_history.xml
	        └── server_watch.xml

* obix-fcgi.conf 

    Provides the required FCGI settings of oBIX Server for lighttpd. For further details refer to Section 5.

* server/server_config.xml

    The main configuration file of the oBIX Server. Static server configuration files will be loaded in sequence:

        core -> sys -> devices

* server/devices/

    In the future, the oBIX Server is likely to support 'Persistent Device' and will have device contracts saved as separate files in this folder on the hard drive and loaded into the global XML database at start-up.

* histories/

    Contains history facilities for various devices. Each device will have its own separate sub-folder that contains an index file and a number of raw history data files named by the date when they are generated. For details see docs/HISTORY.md

Note: This folder does not exist in the source code and is created at installation. For the sake of hard disk performance a standalone hard disk had better be mounted onto this folder because history facilities are likely to consume a vast amount of space during a long period of time.

# 4. Build Instructions

To build the obix-server:

	$ cd <path to oBIX server source>
	$ cmake -DCMAKE_BUILD_TYPE=Release .
	$ VERBOSE=1 make

This creates an "out-of-source" build with all output, including the obix-fcgi binary and libobix libraries, found in the build directory.

**Building on 2.x kernel**

The 2.x kernel requires an additional kernel headers directory in FindKernelHeaders.cmake.
Uncomment the KERNELHEADERS_DIR line in src/server/cmake/FindKernelHeaders.cmake and src/libs/cmake/FindKernelHeaders.cmake

```
# Only needed on 2.x kernels
#  ${KERNELHEADERS_DIR}/arch/x86/include/asm
```


**Debug build**

To enable the DEBUG macro and associated tools, specify CMAKE_BUILD_TYPE to "Debug":

	$ cmake -DCMAKE_BUILD_TYPE=Debug .

Note: CMAKE_C_FLAGS_DEBUG defined in the top level CMakeList.txt seems unable to take effect in the subfolders. To workaround this, explicitly define it in the CMakeList.txt file in src/server/ folder.

**Make install build**

The make install target copies the executable, libraries, headers, and res directory.

    $ sudo make install
    
The default install path for the libraries is /usr/lib. To change this to /usr/lib64, set the LIB_SUFFIX option on the command line:

    $ cmake -DLIB_SUFFIX="64" .
    $ make
    $ sudo make install

The default install path for documentation is /usr/share/doc/obix. To change this to /usr/share/doc/obix-$version, set the PROJECT_DOC_DIR_SUFFIX:

    $ cmake -D-DPROJECT_DOC_DIR_SUFFIX
    $ make
    $ sudo make install
    
See redhat/obix.spec for further details.



**RPM build**

Fedora/RHEL systems can use the specfile redhat/obix.spec, and create a local tarball from git. In this example there is a local git tag named "1.0":

    git archive --format=tar --prefix=obix-1.0/ 1.0 | gzip >obix-1.0.tar.gz

# 5. Running oBIX Server

oBIX Server is implemented as a FastCGI script which can be executed by any HTTP server with FCGI support.

## 5.1. Self-contained lighttpd using make test

The **make test** target invokes a self-contained lighttpd instance that runs on port 4242 and therefore does not require root access to run. This removes the need to configure a standalone instance of lighttpd and allows for automated testing.

Note: This use case is ideal for development, but should is not suitable for production.

To start a local lighttpd instance of oBIX:

    $ cd <path to oBIX server source>
    $ cmake .
    $ make
    $ make test

This will start a local instance of lighttpd. You can access the server via: localhost:4242/obix (there is a redirect in place from localhost:4242 to /obix).

You can run any of the scripts in the directory **tests/scripts**.

Note: The port 4242 must be removed from all scripts when used in the case of Standalone Lighttpd Instance as in production.

## 5.2. Standalone Lighttpd Instance
 
This section describes how to use a standalone instance of lighttpd (http://lighttpd.net) to run oBIX Server. This process is likely to be the same for other HTTP servers. A standalone fcgi server is the recommended deployment scenario for production:
 
Pre-requisites:

1. Install obix. On Fedora/RHEL:

    $ sudo yum obix obix-server obix-libs

This will bring in the dependencies of lighttpd and lighttpd-fastcgi.

To configure a standalone instance:

1. Edit /etc/obix/res/server/server_config.xml file with an XML editor. Update server-address and any other fields if required (default is localhost).

2. The default /etc/obix/res/obix-fcgi.conf links /usr/bin/obix-fcgi to /etc/obix/res/server. Update the paths if you wish to deploy to different locations.

3. Edit /etc/lighttpd/modules.conf file and add a line to include /etc/lighttpd/conf.d/obix-fcgi.conf:

        include "conf.d/obix-fcgi.conf"
    
4. Start lighttpd:

        $ sudo service lighttpd start
    
    Or
    
        $ sudo /sbin/lighttpd -f /etc/lighttpd/lighttpd.conf
    
5. Check that oBIX Server has started. The lobby object will we accessible from the browser:

        http://localhost/obix
    
    Or via curl
    
        $ curl http://localhost/obix/

## 5.3 Firewall

Lighttpd will use port 80 by default. You need to open this port on your firewall.

If you are using iptables, this command will temporarily allow port 80:

```
/sbin/iptables -A INPUT -m state --state NEW -p tcp --dport 80 -j ACCEPT
```

## 5.4 Troubleshooting (Standalone Lighttpd Instance)

Note: the path to obix-fcgi binary, the lighttpd's configuration and log folders differ in the case of Self-contained Lighttpd Instance, adjust accordingly.

* On Fedora oBIX Server logs could be observed by journalctl:

		$ journalctl -f /usr/bin/obix-fcgi

* Once lighttpd is started, use the following command to check the status of the oBIX Server:

		$ ps -eLf | grep lighttpd

* Normally multiple oBIX Server threads would be displayed. However, if no oBIX Server threads are running, it can be helpful to get to know relevant lighttpd error messages:

		$ sudo tail /var/log/lighttpd/error.log

* Strace is very handy to nail down the reason why lighttpd failed to start oBIX Server:

		$ cd /etc/lighttpd
		$ sudo strace -ff -o strace lighttpd -f /etc/lighttpd/lighttpd.conf

	The above command will have multiple strace.<TID> files generated under
	the current directory. Examine the tail part of each to identify the culprit.

* Moreover, if the lighttpd thread that tries to execute oBIX Server segfaults, it is also desirable to have coredump generated. To enable this, use the following commands:

		$ cd /etc/lighttpd
		$ ulimit -c unlimited
		$ sudo lighttpd -f /etc/lighttpd/lighttpd.conf

	Then gdb can be used to analyse the backtrace recorded in the core file.

* If history index files are touched or new log files installed (e.g. for test purposes), be sure to verify the log file abstracts in the index file are consistent with the real log files.

* If obix-fcgi binary failed to find libobix.so, its path can be specified in the "bin-environment" setting of the obix-fcgi.conf file.

* If the oBIX server oops on the fly, gdb can be used to attach to a running instance of the obix-fcgi binary and capture the calltrace on the spot.
