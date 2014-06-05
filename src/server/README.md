# oBIX Server Version 1.0
Providing oBIX Server implementation for C language
 
Copyright (c) 2014 Tyler Watson [tyler.watson@nextdc.com]    
Copyright (c) 2013-2014 Qingtao Cao [harry.cao@nextdc.com]    
Copyright (c) 2009 Andrey Litvinov [litvinov.andrey@gmail.com]
 
oBIX Server is free software licensed under the GPLv2 License.
The text for this license can be found in the COPYING file.

This file contains a description of the project, under the following sections:
    
1. Project Overview
2. System Requirements
3. Package Contents
4. Coding Style
5. Building Instructions
6. Configuring oBIX Server
7. History Facility
8. Watch Subsystem
9. Management of XML Database


# 1. Project Overview

oBIX Server is an open source project derived from the C oBIX Tools (CoT) project, an open source project dedicated to the development of embedded Building Automation solutions based on oBIX standard (http://www.obix.org). 

The whole project is written in C and has tiny resource requirements. It allows the project to run on embedded Linux platforms, such as OpenWrt (http://openwrt.org).

This package only contains the implementation of oBIX Server. The common library that can be shared among oBIX Server and oBIX client is in a separate package.

All client software such as libraries and adapter program
s are still using old XML parser provided by libupnp whereas the server implementation has adopted the new XML parser from libxml2, they are organised and distributed as separate packages.

## 1.1 C oBIX Server
 
C oBIX Server is a stand-alone application intended for storing building automation data from various sources. It is a core for automation systems, providing a unified Web services interface to control connected devices (security sensors, heating / ventilation systems, lights, switches, etc.). 

Devices are connected to the server through oBIX adapters. These adapters are separate applications, which convert device-specific protocol into oBIX messages. The server provides the same oBIX interface for devices (device adapters) and UI management applications. 

The main difference between this oBIX Server and other available implementations (such as oFMS http://www.stok.fi/eng/ofms/index.html or oX http://obix-server.sourceforge.net) is it is written in C and can be run on cheap low-resource platforms and more importantly, faster.

The list of currently implemented oBIX features includes:
 - Read, write and invoke requests handling
 - Lobby object
 - Batch operation
 - HTTP protocol binding
 - WatchService (see below)
 - Histories (see below).

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
 
This project has been created for running on Linux platforms. It was tested on various platforms, including embedded devices with OpenWrt (http://openwrt.org) installed. An example of a tested embedded platform is Asus WL-500g Premium V2 
router (32 MB of RAM and 240 MHz CPU). Any other device capable of running OpenWrt can be used instead.

Other Linux distributions for embedded devices were not tested but may possibly be used if all project dependencies are satisfied.

The project has the following libraries or packages dependencies:
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

    * Folder with project source files.

    * server/ - Sources for oBIX server.
 
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

    In the future, the oBIX Server is likely to support 'Persistent Device' and will have device contracts saved as separate files in the devices/folder on the hard drive and loaded into the global XML database at start-up.

* histories/

    Contains history facilities for various devices. Each device will have its own separate sub-folder that contains an index file and a number of raw history data files named by the date when they are generated. For details see Section 6.


# 4. Coding Style

The coding style adopted by Linux kernel source code is encouraged and should be used as much as possible. It can be found at: <Linux kernel source>/Documentation/CodingStyle>

Generally speaking, good codes speak for themselves instead of using verbose, distracting long variable names.

The one deviation from the Linux kernel coding style is the size of the tab key. As consistent with the value used in the original CoT package the size of the table key is set to four instead of eight.

The following content can be put in the ~/.vimrc to this end:

	set ts=4
	set incsearch
	set hlsearch
	set showmatch
	let c_space_errors=1
	syntax enable
	syntax on

In particular, enabling "c_space_errors" option helps to highlight any white space errors.


# 5. Build Instructions

To build the obix-server source code:

	$ cd <path to oBIX server source>
	$ cmake -DCMAKE_BUILD_TYPE=Release .
	$ VERBOSE=1 make

This creates an "out-of-source" build with all output, including the obix-fcgi binary, found in the build directory.

**Debug build**

To enable the DEBUG macro and associated tools, specify CMAKE_BUILD_TYPE to "Debug":

	$ cmake -DCMAKE_BUILD_TYPE=Debug .

Note: CMAKE_C_FLAGS_DEBUG defined in the top level CMakeList.txt seems unable to take effect in the subfolders. To workaround this, explicitly define it in the CMakeList.txt file in src/server/ folder.

**Make install build**

The make install target copies obix-fcgi to /usr/bin/obix-fcgi.

    $ sudo make install

# 6. Running oBIX Server

oBIX Server is implemented as a FastCGI script which can be executed by any HTTP server with FCGI support.

## 6.1. Self-contained lighttpd using make test

The **make test** target invokes a self-contained lighttpd instance that runs on port 4242 and therefore does not require root access to run. This removes the need to configure a standalone instance of lighttpd and allows for automated testing.

Note: This use case is ideal for development, but should is not suitable for production.

To start a local lighttpd instance of oBIX:

    $ cd <path to oBIX server source>
    $ cmake .
    $ make
    $ make test

This will start a local instance of lighttpd. You can access the server via: localhost:4242/obix

You can run any of the scripts in the directory **test/scripts**.

TODO: Add further automation to the make test build target.

## 6.2. Standalone Lighttpd Instance
 
This section describes how to use a standalone instance of lighttpd (http://lighttpd.net) to run oBIX Server. This process is likely to be the same for other HTTP servers. A standalone fcgi server is the recommended deployment scenario for production:
 
Pre-requisites:

1. Install lighttpd. On Fedora/RHEL:

    $ sudo yum install lighttpd

To setup a standlone instance:

1. Compile and install oBIX Server package using make install (see Build Instructions).
 
2. Copy the entire res directory to /etc/obix

    ~~~~
    $ sudo mkdir /etc/obix
    $ sudo cp -r res/ /etc/obix
    ~~~~
    
3. Edit /etc/obix/res/server/server_config.xml file with an XML editor. Update server-address and any other fields if required (default is localhost).

4. Copy /etc/obix/res/obix-fcgi.conf to /etc/ligttpd/conf.d

        $ sudo cp /etc/obix/res/obix-fcgi.conf /etc/ligttpd/conf.d

    The default obix-fcgi.conf links /usr/sbin/obix-fcgi to /var/lib/obix/res/server. Update the paths if you wish to deploy to different locations.


5. Edit /etc/lighttpd/modules.conf file and add a line to include /etc/lighttpd/conf.d/obix-fcgi.conf:

        include "conf.d/obix-fcgi.conf"

6. Create a folder for the history facility under /var/lib/obix/histories. Create a symbolic link in /etc/obix/res/server.

    ~~~~
    $ sudo mkdir -p /var/lib/obix/histories
    $ sudo ls -n /var/lib/obix/histories/ /etc/obix/res/server/
    ~~~~

    Note: The histories folder can become very large. In a production environment, it is recommended that /var be mounted on a separate partition.
    
7. Change ownership on the histories folder to the lighttpd user.

        $ sudo chown -R lighttpd:lighttpd /var/lib/obix/histories/

	Note: the owner and group ID of lighttpd are not necessarily "lighttpd". They are defined in lighttpd.conf and may be system specific. Check in /etc/lighttpd/lighttpd.conf:
	
	~~~~
	server.username  = "lighttpd"
	server.groupname = "lighttpd"
    ~~~~
	
8. Start lighttpd:

        $ sudo service lighttpd start
    
    Or
    
        $ sudo /sbin/lighttpd -f /etc/lighttpd/lighttpd.conf
    
9. Check that oBIX Server has started. The lobby object will we accessible from the browser:

        http://localhost/obix
    
    Or via curl
    
        $ curl http://localhost/obix/


		
Notes:

obix-fcgi.conf bin-path

~~~~        
"<path-to-obix.fcgi> [-syslog] [<path-to-res-folder>]"

where:

<path-to-obix.fcgi> 
    Location of obix.fcgi file. Provide an absolute path. For development
    set this to your local build instance.

-syslog
    Tells server to use syslog for logging messages before actual log
    settings are loaded from configuration file. It can be useful if
    some error occurs, for instance, during settings parsing. Messages
    are sent to syslog with facility 'user'. If this parameter is not
    provided then all messages generated before log settings are loaded
    will be written to the standard output.
                                                        
<path-to-res-folder>
    Address of the folder with server's resource files. These are
    installed to /var/lib/obix/res/server by default.         

For example: "bin-path" => "/usr/local/sbin/obix-fcgi /var/lib/obix/res/server"
~~~~

    
    
## 6.3 Troubleshooting

* On Fedora oBIX Server logs could be observed by journalctl:

		$ journalctl -f <path-to-obix.fcgi>

* Once lighttpd is started, use the following command to check the status of the oBIX Server:

		$ ps -eLf | grep lighttpd

* Normally multiple oBIX Server threads would be displayed. However, if no oBIX Server threads are running, it can be helpful to get to know relevant lighttpd error messages:

		$ tail /var/log/lighttpd/error.log

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


# 7 History Facility

## 7.1. Overview

The overall history facility resides under the histories/ sub-folder in the res/server/ folder, along with other configuration files.

oBIX Server supports the creation of a history facility for each device exclusively from one another, which is named after the unique ID of the relevant device. As an example, below are history facilities for a number of CB devices all on the "4A-1A" BCM device in data hall 1, M1 data centre:


    $ ls -l histories | more
	drwxr-xr-x. 2 lighttpd lighttpd 4096 May 16 10:43 M1.DH1.4A-1A.CB01
	drwxr-xr-x. 2 lighttpd lighttpd 4096 May 16 10:43 M1.DH1.4A-1A.CB02
	drwxr-xr-x. 2 lighttpd lighttpd 4096 May 16 10:43 M1.DH1.4A-1A.CB03
	drwxr-xr-x. 2 lighttpd lighttpd 4096 May 16 10:43 M1.DH1.4A-1A.CB04
	drwxr-xr-x. 2 lighttpd lighttpd 4096 May 16 10:43 M1.DH1.4A-1A.CB05

oBIX adapters can request the oBIX Server have one history facility created for the devices they control through the History.Get operation. If the required history facility has been established already, the oBIX Server will simply respond with the href, then the oBIX adapter can append data to it while the oBIX client queries records which satisfy the specified criteria.

The history facility for one device is comprised of a single index file and a number of log files named by the date they are generated. The index file is a complete XML file consisting of a list of abstracts for each log file, while log files just contain "raw data", that is, records appended by oBIX adapters without a XML header nor the root element and
that's why they are all XML fragments. For example:

    $ tree histories/M1.DH1.4A-1A.CB01
	histories/M1.DH1.4A-1A.CB01
	├── 2014-05-15.fragment
	├── 2014-05-16.fragment
	└── index.xml
    
	0 directories, 3 files

Abstracts in the index file describe the overall number of records in the relevant log file, the date when they were generated on and the start and end timestamps of the very first and very last records respectively:

    $ cat histories/M1.DH1.4A-1A.CB01/index.xml 
	<?xml version="1.0" encoding="UTF-8"?>
	<list name="index" href="index" of="obix:HistoryFileAbstract">
	  <obj is="obix:HistoryFileAbstract">
    	<date name="date" val="2014-05-15"/>
	    <int name="count" val="1964"/>
    	<abstime name="start" val="2014-05-15T06:40:34"/>
	    <abstime name="end" val="2014-05-15T23:27:59"/>
	  </obj>
	  <obj is="obix:HistoryFileAbstract">
	    <date name="date" val="2014-05-16"/>
	    <int name="count" val="121"/>
	    <abstime name="start" val="2014-05-16T00:43:26"/>
	    <abstime name="end" val="2014-05-16T01:42:32"/>
	  </obj>
	</list>

The count and end XML nodes will be updated on every successful History.Append operation.


## 7.2. Initialisation

At start-up, oBIX Server will try to initialise from history facilities available on the hard drive, so all available history data generated before the previous shutdown won't get lost.

If index files are missing, or the suffix of fragments or indexes, or their content is incorrectly touched, oBIX Server may fail to start.


## 7.3. History.Get

The History.Get operation will establish a new history facility for a specified device (if it has not yet been set up) and return its href.

On the command line, the requestor needs to specify the unique device ID in the "val" attribute of the "dev_id" XML node as the only parameter. 
The historyGet script is designed for this purpose. For example:

    $ cd tests/scripts
    $ ./historyGet 
	usage:
		./historyGet [ -v ] < -d "device href segment" >
	Where
		-v Verbose mode
		-d The href segment of the device, e.g., "/M1/DH1/BCM01/CB01/"

    $ ./historyGet -d /M1/DH1/BCM01/CB01/

The oBIX Server will respond with the following XML file:

	<?xml version="1.0" encoding="UTF-8"?>
	<str name="M1.DH1.BCM01.CB01" href="/obix/historyService/histories/M1/DH1/BCM01/CB01/"/>

Then the requestor can further manipulate the append and query operations of the history facility created.

In source code obix_getHistory() can be called to this end. The required name of the history facility should have been setup during registration.


## 7.4. History.Append

The History.Append operation appends the raw history records to the end of the latest history log file and update the end timestamp and count value in relevant abstract, provided that the timestamp of history records are not earlier than the timestamp of the very last record in the very last log file. Otherwise, the record will simply be ignored by oBIX Server.

If the input of the History.Append operation contains some records with an invalid timestamp but at least one valid record, the History.Append operation tries to absorb as many valid records as possible. Only if all records have an  invalid timestamp will oBIX Server return an error message.

If records are on a later, different date than the last log file, a new log file will be created.

On the command line, the requestor needs to provide the HistoryAppendIn contract as input. The historyAppendSingle script could be used for this purpose:

    $ cd tests/scripts
    $ ./historyAppendSingle 
	usage:
		./historyAppendSingle [ -v ] < -d "device href segment" > < -s "timestamp" >
							  [ -c "content" ]
	Where
		-v Verbose mode
		-d The href segment of the device, e.g., "/M1/DH1/BCM01/CB01/"
		-s The start timestamp, as in format '2014-04-25T15:43:50'
		-c The body of the history record without <obj> and </obj> enclosure

	$ ./historyAppendSingle -d /M1/DH1/BCM01/CB01/ -s `date +%FT%T` -c "I am working"

oBIX Server will respond with the following XML file:

	<?xml version="1.0" encoding="UTF-8"?>
	<obj is="obix:HistoryAppendOut">
	  <int name="numAdded" val="1"/>
	  <int name="newCount" val="1"/>
	  <abstime name="newStart" val="2014-04-25T15:34:23"/>
	  <abstime name="newEnd" val="2014-04-25T15:34:23"/>
	</obj>

Upon success, the History.Append operation will return:

* A HistoryAppendOut contract as defined in the oBIX specification to describe the number of records successfully appended 
* The overall number of records for the current device
* The updated start and end timestamps for the very first record
* The updated start and end timestamps for the very last record.

In source code, obix_createHistoryAppendIn() can be used to generate the required HistoryAppendIn contract (carrying only one record). After conversion to a XML document it could be passed to obix_appendHistory() to send to oBIX Server.


## 7.5. History.Query

The History.Query operation will try to fetch the records that were generated within the time range as specified by the HistoryFilter contract.

According to the oBIX specification, nearly all members of a HistoryFilter contract can be safely omitted, when a default value that makes the most sense applies.

On the command line, the requestor needs to specify a HistoryFilter contract to specify how oBIX Server should fetch the history data.

To inquire on all available data for the specified device, use historyQueryAll script:

    $ cd tests/scripts
    $ ./historyQueryAll 
	usage:
		./historyQueryAll [ -v ] < -d "device href segment" >
	Where
		-v Verbose mode
		-d The href segment of the device, e.g., "/M1/DH1/BCM01/CB01/"

    $ ./historyQueryAll -d /M1/DH1/BCM01/CB01/

The oBIX Server will respond with the following XML file:

	<?xml version="1.0" encoding="UTF-8"?>
	<obj is="obix:HistoryQueryOut">
		<int name="count" val="1"/>
		<abstime name="start" val="2014-04-25T15:34:23"/>
		<abstime name="end" val="2014-04-25T15:34:23"/>
		<list name="data" of="obix:HistoryRecord">
			<obj is="obix:HistoryRecord">
				<abstime name="timestamp" val="2014-04-25T15:34:23"/>
				<obj data="I am working"/>
			</obj>
		</list>
	</obj>

HistoryQuery script can have fine control on how to query the desired history record:

    $ cd tests/scripts
    $ ./historyQuery
	usage:
		./historyQuery [ -v ] < -d "device href segment" > [ -n "number of records" ]
					   [ -s "start timestamp" ] [ -e "end timestamp" ]
	Where
		-v Verbose mode
		-d The href segment of the device, e.g., "/M1/DH1/BCM01/CB01/"
		-n The number of records desirable
		-s The start timestamp, as in format "2014-04-25T15:41:48"
		-e The end timestamp, as in format "2014-04-25T15:41:48"

In source code, obix_createHistoryFilter() can be used to generate the required HistoryFilter contract. After conversion to a XML document it can be passed to obix_queryHistory() to query desirable history data from the oBIX Server.

As the amount of history data can be as large as several GB, data are stored in a scattered manner by libcurl in memory. Call obix_saveHistoryData() to save them into a permanent file.

Furthermore, obix_getHistoryData() can be used to assemble scattered received data into a consecutive memory region. Be sure to use it only when the amount of data is relatively small.


## 7.6. Hierarchy Support

History facilities are organised in a hierarchy structure in the global XML storage. For example:

    $ curl http://localhost/obix/historyService/histories/M1/DH1/4A-1A/CB01
	<?xml version="1.0"?>
	<obj is="obix:HistoryDevLobby" href="/obix/historyService/histories/M1/DH1/4A-1A/CB01">
	  <op name="query" href="query" in="obix:HistoryFilter" out="obix:HistoryQueryOut"/>
	  <op name="append" href="append" in="obix:HistoryAppendIn" out="obix:HistoryAppendOut"/>
	  <list name="index" href="index" of="obix:HistoryFileAbstract">
    	<obj is="obix:HistoryFileAbstract">
	      <date name="date" val="2014-05-15"/>
    	  <int name="count" val="1964"/>
	      <abstime name="start" val="2014-05-15T06:40:34"/>
    	  <abstime name="end" val="2014-05-15T23:27:59"/>
	    </obj>
	    <obj is="obix:HistoryFileAbstract">
    	  <date name="date" val="2014-05-16"/>
	      <int name="count" val="358"/>
    	  <abstime name="start" val="2014-05-16T00:43:26"/>
	      <abstime name="end" val="2014-05-16T03:41:03"/>
    	</obj>
	  </list>
	</obj>

However, they are still organised in a flat mode on the hard drive as illustrated above.

**NOTE:** All history handlers take the URI of a request as parameter then extract the portion about device name (/M1/DH1/4A-1A/CB01) and convert all slashes in it to dots (M1.DH1.4A-1A.CB01).

For this reason any component such as 'name of data centre', 'data all', 'BCM device' and 'CB device' should not contain any dot at all.


## 7.7. CURL Timeout Value

The "curl-timeout-history" tag in oBIX adapters / clients configuration files can be manipulated to setup the timeout threshold in relevant CURL handle to access history data. Applications should make use of a reasonable timeout value according to their practical networking latency and workload of data likely sent by the oBIX Server.

For oBIX adapters that only append small amount of records, timeout threshold could be set much smaller than those used by oBIX clients that could query GB amount of history data.

If the "curl-timeout-history" tag is omitted then the timeout threshold would default to zero, which means never timing out. So users could start with 0 timeout to gain a better idea about the time likely consumed in most conditions, then setup a positive value for the sake of stability.


## 7.8. Stress Test

The generate_logs.c in the test/ folder can be used to generate a number of history log files as well as their index file for test purpose. Please refer to the comment at the head of the relevant file for more information.

Be sure the hard drive is spacious enough to accommodate all potential log files before running this program. As an example, log files for 12 months will consume 4.3GB.

If generated log files and their indexes are to be merged with the existing history facility, make sure the merged index file is well-formatted and consistent with available log files.


# 8. Watch Subsystem

A super oBIX watch subsystem has been implemented to provide fantastic scalability, flexibility and performance. So far, it supports the following major features:

* No limitation on the number of watches.
* No limitation on the number of objects monitored by one watch.
* No limitation on the number of oBIX clients sharing one watch.
* Multiple watches able to monitor one same object, particuarly nested watches installed at different levels in one subtree.
* Long poll mechanism.
* Support parallelism and thread safe, specifically, multiple long poll threads handling poll tasks simultaneously to yield minimal latency.
* Recyclable watch IDs, no worries about watch ID counter's overflow (by manipulating extensible bitmap nodes).

Watch relevant scripts in tests/scripts/ can be used to test the watch subsystem.

A single watch object can be created by watchMakeSingle script and watchAddSingle script can be called for it several times to have multiple objects added to its watched upon list. The watchMake script can also be used to create a number of watches in a batch mode.

It's worthwhile to note that one watch object should avoid monitoring different objects that are ancestors or descendants of each other. That is, objects at a different level in one subtree, which is not only redundant but may result in
some unexpected side effects such as one watchOut contract may not contain all positive changes in the response to one Watch.pollChange request.

This occurs because long poll threads are working asynchronously with the thread that notifies all watches installed on ancestor objects one after the other, therefore one long poll thread could send out watchOut contract *before* all
watch_item_t for descendant objects in one watch can be properly notified. However, fortunately, this is not a fatal race condition since no changes would ever be lost and an extra pollChange request on the same watch object will collect any remaining changes.

Again, different watch objects are free to monitor whatever objects, but one watch object should avoid keeping an eye on both parent and children object.

After a watch object is fully loaded, watchPollChange script can be used to have it waiting for any changes on any monitored object, while the watchPollRefresh script is useful not only to reset any existing changes, but also to show the full list of all objects monitored by one specified watch object.

The watchRemoveSingle script could be used to remove one specified object from the watched upon list of the given watch. The watchDelete script could be used to remove a watch object completely.

It's also worthwhile to note that if a watch object is currently waiting for a change, the use of watchDelete will interrupt its waiting and have it returned prematurely so as to be deleted properly.

Due to the usage of extensible bitmap, any IDs of deleted watch objects can be properly recycled, therefore eliminating the potential overflow of a plain watch ID counter.

Lastly, the watchDeleteSingle script can delete the specified watch object, whereas the watchDeleteAll script will delete all watch objects created on the oBIX Server. They are especillay useful to test the recycling of watch IDs.


# 9. XML Database Management

Above all, the oBIX Server is a huge XML database (or XML DOM tree) that hosts a great number of XML objects (or oBIX contracts) signed up or appended by oBIX adapters such as devices and their history records.

At the time of this writing, the xmldb_update_node() in xml_storage.c can alter existing objects in the XML database in the following manner:

* Change the "val" attribute of the node of the given href
* Insert any children node from the provided input document to the destination node in the XML database
* Remove any matching reference node specified in the input document from the destination node in the XML database.

Reference nodes can be created and removed (thus updated/replaced) on the fly to setup the dynamic connection between multiple oBIX contracts without introducing further inconsistency issues in the XML database when a normal object is
deleted/replaced, since watch items may have been installed in there, relevant descriptors must be removed from relevant watch objects and poll tasks on the changed node or any of its ancestors must be notified.

The addChildren and removeRef scripts in tests/scripts/ can be used to illustrate how the insertion and deletion of the reference nodes are supported. The signUp test script shall be run first to register the required example device.
