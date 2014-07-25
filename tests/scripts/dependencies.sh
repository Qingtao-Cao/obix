#!/bin/bash

##############################################################################
# Copyright (c) 2013-2014 NEXTDC LTD.
#
# This file is part of oBIX.
#
# oBIX is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# oBIX is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with oBIX.  If not, see <http://www.gnu.org/licenses/>.
#
##############################################################################
 
rpm -qi lighttpd-fastcgi >/dev/null || ( echo "lighttpd-fastcgi rpm missing" ; exit 1 )
rpm -qi lighttpd         >/dev/null || ( echo "lighttpd rpm missing" ; exit 1 )
rpm -qi libxml2          >/dev/null || ( echo "libxml2  rpm missing" ; exit 1 )
rpm -qi curl             >/dev/null || ( echo "curl rpm missing" ; exit 1 )

exit 0
