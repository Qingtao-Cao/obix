#!/usr/bin/env bash

source obix-client-functions.sh

OBIX_DOC=$(curl http://localhost:4242/obix)
echo "$OBIX_DOC"
