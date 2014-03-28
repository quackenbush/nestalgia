#!/bin/sh
make -j2 -f platform/win32/win32.mk
RESULT=$?

exit $RESULT
