# Multi-thread support

The multi-thread support feature is crucial for the oBIX Server to meet performance and scalability requirements. If oBIX server can't consume requests from the FCGI listening socket and its backlog queue is full, newly arrived FCGI requests are simply rejected resulting in relevant client applications receiving HTTP 503 "Service Not Available" type of error. Obviously the more number of oBIX adaptors sending requests concurrently, or the more time-consuming a request is (e.g., when disk I/O is further triggered), the more likely such situation happens.

The solution lies in multi-thread support, that is, multiple server threads are spawned at start-up to accept and handle FCGI requests in parallel. As a matter of fact, an accepted FCGI request is a wrapper structure of an established TCP connection, through which relevant server thread can send back responses separate from others.

# Observation of multi-thread behaviour

The http_load program (available from http://acme.com/software/http_load/) can be used to observe the multi-thread behaviour of the oBIX Server. To this end, deliberately invoke "sleep(2)" in obix_server_read() so as to have each read request consume 2 seconds for test purpose, and specify oBIX server to spawn 20 threads at start-up.

In command line specify http_load to send 20 fetch requests in parallel (to read from localhost/obix). If the oBIX server is single threaded, or FCGX requests are accepted and handled in a serialised manner, it will take up to 20 * 2 = 40 seconds for oBIX Server to handle all 20 fetches. However, if there are 20 server threads working in parallel, it will still take 2 seconds:


```
$ cat urls.txt
http://localhost/obix

$ ./http_load -parallel 20 -fetch 20 urls.txt
20 fetches, 20 max parallel, 22380 bytes, in 2.00914 seconds
1119 mean bytes/connection
9.95452 fetches/sec, 11139.1 bytes/sec
msecs/connect: 0.73005 mean, 1.553 max, 0.119 min
msecs/first-response: 2004.5 mean, 2006.06 max, 2001.76 min
HTTP response codes:
  code 200 -- 20
```

Now send one more fetch request, the time spent should be 2 + 2 = 4 seconds:

```
$ ./http_load -parallel 20 -fetch 21 urls.txt
21 fetches, 20 max parallel, 23499 bytes, in 4.00529 seconds
1119 mean bytes/connection
5.24307 fetches/sec, 5867 bytes/sec
msecs/connect: 0.634619 mean, 1.663 max, 0.114 min
msecs/first-response: 2004.11 mean, 2005.73 max, 2001.17 min
HTTP response codes:
  code 200 -- 21
```

So is the case if there are 40 fetch requests, it won't take longer time than 21 requests thanks to multi-thread support of the oBIX Server:

```
$ ./http_load -parallel 20 -fetch 40 urls.txt
40 fetches, 20 max parallel, 44760 bytes, in 4.04571 seconds
1119 mean bytes/connection
9.88702 fetches/sec, 11063.6 bytes/sec
msecs/connect: 0.53955 mean, 1.892 max, 0.122 min
msecs/first-response: 2005.57 mean, 2032.64 max, 2001.14 min
HTTP response codes:
  code 200 -- 40
```

# Observation of TCP established connections for each thread

In this example, 5 server threads are spawned:

```
$ ps -eLf | grep lighttpd
lighttpd  5364     1  5364  2    1 14:24 ?        00:02:25 lighttpd -f /etc/lighttpd/lighttpd.conf
lighttpd  5365  5364  5365  0    8 14:24 ?        00:00:00 /usr/bin/obix-fcgi /etc/obix/res/server
lighttpd  5365  5364  5366  0    8 14:24 ?        00:00:00 /usr/bin/obix-fcgi /etc/obix/res/server
lighttpd  5365  5364  5367  0    8 14:24 ?        00:00:00 /usr/bin/obix-fcgi /etc/obix/res/server
lighttpd  5365  5364  5368  0    8 14:24 ?        00:00:55 /usr/bin/obix-fcgi /etc/obix/res/server
lighttpd  5365  5364  5369  0    8 14:24 ?        00:00:55 /usr/bin/obix-fcgi /etc/obix/res/server
lighttpd  5365  5364  5370  0    8 14:24 ?        00:00:55 /usr/bin/obix-fcgi /etc/obix/res/server
lighttpd  5365  5364  5371  0    8 14:24 ?        00:00:55 /usr/bin/obix-fcgi /etc/obix/res/server
lighttpd  5365  5364  5372  0    8 14:24 ?        00:00:55 /usr/bin/obix-fcgi /etc/obix/res/server
harry     6808  2955  6808  0    1 16:09 pts/1    00:00:00 grep --color=auto lighttpd
```

In particular, the process with ID 5365 is the oBIX server process, which spawns another 5 threads running payload() (threads 5368 ~ 5372) while the other two threads (5366 and 5367) are watch threads to handle expired watch objects and asynchronous watch.pollChanges requests respectively.

The strace command can be used to observe the established TCP connections used by server threads in parallel:

```
$ sudo strace -ff -p 5365 -ttT -o obix -e trace=accept
[sudo] password for harry: 
Process 5365 attached with 8 threads
^CProcess 5365 detached
Process 5366 detached
Process 5367 detached
Process 5368 detached
Process 5369 detached
Process 5370 detached
Process 5371 detached
Process 5372 detached

$ head obix.*
==> obix.5365 <==

==> obix.5366 <==

==> obix.5367 <==

==> obix.5368 <==
16:10:17.388576 accept(4, {sa_family=AF_LOCAL, NULL}, [2]) = 9 <0.045586>
16:10:17.435011 accept(4, {sa_family=AF_LOCAL, NULL}, [2]) = 9 <0.041304>
16:10:17.476849 accept(4, {sa_family=AF_LOCAL, NULL}, [2]) = 9 <0.061366>
16:10:17.539488 accept(4, {sa_family=AF_LOCAL, NULL}, [2]) = 6 <0.056658>
16:10:17.596851 accept(4, {sa_family=AF_LOCAL, NULL}, [2]) = 6 <0.064380>
16:10:17.662480 accept(4, {sa_family=AF_LOCAL, NULL}, [2]) = 6 <0.053663>
16:10:17.716718 accept(4, {sa_family=AF_LOCAL, NULL}, [2]) = 6 <0.067495>
16:10:17.785521 accept(4, {sa_family=AF_LOCAL, NULL}, [2]) = 6 <0.041236>
16:10:17.827334 accept(4, {sa_family=AF_LOCAL, NULL}, [2]) = 6 <0.049794>
16:10:17.877617 accept(4, {sa_family=AF_LOCAL, NULL}, [2]) = 6 <0.068647>

==> obix.5369 <==
16:10:17.388444 accept(4, {sa_family=AF_LOCAL, NULL}, [2]) = 7 <0.027937>
16:10:17.417121 accept(4, {sa_family=AF_LOCAL, NULL}, [2]) = 7 <0.040983>
16:10:17.458558 accept(4, {sa_family=AF_LOCAL, NULL}, [2]) = 7 <0.039720>
16:10:17.498689 accept(4, {sa_family=AF_LOCAL, NULL}, [2]) = 7 <0.057452>
16:10:17.556927 accept(4, {sa_family=AF_LOCAL, NULL}, [2]) = 7 <0.063309>
16:10:17.621499 accept(4, {sa_family=AF_LOCAL, NULL}, [2]) = 5 <0.039798>
16:10:17.662129 accept(4, {sa_family=AF_LOCAL, NULL}, [2]) = 5 <0.040277>
16:10:17.704085 accept(4, {sa_family=AF_LOCAL, NULL}, [2]) = 5 <0.052066>
16:10:17.756854 accept(4, {sa_family=AF_LOCAL, NULL}, [2]) = 5 <0.068393>
16:10:17.826746 accept(4, {sa_family=AF_LOCAL, NULL}, [2]) = 5 <0.040374>

==> obix.5370 <==
16:10:17.388412 accept(4, {sa_family=AF_LOCAL, NULL}, [2]) = 8 <0.028962>
16:10:17.417998 accept(4, {sa_family=AF_LOCAL, NULL}, [2]) = 8 <0.057150>
16:10:17.476330 accept(4, {sa_family=AF_LOCAL, NULL}, [2]) = 8 <0.039796>
16:10:17.516923 accept(4, {sa_family=AF_LOCAL, NULL}, [2]) = 8 <0.062304>
16:10:17.580510 accept(4, {sa_family=AF_LOCAL, NULL}, [2]) = 8 <0.055643>
16:10:17.636665 accept(4, {sa_family=AF_LOCAL, NULL}, [2]) = 8 <0.065547>
16:10:17.703027 accept(4, {sa_family=AF_LOCAL, NULL}, [2]) = 8 <0.000012>
16:10:17.703479 accept(4, {sa_family=AF_LOCAL, NULL}, [2]) = 8 <0.040648>
16:10:17.744683 accept(4, {sa_family=AF_LOCAL, NULL}, [2]) = 8 <0.052445>
16:10:17.797634 accept(4, {sa_family=AF_LOCAL, NULL}, [2]) = 8 <0.039477>

==> obix.5371 <==
16:10:17.388362 accept(4, {sa_family=AF_LOCAL, NULL}, [2]) = 6 <0.026898>
16:10:17.415960 accept(4, {sa_family=AF_LOCAL, NULL}, [2]) = 6 <0.041135>
16:10:17.457616 accept(4, {sa_family=AF_LOCAL, NULL}, [2]) = 6 <0.039644>
16:10:17.498276 accept(4, {sa_family=AF_LOCAL, NULL}, [2]) = 6 <0.040003>
16:10:17.539104 accept(4, {sa_family=AF_LOCAL, NULL}, [2]) = 10 <0.040158>
16:10:17.580297 accept(4, {sa_family=AF_LOCAL, NULL}, [2]) = 9 <0.040000>
16:10:17.621147 accept(4, {sa_family=AF_LOCAL, NULL}, [2]) = 9 <0.040122>
16:10:17.662441 accept(4, {sa_family=AF_LOCAL, NULL}, [2]) = 9 <0.080870>
16:10:17.744507 accept(4, {sa_family=AF_LOCAL, NULL}, [2]) = 9 <0.039754>
16:10:17.785480 accept(4, {sa_family=AF_LOCAL, NULL}, [2]) = 7 <0.039841>

==> obix.5372 <==
16:10:17.388228 accept(4, {sa_family=AF_LOCAL, NULL}, [2]) = 5 <0.004895>
16:10:17.394014 accept(4, {sa_family=AF_LOCAL, NULL}, [2]) = 5 <0.062183>
16:10:17.456831 accept(4, {sa_family=AF_LOCAL, NULL}, [2]) = 5 <0.040382>
16:10:17.497957 accept(4, {sa_family=AF_LOCAL, NULL}, [2]) = 5 <0.040296>
16:10:17.539449 accept(4, {sa_family=AF_LOCAL, NULL}, [2]) = 5 <0.039841>
16:10:17.579955 accept(4, {sa_family=AF_LOCAL, NULL}, [2]) = 5 <0.040317>
16:10:17.621532 accept(4, {sa_family=AF_LOCAL, NULL}, [2]) = 7 <0.054615>
16:10:17.676605 accept(4, {sa_family=AF_LOCAL, NULL}, [2]) = 7 <0.066654>
16:10:17.744574 accept(4, {sa_family=AF_LOCAL, NULL}, [2]) = 7 <0.039724>
16:10:17.785268 accept(4, {sa_family=AF_LOCAL, NULL}, [2]) = 10 <0.040022>
```

In above example, for each thread the fd[4] is the UNIX socket file (/var/run/lighttpd/obix.fcgi.socket-0) that oBIX server threads listen on in parallel. From the start timestamp and duration of the accept() system call, we can tell that each thread accepts and establishes connections simultaneously.

# Debugging multi-thread related problems

Generally speaking concurrency issues are hard to reproduce and analyse. To start with, try to reduce the number of server threads via the "multi_threads" configuration option while still able to reproduce the problem and enable the "DEBUG_TSYNC" macro in libs/tsync.c file.

## Server threads' calltrace

When a synchronisation issue occurs such as a thread is slept forever on a POSIX pthread conditional, gdb can be used to attach to each server thread to peek its calltrace.

The polling threads' calltrace may look like below:

(gdb) bt
#0  pthread_cond_wait@@GLIBC_2.3.2 () at ../nptl/sysdeps/unix/sysv/linux/x86_64/pthread_cond_wait.S:185
#1  0x000000000040f1d4 in poll_thread_task (arg=0x2626ee0) at /work/obix/src/server/watch.c:1827
#2  0x0000003c78e07c65 in start_thread (arg=0x7fdb36e62700) at pthread_create.c:308
#3  0x0000003c786f5b9d in clone () at ../sysdeps/unix/sysv/linux/x86_64/clone.S:113
(gdb)

They are running the poll_thread_task loop and sleep on a pthread conditional when there is no outstanding watch poll task to handle.

Moreover, common server threads may enter the following calltrace while being blocked by glibc accept() API to wait for an established FCGX requests:

```
(gdb) bt
#0  0x0000003c78e0e34d in accept () at ../sysdeps/unix/syscall-template.S:81
#1  0x0000003c7a20794e in OS_Accept (listen_sock=4, fail_on_intr=0, webServerAddrs=0x0) at os_unix.c:1166
#2  0x0000003c7a20593c in FCGX_Accept_r (reqDataPtr=0x7fdb280008c0) at fcgiapp.c:2196
#3  0x000000000040a876 in obix_fcgi_request_create (fcgi=0x25fe7c0) at /work/obix/src/server/obix_fcgi.c:429
#4  0x000000000040a8f2 in payload (arg=0x25fe7c0) at /work/obix/src/server/obix_fcgi.c:481
#5  0x0000003c78e07c65 in start_thread (arg=0x7fdb36626700) at pthread_create.c:308
#6  0x0000003c786f5b9d in clone () at ../sysdeps/unix/sysv/linux/x86_64/clone.S:113
(gdb) 
```

When synchronisation issues emerge server threads may deadlock with others or never get waken up properly. Once upon a time there was a defect in a tsync.c API resulting in server threads running signOff handlers blocked indefinitely. Relevant calltrace looks like below:

```
(gdb) bt
#0  pthread_cond_wait@@GLIBC_2.3.2 () at ../nptl/sysdeps/unix/sysv/linux/x86_64/pthread_cond_wait.S:185
#1  0x00007f96731a510d in tsync_reader_entry (sync=0xc2d3118) at /work/obix/src/libs/tsync.c:160
#2  0x0000000000412b02 in device_notify_watches (dev=0xc2d3080, node=0xee7950)
    at /work/obix/src/server/device.c:1030
#3  0x0000000000413092 in device_del (href=0x7f9664003f80 "/obix/deviceRoot/example", 
    requester_id=0x7f966400a470 "UNDEFINED:UNDEFINED", sign_off=1) at /work/obix/src/server/device.c:1228
#4  0x000000000040be6f in handlerSignOff (request=0x7f96640079a0, uri=0x7f9664009c90 "/obix/signOff", 
    input=0x7f9664004160) at /work/obix/src/server/server.c:497
#5  0x000000000040c267 in obix_server_invoke (request=0x7f96640079a0, overrideUri=0x0, input=0x7f9664004160)
    at /work/obix/src/server/server.c:678
#6  0x000000000040c31b in obix_server_handlePOST (request=0x7f96640079a0, input=0x7f966400a490)
    at /work/obix/src/server/server.c:691
#7  0x000000000040a77e in obix_handle_request (request=0x7f96640079a0) at /work/obix/src/server/obix_fcgi.c:390
#8  0x000000000040a95e in payload (arg=0xeca7c0) at /work/obix/src/server/obix_fcgi.c:491
#9  0x0000003c78e07c65 in start_thread (arg=0x7f9672156700) at pthread_create.c:308
#10 0x0000003c786f5b9d in clone () at ../sysdeps/unix/sysv/linux/x86_64/clone.S:113
(gdb)
```

Above server thread has unregistered relevant device successfully but needs to notify any wathces monitoring the parent device before return. To this end, it needs to enter the "read region" of the parent device to find all watches, that's where it got blocked.

However, gdb further reveals that at this moment the number of writers on the parent device were as a matter of fact zero:

```
(gdb) fr 1
#1  0x00007f96731a510d in tsync_reader_entry (sync=0xc2d3118) at /work/obix/src/libs/tsync.c:160
160                     pthread_cond_wait(&sync->rq, &sync->mutex);
(gdb) p *sync
$1 = {being_shutdown = 0, readers = 1, writers = 0, running_readers = 0, running_writers = 0, rq = {__data = {
      __lock = 0, __futex = 3, __total_seq = 2, __wakeup_seq = 1, __woken_seq = 1, __mutex = 0xc2d31c0, 
      __nwaiters = 2, __broadcast_seq = 0}, 
    __size = "\000\000\000\000\003\000\000\000\002\000\000\000\000\000\000\000\001\000\000\000\000\000\000\000\001\000\000\000\000\000\000\000\300\061-\f\000\000\000\000\002\000\000\000\000\000\000", 
    __align = 12884901888}, wq = {__data = {__lock = 0, __futex = 4, __total_seq = 2, __wakeup_seq = 2, 
      __woken_seq = 2, __mutex = 0xc2d31c0, __nwaiters = 0, __broadcast_seq = 0}, 
    __size = "\000\000\000\000\004\000\000\000\002\000\000\000\000\000\000\000\002\000\000\000\000\000\000\000\002\000\000\000\000\000\000\000\300\061-\f", '\000' <repeats 11 times>, __align = 17179869184}, swq = {
    __data = {__lock = 0, __futex = 0, __total_seq = 0, __wakeup_seq = 0, __woken_seq = 0, __mutex = 0x0, 
      __nwaiters = 0, __broadcast_seq = 0}, __size = '\000' <repeats 47 times>, __align = 0}, mutex = {
    __data = {__lock = 0, __count = 0, __owner = 0, __nusers = 1, __kind = 0, __spins = 0, __list = {
        __prev = 0x0, __next = 0x0}}, __size = '\000' <repeats 12 times>, "\001", '\000' <repeats 26 times>, 
    __align = 0}}
(gdb)
```

If there is no writers, this server thread (a reader) can enter the "read region" successfully and immediately. So the only explanation is that this reader has been blocked by a writer but never got properly waken up when that writer exited. Turned out tsync_writer_exit() failed to wake up all blocked readers but just one of them.

## Synchronisation debug logs

When "DEBUG_TSYNC" macro is enabled, tsync.c APIs can throw out relevant debug information whenever a critial region is entered or exited. For example, following command:

	$ ./signUp & ./signOff  & ./signUp & ./signOff

will have shell fork four different sub shells to execute different scripts simultaneously, the oBIX server threads could output following logs (Note, the order in which sub shells run is uncertain):

```
/work/obix/src/server/server.c(507): SignOff "/obix/deviceRoot/example" : Provided href doesn't point to a valid device
/work/obix/src/libs/tsync.c(125): [32338] Writer entered, writers = 1, readers = 0 (0xdfd3078)
/work/obix/src/libs/tsync.c(116): [32339] New Writer waiting for existing writers = 2, readers = 0 (0xdfd3078)
/work/obix/src/libs/tsync.c(116): [32341] New Writer waiting for existing writers = 3, readers = 0 (0xdfd3078)
/work/obix/src/libs/tsync.c(165): [32338] Writer exited, writers = 2, readers = 0 (0xdfd3078)
/work/obix/src/libs/tsync.c(185): [32338] Reader begin sleeping, writers = 2, readers = 1 (0xdfd3078)
/work/obix/src/libs/tsync.c(125): [32339] Writer entered, writers = 2, readers = 1 (0xdfd3078)
/work/obix/src/libs/tsync.c(165): [32339] Writer exited, writers = 1, readers = 1 (0xdfd3078)
/work/obix/src/libs/tsync.c(185): [32339] Reader begin sleeping, writers = 1, readers = 2 (0xdfd3078)
/work/obix/src/libs/tsync.c(125): [32341] Writer entered, writers = 1, readers = 2 (0xdfd3078)
/work/obix/src/libs/tsync.c(165): [32341] Writer exited, writers = 0, readers = 2 (0xdfd3078)
/work/obix/src/libs/tsync.c(194): [32341] Reader entered, writers = 0, readers = 3, running_readers = 1 (0xdfd3078)
/work/obix/src/libs/tsync.c(226): [32341] Reader exited, writers = 0, readers = 2, running_readers = 0 (0xdfd3078)
/work/obix/src/libs/tsync.c(194): [32339] Reader entered, writers = 0, readers = 2, running_readers = 1 (0xdfd3078)
/work/obix/src/libs/tsync.c(226): [32339] Reader exited, writers = 0, readers = 1, running_readers = 0 (0xdfd3078)
/work/obix/src/libs/tsync.c(194): [32338] Reader entered, writers = 0, readers = 1, running_readers = 1 (0xdfd3078)
/work/obix/src/libs/tsync.c(226): [32338] Reader exited, writers = 0, readers = 0, running_readers = 0 (0xdfd3078)
```

How to decode above logs with each server thread's behaviour is a homework left for developers :-)
