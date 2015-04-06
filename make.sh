#!/bin/bash
libtool --mode=link gcc -Wall dvr.c -o dvr \
    $(pkg-config --cflags --libs gstreamer-1.0) \
    $(pkg-config --cflags --libs gtk+-3.0)

#-lgstinterfaces-1.0 -o dvr
