#!/usr/bin/env bash
#set -x

##
#Valid oBIX object v1.1 as per wd-06 (pg. 16)
##
OBIX_XPATH_VALID_OBJECTS="obj|list|op|feed|ref|err|bool|int|real|str|enum|uri|abstime|reltime|date|time"
OBIX_XPATH_SELF_VALID_OBJECTS="self::obj|self::list|self::op|self::feed|self::ref|self::err|self::bool|self::int|self::real|self::str|self::enum|self::uri|self::abstime|self::reltime|self::date|self::time"

OBIX_XPATH_COUNT_VALUE_ROOT="count(/$OBIX_XPATH_VALID_OBJECTS[@val])"
OBIX_XPATH_COUNT_ERR_CONTRACT_ROOT="count(/err)"
OBIX_XPATH_COUNT_UNKNOWN_ELEMENTS="count(//*[not($OBIX_XPATH_SELF_VALID_OBJECTS)])"
OBIX_XPATH_COUNT_VALID_ROOT="count(/$OBIX_XPATH_VALID_OBJECTS)"

function obix_xpath_eval()
{
		local v

		if [ ! -n "$2" ]; then 
				v=$(xmllint --xpath "$1" --format -)
		else
				v=$(xmllint --xpath "$1" --format - <<< "$2")
		fi

		[ $? == 0 ] || return 0

		return $v
}

function obix_has_value()
{
		obix_xpath_eval $OBIX_XPATH_COUNT_VALUE_ROOT "$1"

		[ "$?" == 1 ] && return 0;

		return 1;
}

function obix_is_err_contract()
{
		obix_xpath_eval $OBIX_XPATH_COUNT_ERR_CONTRACT_ROOT "$1"
		
		[ "$?" == 1 ] && return 0;

		return 1;
}

function obix_has_unknown_elements()
{
		# this is buggy as hell, do not use for the moment.
		# tw
		obix_xpath_eval $OBIX_XPATH_COUNT_UNKNOWN_ELEMENTS "$1"

		[ "$?" == 1 ] && return 0;

		return 1;
}

function obix_valid_root()
{
		obix_xpath_eval $OBIX_XPATH_COUNT_VALID_ROOT "$1"
		
		[ "$?" == 1 ] && return 0;

		return 1;
}

export -f obix_has_unknown_elements
export -f obix_is_err_contract
export -f obix_has_value
export -f obix_valid_root
