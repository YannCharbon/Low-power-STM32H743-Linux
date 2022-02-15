# Usage guide

To create an application, create a folder inside this directory and add a Makefile with the following content :

<pre>
EXE=[desired executable name]
SRCS=$(wildcard *.c)
CFLAGS+=[optional compilation flags]
LDFLAGS+=[optional linker flags]

# Include the standard application Makefile
include ../appl.mk
</pre>

## Cross-compile
To cross-compile the app for the custom Linux run `make` inside the app folder.

## Compile for host
To compile the app for the host, run `gcc <sources>.c -o <executable name>`.