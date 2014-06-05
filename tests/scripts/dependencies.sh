#!/bin/bash

rpm -qi lighttpd-fastcgi >/dev/null || ( echo "lighttpd-fastcgi rpm missing" ; exit 1 )
rpm -qi lighttpd         >/dev/null || ( echo "lighttpd rpm missing" ; exit 1 )
rpm -qi libxml2          >/dev/null || ( echo "libxml2  rpm missing" ; exit 1 )
rpm -qi curl             >/dev/null || ( echo "curl rpm missing" ; exit 1 )

exit 0
