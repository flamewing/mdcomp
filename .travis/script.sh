#!/bin/sh
chmod a+x ./autogen.sh
./autogen.sh && make -j $(nproc)

