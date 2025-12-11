#!/bin/bash

if [ -z `which time` ]; then
	echo "error: time not found"
	exit 255
fi
if [ -z `which bc` ]; then
	echo "error: bc not found"
	exit 254
fi

if [[ `uname` == "Darwin" ]]; then
	# it is not trivial to look up the p-cores clock on macOS -- leave that to user
	if [[ $# -ne 1 ]]; then
		echo "usage: $0 p-core-MHz"
		exit 250
	fi
	CLOCK=$1
else
	CLOCK=`lscpu | grep -m 1 -E "^CPU[[:space:][:alpha:]]+MHz" | sed "s/^[^:]\+:[[:space:]]\+//g"`
fi

CFLAGS=(
	-O3
	-fno-rtti
	-fno-exceptions
	-fstrict-aliasing
)

if [[ ${HOSTTYPE:0:3} == "arm" ]]; then
	if [ -z $CC ] || [ ! -e $CC ]; then
		echo "error: envvar CC must hold the path to aarch64 compiler"
		exit 252
	fi
elif [[ ${HOSTTYPE} == "aarch64" ]]; then
	if [ -z $CC ]; then
		CC=g++
	fi
	if [ -z `which $CC` ]; then
		echo "error: $CC not found"
		exit 253
	fi
elif [[ ${HOSTTYPE} == "x86_64" ]]; then
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
${CC} ${CFLAGS[@]} lattest.cpp -o lattest_coissue -DCOISSUE

if [[ `uname` == "Darwin" ]]; then
	TIME_A=$(zsh -c "TIMEFMT=%U; time ./lattest_nocoissue" 2>&1); TIME_A=${TIME_A%?} # strip trailing 's'
	TIME_B=$(zsh -c "TIMEFMT=%U; time ./lattest_coissue" 2>&1); TIME_B=${TIME_B%?} # strip trailing 's'
else
	TIME_A=$(`which time` -f %e ./lattest_nocoissue 2>&1)
	TIME_B=$(`which time` -f %e ./lattest_coissue 2>&1)
fi
echo "scale=4; ${TIME_A} * ${CLOCK} * 10^6 / (5 * 10^8 * 16)" | bc
echo "scale=4; ${TIME_B} * ${CLOCK} * 10^6 / (5 * 10^8 * 16)" | bc
