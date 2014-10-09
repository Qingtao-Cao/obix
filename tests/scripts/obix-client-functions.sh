#!/usr/bin/env bash

OBIX_XPATH_ERR_CONTRACT_ROOT="count(/err)"

function obix_is_err_contract()
{
	local v

	if [ ! -n "$1" ]; then 
		return 1
	fi

	v=$(xmllint --xpath $OBIX_XPATH_ERR_CONTRACT_ROOT --format - <<< "$1")

	if [ "$v" != "0" ]; then
			return 1;
	fi

	return 0;
}

export -f obix_is_err_contract
