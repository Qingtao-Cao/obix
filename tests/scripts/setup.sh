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


# Create a history directory for server
if [ ! -d tests/res/server/histories/ ]
then
	mkdir -p tests/res/server/histories/
fi

# Check the server has been built
if [ -f build/server/obix-fcgi ]
then
	cp -f build/server/obix-fcgi tests/obix-fcgi
else
	echo "Cannot find obix-fcgi. Exiting."
	exit 1
fi

# Create dir structure for lighttpd
mkdir -p tests/lighttpd/log tests/lighttpd/run tests/lighttpd/lib

exit 0
