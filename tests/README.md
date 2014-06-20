# oBIX tests

There is basic automated test functionality available via make test.

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

# Manual Test Cases

The following is a basic set of manual tests with expected output.

Notes:

* Output shown for oBIX installed and running on localhost. Adjust for your system.
* xmllint is not required, but does tidy up the output from curl.
* Results for tests can be checked in both curl and a browser.
* For the sake of brevity, not all output from commands are shown
* Use two terminals to make things easier:
    * Root - monitor the logs / start stop services
    * Normal user - run the scripts from the tests/scripts directory.
* Need to use the full path to a device's attribute. Path is constructed using "href" attribute values.

    ```XML 
    <obj name="ExampleTimer" href="example" writable="true">
        <obj href="parent" val="yes">
            <reltime name="time" href="time" val="PT0S"/>
            <bool name="reset" href="reset" val="false" writable="true"/>
        </obj>
    </obj>
    ```
* For this example device, the path to the reset attribute would be: $hostname/obix/deviceRoot/example/parent/reset

# Basic functionality

The scripts to use are in tests/scripts directory. They are hard coded for the standalone instance of lighttpd used by make test (localhost:4242). Update them to match your hostname.

```
sed -i -e 's/localhost:4242/myobixhostname.com/g' *
```

## Start oBIX

In the root terminal:

1. Start lighttpd

    RHEL

    ```
    service lighttpd start
    ```

    Fedora

    ```
    systemctl start lighttpd.service
    ```

In the normal terminal change directory into $obix_source/tests/scripts:

1. Check oBIX is running.

``` XML 
curl localhost/obix | xmllint --format -
<?xml version="1.0"?>
<obj href="/obix" is="obix:Lobby /obix/def/SignUpLobby /obix/def/DevicesLobby">
  <ref name="about" href="/obix/about" is="obix:About"/>
  <op name="batch" href="batch" in="obix:BatchIn" out="obix:BatchOut"/>
  <ref name="historyService" href="/obix/historyService" is="obix:HistoryService"/>
  <ref name="watchService" href="/obix/watchService" is="obix:WatchService"/>
  <ref name="devices" href="/obix/devices" display="Device References" is="obix:list"/>
  <op name="signUp" href="signUp" in="obix:obj" out="obix:obj"/>
</obj>
```
    
## Add a device

After starting the oBIX server, add a test device.

1. Check the default device (TestDevice) has been loaded.

    ```XML
    curl localhost/obix/devices | xmllint --format -
    <?xml version="1.0"?>
    <list href="/obix/devices" displayName="Device List" of="obix:ref">
        <ref href="/obix/deviceRoot/TestDevice" name="TestDevice" displayName="Device for tests"/>
    </list>
    ```
    Browser: hostname/obix/devices


1. Add the example device using signUp:

    ```XML
    ./signUp | xmllint --format -
    <?xml version="1.0"?>
    <obj name="ExampleTimer" href="/obix/deviceRoot/example" writable="true">
        <obj href="parent" val="yes">
            <reltime name="time" href="time" val="PT0S"/>
            <bool name="reset" href="reset" val="false" writable="true"/>
        </obj>
    </obj>
    ```

1. Check the device:

    ```XML 
    curl http://localhost/obix/deviceRoot/example | xmllint --format -

    <?xml version="1.0"?>
    <obj name="ExampleTimer" href="/obix/deviceRoot/example" writable="true">
        <obj href="parent" val="yes">
            <reltime name="time" href="time" val="PT0S"/>
            <bool name="reset" href="reset" val="false" writable="true"/>
        </obj>
    </obj>
    ```

    Browser: http://localhost/obix/deviceRoot/example


## Update a device

We can update the `val` attribute of a device (provided writable="true").

For these tests, we will set the `reset` attribute (boolean). 

1. Perform a GET to check current status. Looking for val="false":

    ```XML
    curl http://localhost/obix/deviceRoot/example/parent/reset | xmllint --format -
    
    <bool name="reset" href="/obix/deviceRoot/example/parent/reset" val="false" writable="true"/>
    ```

2. Set the value to true (note oBIX is case sensitive):

    ```XML 
    curl -XPUT --data '<bool val="true"/>' http://localhost/obix/deviceRoot/example/parent/reset
    
    <?xml version="1.0"?>
    <bool name="reset" href="reset" val="true" writable="true"/>
    ```

3. Set it to true again to make sure nothing breaks. 

    ```XML 
    curl -XPUT --data '<bool val="true"/>' http://localhost/obix/deviceRoot/example/parent/reset
    
    <?xml version="1.0"?>
    <bool name="reset" href="reset" val="true" writable="true"/>
    ```

    Browser: http://localhost/obix/deviceRoot/example

4. Set back to original value (false).

    ```XML 
    curl -XPUT --data '<bool val="false"/>' http://localhost/obix/deviceRoot/example/parent/reset
    
    <?xml version="1.0"?>
    <bool name="reset" href="reset" val="false" writable="true"/>
    ```


## Negative tests

These are tests that should not work.

### Writable="false"

Write to an attribute that is not writable (writable="false"). We should get an error contract.

In ExampleTimer, we should not be able to write to the `time` attribute.

```XML
<obj name="ExampleTimer" href="example" writable="true">
    <obj href="parent" val="yes">
        <reltime name="time" href="time" val="PT0S"/>
        <bool name="reset" href="reset" val="false" writable="true"/>
    </obj>
</obj>
```

1. Attempt to write the value 4 seconds to time (val="PT4S"):

    ```XML
    curl -XPUT --data '<reltime val="PT4S"/>' \
    http://localhost/obix/deviceRoot/example/parent/time | xmllint --format -

    <err is="obix:PermissionErr" name="General error contract"
    href="/obix/deviceRoot/example/parent/time" displayName="obix:Write" 
    display="The destination object or its direct parent is not writable"/>
    ```

2. Check for a log entry (/var/log/messages or journalctl):

    ```
    /builddir/build/BUILD/obix-1.0/src/server/server.c(300): 
    Failed to update the XML database: The destination object or 
    its direct parent is not writable     
    ```

### Wrong data type

Post a value to a bool other than true/false (case sensitive)

In ExampleTimer, we should only be able to write lower case true||false into `reset`.

```XML
<obj name="ExampleTimer" href="example" writable="true">
    <obj href="parent" val="yes">
        <reltime name="time" href="time" val="PT0S"/>
        <bool name="reset" href="reset" val="false" writable="true"/>
    </obj>
</obj>
```

1. Attempt to write True (wrong case):

    ```XML 
    $ curl -XPUT --data '<bool val="True"/>' http://localhost/obix/deviceRoot/example2/parent/reset

    <?xml version="1.0"?>
    <err is="obix:UnsupportedErr" name="General error contract" 
    href="/obix/deviceRoot/example2/parent/reset" displayName="obix:Write" 
    display="@val on the source input data not a valid boolean"/>
    ```

1. Attempt to write NULL:

    ```XML 
    curl -XPUT --data '<bool val=""/>' http://localhost/obix/deviceRoot/example2/parent/reset
    
    <?xml version="1.0"?>
    <err is="obix:UnsupportedErr" name="General error contract" 
    href="/obix/deviceRoot/example2/parent/reset" displayName="obix:Write" 
    display="@val on the source input data not a valid boolean"/>
    ```

1. Attempt to write valid boolean (true)

    ```XML 
    curl -XPUT --data '<bool val="true"/>' http://localhost/obix/deviceRoot/example/parent/reset
    
    <bool name="reset" href="reset" val="true" writable="true"/>
    ```


# History

Refer to https://github.com/ONEDC/obix/blob/devel/docs/HISTORY.md

The scripts require arguments:

* -d $href
* -s $timestamp. To use current time: $(date +%FT%T)
* -c $object data. 

Pre-requisites:

1. Add the example device using tests/scripts/signUp

## Creating the history record 

1. Create a new record using historyGet:

    ```XML 
    ./historyGet -d example -s $(date +%FT%T) -c "Test entry"
    
    <?xml version="1.0" encoding="UTF-8"?>
    <str name="example" href="/obix/historyService/histories/example/"/>
    ```

1. Check the history lobby object:

    ```XML 
    curl http://localhost/obix/historyService/histories/ | xmllint --format -

    <?xml version="1.0"?>
        <list name="histories" href="/obix/historyService/histories" of="obix:obj">
        <obj is="obix:HistoryDevLobby" href="example">
            <op name="query" href="query" in="obix:HistoryFilter" out="obix:HistoryQueryOut"/>
            <op name="append" href="append" in="obix:HistoryAppendIn" out="obix:HistoryAppendOut"/>
            <list name="index" href="index" of="obix:HistoryFileAbstract"/>
        </obj>
    </list>
    ```

    Browser: http://localhost/obix/historyService/histories/

1. Check the record:

    ```XML 
    curl localhost/obix/historyService/histories/example | xmllint --format -

    <?xml version="1.0"?>
    <obj is="obix:HistoryDevLobby" href="/obix/historyService/histories/example">
        <op name="query" href="query" in="obix:HistoryFilter" out="obix:HistoryQueryOut"/>
        <op name="append" href="append" in="obix:HistoryAppendIn" out="obix:HistoryAppendOut"/>
        <list name="index" href="index" of="obix:HistoryFileAbstract"/>
    </obj>
    ```

    Browser: http://localhost/obix/historyService/histories/example

    There are no entries. (Compare this to a history record after we have added entries).
    
1. Check that the data was written to the correct disk location (default /var/lib/obix/histories).

    As we have not added any entries, there will be no fragments, only an index.xml file.

    ```
    ls /var/lib/obix/histories
    example
    ```

    ```
    ls /var/lib/obix/histories/example/
    index.xml
    ```

## Adding records to the history

1. Add 4 records to example device history

    ```XML 
    for value in 70 80 90 100; do ./historyAppendSingle -d example -s $(date +%FT%T) -c "$value"; done;

    <?xml version="1.0" encoding="UTF-8"?>
    <obj is="obix:HistoryAppendOut">
        <int name="numAdded" val="1"/>
        <int name="newCount" val="1"/>
        <abstime name="newStart" val="2014-06-19T15:00:47"/>
        <abstime name="newEnd" val="2014-06-19T15:00:47"/>
    </obj>
    <?xml version="1.0" encoding="UTF-8"?>
    <obj is="obix:HistoryAppendOut">
        <int name="numAdded" val="1"/>
        <int name="newCount" val="2"/>
        <abstime name="newStart" val="2014-06-19T15:00:47"/>
        <abstime name="newEnd" val="2014-06-19T15:00:48"/>
    </obj>
    <?xml version="1.0" encoding="UTF-8"?>
    <obj is="obix:HistoryAppendOut">
        <int name="numAdded" val="1"/>
        <int name="newCount" val="3"/>
        <abstime name="newStart" val="2014-06-19T15:00:47"/>
        <abstime name="newEnd" val="2014-06-19T15:00:48"/>
    </obj>
    <?xml version="1.0" encoding="UTF-8"?>
    <obj is="obix:HistoryAppendOut">
        <int name="numAdded" val="1"/>
        <int name="newCount" val="4"/>
        <abstime name="newStart" val="2014-06-19T15:00:47"/>
        <abstime name="newEnd" val="2014-06-19T15:00:48"/>
    </obj>
    ```

    Browser: http://localhost/obix/historyService/histories/example


    *TODO: Check with Harry - should we be able to see all the entries via a browser?*
    
    
1. Check that the data was written to the correct disk location (default /var/lib/obix/histories).

    There should now be a fragment with today's date and index.xml 
    
    ```
    ls /var/lib/obix/histories/example/
    2014-06-19.fragment  index.xml
    ```

1. Get the records and check that 70, 80, 90, 100 were written as data.

    ```XML 
    ./historyQuery -d example
    <?xml version="1.0" encoding="UTF-8"?>
    <obj is="obix:HistoryQueryOut">
        <int name="count" val="4"/>
        <abstime name="start" val="2014-06-19T15:00:47"/>
        <abstime name="end" val="2014-06-19T15:00:48"/>
        <list name="data" of="obix:HistoryRecord">
            <obj is="obix:HistoryRecord">
                <abstime name="timestamp" val="2014-06-19T15:00:47"/>
            <obj data="70"/>
            </obj>
            <obj is="obix:HistoryRecord">
                <abstime name="timestamp" val="2014-06-19T15:00:48"/>
            <obj data="80"/>
            </obj>
            <obj is="obix:HistoryRecord">
                <abstime name="timestamp" val="2014-06-19T15:00:48"/>
            <obj data="90"/>
            </obj>
            <obj is="obix:HistoryRecord">
                <abstime name="timestamp" val="2014-06-19T15:00:48"/>
            <obj data="100"/>
            </obj>
        </list>
    </obj>
    ```

    Browser: http://localhost/obix/historyService/histories/example


    *TODO: Check with Harry - should we be able to see all the entries via a browser?*

## History persistence 

The history should be reloaded when lighttpd is restarted.

Pre-requisites:

1. Example device loaded.
1. History records added to example device.

In the root terminal:

1. Check the service is running:

    ```
    service lighttpd status
    ```

    Fedora

    ```
    systemctl status lighttpd.service
    ```

1. Stop the service:

    ```
    service lighttpd stop
    ```

    Fedora

    ```
    systemctl stop lighttpd.service
    ```

1. Check the history directory has not been removed.

    ```
    ls /var/lib/obix/histories/example/
    2014-06-19.fragment  index.xml
    ```

1. Start the service:

    ```
    service lighttpd start
    ```

    Fedora

    ```
    systemctl start lighttpd.service
    ```

In the normal terminal:

1. Check oBIX is running and the test device (TestDevice) has loaded:

    ```XML 
    curl localhost/obix/devices | xmllint --format -
    
    <?xml version="1.0"?>
    <list href="/obix/devices" displayName="Device List" of="obix:ref">
        <ref href="/obix/deviceRoot/TestDevice" name="TestDevice" displayName="Device for tests"/>
    </list>
    ```
    Browser: hostname/obix/devices

1. Check the history record for the example device have been loaded.

    ```XML
    ./historyQuery -d example
    <?xml version="1.0" encoding="UTF-8"?>
    <obj is="obix:HistoryQueryOut">
        <int name="count" val="4"/>
        <abstime name="start" val="2014-06-19T15:08:36"/>
        <abstime name="end" val="2014-06-19T15:08:36"/>
        <list name="data" of="obix:HistoryRecord">
            <obj is="obix:HistoryRecord">
                <abstime name="timestamp" val="2014-06-19T15:08:36"/>
                <obj data="70"/>
            </obj>
            <obj is="obix:HistoryRecord">
                <abstime name="timestamp" val="2014-06-19T15:08:36"/>
                <obj data="80"/>
            </obj>
            <obj is="obix:HistoryRecord">
                <abstime name="timestamp" val="2014-06-19T15:08:36"/>
                <obj data="90"/>
            </obj>
            <obj is="obix:HistoryRecord">
                <abstime name="timestamp" val="2014-06-19T15:08:36"/>
                <obj data="100"/>
            </obj>
        </list>
    </obj>
    ```
    
    Note: We did not need to load the example device to view the history.
    
# Watch service

Refer to https://github.com/ONEDC/obix/blob/devel/docs/WATCH.md

## Create a watch object

1. Create a single watch object with watchMakeSingle:

    ```XML 
    ./watchMakeSingle | xmllint --format -

    <?xml version="1.0"?>
    <obj href="/obix/watchService/0/watch0/" is="obix:Watch /obix/def/LongPollWatch">
    <reltime name="lease" href="lease" min="PT0S" max="PT24H" val="PT1H" writable="true"/>
    <obj name="pollWaitInterval" href="pollWaitInterval">
        <reltime name="min" href="min" min="PT0S" max="PT1M" val="PT10S" writable="true"/>
        <reltime name="max" href="max" min="PT0S" max="PT1M" val="PT30S" writable="true"/>
    </obj>
    <op name="add" href="add" in="obix:WatchIn" out="obix:WatchOut"/>
    <op name="remove" href="remove" in="obix:WatchIn"/>
    <op name="pollChanges" href="pollChanges" out="obix:WatchOut"/>
    <op name="pollRefresh" href="pollRefresh" out="obix:WatchOut"/>
    <op name="delete" href="delete"/>
    </obj>
    ```

    The /0 parent holds 64 watch objects. When we create watch 65, it goes under parent /1.

## Create multiple watch objects.

We want to create > 64 watch objects to check that the parent folders are created correctly.

1. Create 65 watch objects with watchMake:

    ```XML 
    ./watchMake -n 65

    [...]
    <obj href="/obix/watchService/0/watch63/"
    <obj href="/obix/watchService/1/watch64/"
    <obj href="/obix/watchService/1/watch65/"

    ```

    Zero based indexing means the watch we created earlier was watch 0. We have now created up to watch65.

2. Repeat and check that parent /2 has been created:
    
    ```XML 
    ./watchMake -n 65
    [...]
    <obj href="/obix/watchService/1/watch127/"
    <obj href="/obix/watchService/2/watch128/"
    [...]
    ```
    

## Delete a single watch object

We should be able to delete any of the 128 watch objects we created.

1. Delete watch object 126:

    ```XML 
    ./watchDelete -w 126
    ```

## Add another watch

Checking watch ID reuse.

1. Create a watch object with watchMakeSingle:

    ```XML 
    ./watchMakeSingle

    <obj href="/obix/watchService/1/watch126/"
    ```

    Should be the watch we deleted (in this case 126).

1. Create another watch object with watchMakeSingle. This time, the number sequence should continue from the highest number.

    ```XML 
    ./watchMakeSingle 

    <obj href="/obix/watchService/2/watch131/"
    ```
1. Delete/Create cycle again.

## Delete all

Delete all watches and start testing again to ensure nothing breaks.

1. Delete all with watchDeleteAll:

    ```
    ./watchDeleteAll
    ```

1. Also check in the browser: http://localhost/obix/watchService

1. Create 130 watch objects with watchMake

    ```XML 
    ./watchMake -n 130
    <obj href="/obix/watchService/0/watch0/
    [...]
    <obj href="/obix/watchService/0/watch63/"
    <obj href="/obix/watchService/1/watch64/"
    [...]
    <obj href="/obix/watchService/1/watch127/"
    <obj href="/obix/watchService/2/watch128/"
    <obj href="/obix/watchService/2/watch129/"
    ```
    As before, check the parent folder rolls over after 63 entries (zero based indexing)
    
1. Create another 130 watch objects with watchMake

    ```XML 
    ./watchMake -n 130
    [...]
    <obj href="/obix/watchService/4/watch259/"
    ```
1. Delete selected watch objects with watchDelete

    ```XML 
    for watchID in 0 64 128 192; do ./watchDelete -w $watchID; done;
    
    <obj null="true"/>
    <?xml version="1.0"?>
    <obj null="true"/>
    <?xml version="1.0"?>
    <obj null="true"/>
    <?xml version="1.0"?>
    <obj null="true"/>
    ```

1. Create 5 watch objects - expecting reuse of watch IDs from previous step plus one new watchID.

    ```XML 
    ./watchMake -n 5
    <obj href="/obix/watchService/0/watch0/"
    <obj href="/obix/watchService/1/watch64/"
    <obj href="/obix/watchService/2/watch128/" 
    <obj href="/obix/watchService/3/watch192/"
    [...new...]
    <obj href="/obix/watchService/4/watch260/"
    ```

## Delete a non-existent watch

Delete should not work if the watch does not exist.

1. Delete the 4 watches from the previous test:

    ```XML 
    $ for watchID in 0 64 128 192; do ./watchDelete -w $watchID; done;

    <obj null="true"/>
    <?xml version="1.0"?>
    <obj null="true"/>
    <?xml version="1.0"?>
    <obj null="true"/>
    <?xml version="1.0"?>
    <obj null="true"/>
    ```
1. Try again - should not succeed

    ```XML 
    for watchID in 0 64 128 192; do ./watchDelete -w $watchID; done;

    <err is="obix:BadUriErr" name="General error contract" 
    href="/obix/watchService/0/watch0/delete"
    displayName="oBIX Server" display="Requested URI could not be found on this server"/>

    <err is="obix:BadUriErr" name="General error contract" 
    href="/obix/watchService/1/watch64/delete"
    displayName="oBIX Server" display="Requested URI could not be found on this server"/>

    <err is="obix:BadUriErr" name="General error contract" 
    href="/obix/watchService/2/watch128/delete"
    displayName="oBIX Server" display="Requested URI could not be found on this server"/>

    <err is="obix:BadUriErr" name="General error contract" 
    href="/obix/watchService/3/watch192/delete"
    displayName="oBIX Server" display="Requested URI could not be found on this server"/>
    ```
1. Try a random watchID that is > any created so far:

    ```XML 
    ./watchDelete -w 4321

    <err is="obix:BadUriErr" name="General error contract" href="/obix/watchService/67/watch4321/delete"
    displayName="oBIX Server" display="Requested URI could not be found on this server"/>
    ```

    
# Config notes

Set the server address in /etc/obix/res/server/server_conf.xml

```XML 
  <server-address val="localhost"/>
```

Localhost works fine for local testing (VM on your local workstation), but needs to be set to the IP/URL for remote clients to work properly.

# Errors

1. If you hit this when trying to run lighttpd:

    ```
    lighttpd -f /etc/lighttpd/lighttpd.conf 
    2014-05-05 11:36:57: (plugin.c.169) dlopen() failed for: \
    /usr/lib64/lighttpd/mod_fastcgi.so /usr/lib64/lighttpd/mod_fastcgi.so: \
    cannot open shared object file: No such file or directory \
    2014-05-05 11:36:57: (server.c.679) loading plugins finally failed
    ```

    Install lighttpd-fastccgi: sudo yum install lighttpd-fastcgi

    (Alternatively install via rpm which has lighttpd-fastcgi as a dependency)

# Logs

Fedora has journalctl which makes filtering easier. RHEL uses syslog.

Fedora: 

```
journalctl -f /usr/bin/obix-fcgi
```

RHEL:

```
tail -f /var/log/messages
```

and lighttpd logs (error.log and access.log)

```
tail -f /var/log/lighttpd/error.log
```
