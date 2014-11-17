# History Facility

## Overview

The overall history facility resides under the histories/ subfolder in the res/server/ folder along with other configuration files. In production a standalone hard drive had better be mounted onto this folder to ensure I/O performance because history facilities are likely to consume a vast amount of space during a long period of time.

oBIX Server supports the creation of an exclusive history facility for each device, named after the unique ID of the relevant device. E.g. below are history facilities for a number of CB devices, all on the "4A-1A" BCM device in data hall 1, M1 data centre:

    $ ls -l histories | more
	drwxr-xr-x. 2 lighttpd lighttpd 4096 May 16 10:43 M1.DH1.4A-1A.CB01
	drwxr-xr-x. 2 lighttpd lighttpd 4096 May 16 10:43 M1.DH1.4A-1A.CB02
	drwxr-xr-x. 2 lighttpd lighttpd 4096 May 16 10:43 M1.DH1.4A-1A.CB03
	drwxr-xr-x. 2 lighttpd lighttpd 4096 May 16 10:43 M1.DH1.4A-1A.CB04
	drwxr-xr-x. 2 lighttpd lighttpd 4096 May 16 10:43 M1.DH1.4A-1A.CB05

oBIX adapters can request the oBIX Server have a single history facility created for the devices they control through the History.Get operation. If a required history facility has already been established, the oBIX Server simply responds with the href. The oBIX adapter can then append data to it while the oBIX client queries records which satisfy the specified criteria.

The history facility for one device is comprised of a single index file and a number of log files, named by the date generated. The index file is a complete XML file consisting of a list of abstracts for each log file, while log files just contain 'raw data', or records appended by oBIX adapters without a XML header or  root element, which is why they are all XML fragments. For example:

    $ tree histories/M1.DH1.4A-1A.CB01
	histories/M1.DH1.4A-1A.CB01
	├── 2014-05-15.fragment
	├── 2014-05-16.fragment
	└── index.xml
    
	0 directories, 3 files

Abstracts in the index file describe:
* The overall number of records in the relevant log file
* The date they were generated on 
* The start / end timestamps of the very first and very last records respectively:

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

The count and end XML nodes are updated on every successful History.Append operation.

## Initialisation

At start-up, oBIX Server will try to initialise from history facilities available on the hard drive, so available history data generated before the previous shutdown won't be lost.

If index files are missing; the suffix of fragments or indexes; or their content is incorrectly touched, oBIX Server may fail to start.

## History.Get

The History.Get operation establishes a new history facility for a specified device (if it has not yet been set up) and return its href.

On the command line, the requestor needs to specify the unique device ID in the "val" attribute of the "dev_id" XML node as the only parameter. The historyGet script is designed for this purpose. For example:

    $ cd tests/scripts
    $ ./historyGet 
	usage:
		./historyGet [ -v ] < -d "device href segment" >
	Where
		-v Verbose mode
		-d The href segment of the device, e.g., "/M1/DH1/BCM01/CB01/"

    $ ./historyGet -d /M1/DH1/BCM01/CB01/

The oBIX Server responds with the following XML file:

	<?xml version="1.0" encoding="UTF-8"?>
	<str name="M1.DH1.BCM01.CB01" href="/obix/historyService/histories/M1/DH1/BCM01/CB01/"/>

The requestor can now further manipulate the append and query operations of the history facility created.

In source code obix_get_history() can be called to this end. The required name of the history facility should have been setup during registration.

## History.Append

The History.Append operation appends the raw history records to the end of the latest history log file and updates the end timestamp and count value in the relevant abstract, provided that the timestamp of history records are no older than, or equal to that of the very last record in the last log file. Otherwise, the record is simply ignored by the oBIX Server.

If the input of the History.Append operation contains some records with an invalid timestamp but at least one valid record, the History.Append operation tries to absorb as many valid records as possible. the oBIX Server will only return an error message when all records have an invalid timestamp.

If records are on a later, different date than the last log file, a new log file is created.

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

The oBIX Server will respond with the following XML file:

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

In source code, obix_create_history_ain() can be used to generate the required HistoryAppendIn contract, which can be further passed to obix_append_history() to send to the oBIX Server.


## History.Query

The History.Query operation will try to fetch the records that were generated within the time range as specified by the HistoryFilter contract.

According to the oBIX specification, nearly all members of a HistoryFilter contract can be safely omitted when a default value that makes the most sense applies.

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
		-s The start timestamp, as in format "2014-04-25T15:41:48Z"
		-e The end timestamp, as in format "2014-04-25T15:41:48Z"

Excepting the '-d' option, any of '-n', '-s' and '-e' options are not mandatory. If absent, the relevant handler will fall back on all existing records, and the timestamp of the first / last records respectively. Furthermore, '-n 0' can be used to obtain the timestamp of the first and the last records explicitly.

**Note: The oBIX specification demands ISO-8601 timezone support. However, current strptime() C API has made some practical compromises regarding the formats supported. Please refer to docs/timezone.md for more information.**

In the source code, obix_create_history_flt() can be used to generate the required HistoryFilter contract, which can be further passed to obix_query_history() to query desirable history data from the oBIX Server. On success, the caller provided pointer is adjusted pointing to the input buffer of the relevant CURL handler, which contains the result of the previous history.Query request. Callers should not free this pointer.

## Hierarchy Support

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

However, they are still organised in a flat mode on the hard drive, as illustrated above.

**NOTE: All history handlers take the URI of a request as a parameter, then extract the portion about the device name (/M1/DH1/4A-1A/CB01) converting all slashes contained to dots (M1.DH1.4A-1A.CB01).**

For this reason any component such as 'name of data centre', 'data all', 'BCM Device' and 'CB Device' should not contain any dots (.) at all.

## CURL Timeout Value

Since a CURL handle is not thread-safe, for multi-thread oBIX applications each thread should utilise its own CURL handle to access the history facility in parallel. Single thread oBIX applications can safely fall back on the default one. This way the 'curl-timeout' and 'curl-bulky' tags in the generic oBIX connection configuration file are used to setup the timeout threshold and the quantum size.

Regardless of which CURL handle is used, oBIX applications should setup these two settings of a CURL handle sensibly. For instance, if only a relative smaller amount of history data is exchanged with the oBIX Server, the timeout threshold should be shorter than when several GB amount of history data are transmitted.

## Stress Test

The generate_logs.c in the test/ folder can be used to generate a number of history log files as well as an index file for test purposes. Please refer to the comment at the head of the relevant file for more information.

Be sure the hard drive is large enough to accommodate all potential log files before running this program. As an example, log files for 12 months will consume 4.3GB.

If generated log files and their indexes are to be merged with the existing history facility, make sure the merged index file is well-formatted and consistent with available log files.

## Timezone Support

ISO-9801 timezone definition has been supported by the history subsystem despite some practical limitation that has been imposed by C library strptime() API. Please refer to docs/timezone.md for more details.

Following steps take advantage of relevant test scripts to demonstrate the support of ISO-8601 timezone.

```
1. Run the standalone oBIX server, and enter <path of obix package>/tests/scripts/ folder;

2. Setup a history facility for the device named "test.test_device":

	$ ./historyGet -d /test/test_device
	<?xml version="1.0" encoding="UTF-8"?>
	<str name="test.test_device" href="/obix/historyService/histories/test/test_device/"/>

3. The newly created history facility contains no data at all:

	$ ./historyQuery -d /test/test_device
	<?xml version="1.0"?>
	<err is="obix:UnsupportedErr" name="General error contract" href="/obix/historyService/histories/test/test_device/query" displayName="query" display="The device's history facility is empty"/>

4. Append the first history record to the history facility, and verify what has been added:

	$ ./historyAppendSingle -d /test/test_device -s `date +%FT%T%z`
	<?xml version="1.0" encoding="UTF-8"?>
	<obj is="obix:HistoryAppendOut">
	  <int name="numAdded" val="1"/>
	  <int name="newCount" val="1"/>
	  <abstime name="newStart" val="2014-10-21T09:48:26+1000"/>
	  <abstime name="newEnd" val="2014-10-21T09:48:26+1000"/>
	</obj>
	
	$ ./historyQueryAll -d /test/test_device
	<?xml version="1.0" encoding="UTF-8"?>
	<obj is="obix:HistoryQueryOut">
	<int name="count" val="1"/>
	<abstime name="start" val="2014-10-21T09:48:26+1000"/>
	<abstime name="end" val="2014-10-21T09:48:26+1000"/>
	<list name="data" of="obix:HistoryRecord">
	<obj is="obix:HistoryRecord">
	  <abstime name="timestamp" val="2014-10-21T09:48:26+1000"/>
	  <real name="kWh" val="0.000000"/>
	  <real name="kW" val="0.000000"/>
	  <real name="V" val="230.000000"/>
	  <real name="PF" val="0.900000"/>
	  <real name="I" val="0.000000"/>
	</obj>
	</list>
	</obj>

5. New history records can be added provided that their timestamp are not earlier than that of the latest one, otherwise an error contract is returned by oBIX server:

	$ ./historyAppendSingle -d /test/test_device -s 2014-10-21T09:48:26+0945
	<?xml version="1.0" encoding="UTF-8"?>
	<obj is="obix:HistoryAppendOut">
	  <int name="numAdded" val="1"/>
	  <int name="newCount" val="2"/>
	  <abstime name="newStart" val="2014-10-21T09:48:26+1000"/>
	  <abstime name="newEnd" val="2014-10-21T09:48:26+0945"/>
	</obj>

	$ ./historyAppendSingle -d /test/test_device -s 2014-10-21T09:48:26+1015
	<?xml version="1.0"?>
	<err is="obix:UnsupportedErr" name="General error contract" href="/obix/historyService/histories/test/test_device/append" displayName="append" display="Data list contains records with TS in the past"/>
	
6. Append a new history record on a brand new day:

	$ ./historyAppendSingle -d /test/test_device -s 2014-10-21T12:48:26-1200
	<?xml version="1.0" encoding="UTF-8"?>
	<obj is="obix:HistoryAppendOut">
	  <int name="numAdded" val="1"/>
	  <int name="newCount" val="3"/>
	  <abstime name="newStart" val="2014-10-21T09:48:26+1000"/>
	  <abstime name="newEnd" val="2014-10-21T12:48:26-1200"/>
	</obj>
	
7. Query all existing history records:

	$ ./historyQuery -d /test/test_device 
	<?xml version="1.0" encoding="UTF-8"?>
	<obj is="obix:HistoryQueryOut">
	<int name="count" val="3"/>
	<abstime name="start" val="2014-10-21T09:48:26+1000"/>
	<abstime name="end" val="2014-10-21T12:48:26-1200"/>
	<list name="data" of="obix:HistoryRecord">
	<obj is="obix:HistoryRecord">
	  <abstime name="timestamp" val="2014-10-21T09:48:26+1000"/>
	  <real name="kWh" val="0.000000"/>
	  <real name="kW" val="0.000000"/>
	  <real name="V" val="230.000000"/>
	  <real name="PF" val="0.900000"/>
	  <real name="I" val="0.000000"/>
	</obj>
	<obj is="obix:HistoryRecord">
	  <abstime name="timestamp" val="2014-10-21T09:48:26+0945"/>
	  <real name="kWh" val="0.000000"/>
	  <real name="kW" val="0.000000"/>
	  <real name="V" val="230.000000"/>
	  <real name="PF" val="0.900000"/>
	  <real name="I" val="0.000000"/>
	</obj>
	<obj is="obix:HistoryRecord">
	  <abstime name="timestamp" val="2014-10-21T12:48:26-1200"/>
	  <real name="kWh" val="0.000000"/>
	  <real name="kW" val="0.000000"/>
	  <real name="V" val="230.000000"/>
	  <real name="PF" val="0.900000"/>
	  <real name="I" val="0.000000"/>
	</obj>
	</list>
	</obj>
	
8. Query history records on specific days:

	$ ./historyQuery -d /test/test_device -e 2014-10-21T00:00:00
	<?xml version="1.0" encoding="UTF-8"?>
	<obj is="obix:HistoryQueryOut">
	<int name="count" val="1"/>
	<abstime name="start" val="2014-10-21T09:48:26+1000"/>
	<abstime name="end" val="2014-10-21T09:48:26+1000"/>
	<list name="data" of="obix:HistoryRecord">
	<obj is="obix:HistoryRecord">
	  <abstime name="timestamp" val="2014-10-21T09:48:26+1000"/>
	  <real name="kWh" val="0.000000"/>
	  <real name="kW" val="0.000000"/>
	  <real name="V" val="230.000000"/>
	  <real name="PF" val="0.900000"/>
	  <real name="I" val="0.000000"/>
	</obj>
	</list>
	</obj>
	
	$ ./historyQuery -d /test/test_device -s 2014-10-21T00:00:00 -e 2014-10-22T00:00:00+0015
	<?xml version="1.0" encoding="UTF-8"?>
	<obj is="obix:HistoryQueryOut">
	<int name="count" val="1"/>
	<abstime name="start" val="2014-10-21T09:48:26+0945"/>
	<abstime name="end" val="2014-10-21T09:48:26+0945"/>
	<list name="data" of="obix:HistoryRecord">
	<obj is="obix:HistoryRecord">
	  <abstime name="timestamp" val="2014-10-21T09:48:26+0945"/>
	  <real name="kWh" val="0.000000"/>
	  <real name="kW" val="0.000000"/>
	  <real name="V" val="230.000000"/>
	  <real name="PF" val="0.900000"/>
	  <real name="I" val="0.000000"/>
	</obj>
	</list>
	</obj>
	
	$ ./historyQuery -d /test/test_device -s 2014-10-22T00:00:00 -e 2014-10-23T00:00:00+0015
	<?xml version="1.0" encoding="UTF-8"?>
	<obj is="obix:HistoryQueryOut">
	<int name="count" val="1"/>
	<abstime name="start" val="2014-10-21T12:48:26-1200"/>
	<abstime name="end" val="2014-10-21T12:48:26-1200"/>
	<list name="data" of="obix:HistoryRecord">
	<obj is="obix:HistoryRecord">
	  <abstime name="timestamp" val="2014-10-21T12:48:26-1200"/>
	  <real name="kWh" val="0.000000"/>
	  <real name="kW" val="0.000000"/>
	  <real name="V" val="230.000000"/>
	  <real name="PF" val="0.900000"/>
	  <real name="I" val="0.000000"/>
	</obj>
	</list>
	</obj>
	
9. Exam history facility's index file:

	$ tree /var/lib/obix/histories/test.test_device/
	/var/lib/obix/histories/test.test_device/
	├── 2014-10-20.fragment
	├── 2014-10-21.fragment
	├── 2014-10-22.fragment
	└── index.xml
	
	0 directories, 4 files
	
	$ cat /var/lib/obix/histories/test.test_device/index.xml 
	<?xml version="1.0" encoding="UTF-8"?>
	<list name="index" href="index" of="obix:HistoryFileAbstract">
	  <obj is="obix:HistoryFileAbstract">
	    <date name="date" val="2014-10-20"/>
	    <int name="count" val="1"/>
	    <abstime name="start" val="2014-10-21T09:48:26+1000"/>
	    <abstime name="end" val="2014-10-21T09:48:26+1000"/>
	  </obj>
	  <obj is="obix:HistoryFileAbstract">
	    <date name="date" val="2014-10-21"/>
	    <int name="count" val="1"/>
	    <abstime name="start" val="2014-10-21T09:48:26+0945"/>
	    <abstime name="end" val="2014-10-21T09:48:26+0945"/>
	  </obj>
	  <obj is="obix:HistoryFileAbstract">
	    <date name="date" val="2014-10-22"/>
	    <int name="count" val="1"/>
	    <abstime name="start" val="2014-10-21T12:48:26-1200"/>
	    <abstime name="end" val="2014-10-21T12:48:26-1200"/>
	  </obj>
	</list>

10. Exam the content of each raw history data files:

	$ cat /var/lib/obix/histories/test.test_device/2014-10-20.fragment 
	<obj is="obix:HistoryRecord">
	  <abstime name="timestamp" val="2014-10-21T09:48:26+1000"/>
	  <real name="kWh" val="0.000000"/>
	  <real name="kW" val="0.000000"/>
	  <real name="V" val="230.000000"/>
	  <real name="PF" val="0.900000"/>
	  <real name="I" val="0.000000"/>
	</obj>
	
	$ cat /var/lib/obix/histories/test.test_device/2014-10-21.fragment 
	<obj is="obix:HistoryRecord">
	  <abstime name="timestamp" val="2014-10-21T09:48:26+0945"/>
	  <real name="kWh" val="0.000000"/>
	  <real name="kW" val="0.000000"/>
	  <real name="V" val="230.000000"/>
	  <real name="PF" val="0.900000"/>
	  <real name="I" val="0.000000"/>
	</obj>
	
	$ cat /var/lib/obix/histories/test.test_device/2014-10-22.fragment 
	<obj is="obix:HistoryRecord">
	  <abstime name="timestamp" val="2014-10-21T12:48:26-1200"/>
	  <real name="kWh" val="0.000000"/>
	  <real name="kW" val="0.000000"/>
	  <real name="V" val="230.000000"/>
	  <real name="PF" val="0.900000"/>
	  <real name="I" val="0.000000"/>
	</obj>
```
