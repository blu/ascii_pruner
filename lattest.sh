#!/bin/bash

if [ -z `which time` ]; then
	echo "error: time not found"
	exit 255
fi
if [ -z `which bc` ]; then
	echo "error: bc not found"
	exit 254
fi

CFLAGS=(
	-O3
	-fno-rtti
	-fno-exceptions
	-fstrict-aliasing
)

if [[ $HOSTTYPE == "arm" ]]; then
	if [ -z $CC ] || [ ! -e $CC ]; then
		echo "error: envvar CC must hold the path to aarch64 compiler"
		exit 252
	fi
elif [[ $HOSTTYPE == "aarch64" ]]; then
	if [ -z $CC ]; then
		CC=g++
	fi
	if [ -z `which $CC` ]; then
		echo "error: $CC not found"
		exit 253
	fi
elif [[ $HOSTTYPE == "x86_64" ]]; then
	if [ -z $CC ]; then
		CC=g++
	fi
	if [ -z `which $CC` ]; then
		echo "error: $CC not found"
		exit 253
	fi
	CFLAGS+=(
		-mssse3
	)
else
	echo "error: unsupported host type"
	exit 251
fi

${CC} ${CFLAGS[@]} lattest.cpp -o lattest_nocoissue
`which time` -f %e ./lattest_nocoissue 2>&1 | xargs -i echo "scale=4;" {} " * " `lscpu | grep -m 1 -E "^CPU[[:space:][:alpha:]]+MHz" | sed "s/^[^:]\+://g"` " * 10^6 / (5 * 10^8 * 16)" | bc

${CC} ${CFLAGS[@]} lattest.cpp -o lattest_coissue -DCOISSUE
`which time` -f %e ./lattest_coissue 2>&1 | xargs -i echo "scale=4;" {} " * " `lscpu | grep -m 1 -E "^CPU[[:space:][:alpha:]]+MHz" | sed "s/^[^:]\+://g"` " * 10^6 / (5 * 10^8 * 16)" | bc
