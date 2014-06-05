# History Facility

## Overview

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


## Initialisation

At start-up, oBIX Server will try to initialise from history facilities available on the hard drive, so all available history data generated before the previous shutdown won't get lost.

If index files are missing, or the suffix of fragments or indexes, or their content is incorrectly touched, oBIX Server may fail to start.


## History.Get

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


## History.Append

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


## History.Query

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

However, they are still organised in a flat mode on the hard drive as illustrated above.

**NOTE:** All history handlers take the URI of a request as parameter then extract the portion about device name (/M1/DH1/4A-1A/CB01) and convert all slashes in it to dots (M1.DH1.4A-1A.CB01).

For this reason any component such as 'name of data centre', 'data all', 'BCM device' and 'CB device' should not contain any dot at all.


## CURL Timeout Value

The "curl-timeout-history" tag in oBIX adapters / clients configuration files can be manipulated to setup the timeout threshold in relevant CURL handle to access history data. Applications should make use of a reasonable timeout value according to their practical networking latency and workload of data likely sent by the oBIX Server.

For oBIX adapters that only append small amount of records, timeout threshold could be set much smaller than those used by oBIX clients that could query GB amount of history data.

If the "curl-timeout-history" tag is omitted then the timeout threshold would default to zero, which means never timing out. So users could start with 0 timeout to gain a better idea about the time likely consumed in most conditions, then setup a positive value for the sake of stability.


## Stress Test

The generate_logs.c in the test/ folder can be used to generate a number of history log files as well as their index file for test purpose. Please refer to the comment at the head of the relevant file for more information.

Be sure the hard drive is spacious enough to accommodate all potential log files before running this program. As an example, log files for 12 months will consume 4.3GB.

If generated log files and their indexes are to be merged with the existing history facility, make sure the merged index file is well-formatted and consistent with available log files.


