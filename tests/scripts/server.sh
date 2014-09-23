#!/bin/bash

start() {
    echo -n "Starting obix-server: "
    lighttpd -f tests/lighttpd/lighttpd.conf &
    RETVAL=$?
    echo
    return $RETVAL
}

stop() {
    echo -n "Stopping obix-server: "
    killall lighttpd
    RETVAL=$?
    return $RETVAL
}

case "$1" in
    start)
        start && exit 0
        ;;
    stop)
        stop || exit 0
        ;;
    *)
        echo $"Usage: $0 {start|stop}"
        exit 2
esac
exit $?

