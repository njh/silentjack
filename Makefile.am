AUTOMAKE_OPTIONS = foreign

AM_CFLAGS = -g -Wall @JACK_CFLAGS@
LIBS = -lm @JACK_LIBS@

bin_PROGRAMS = silentjack
silentjack_SOURCES = silentjack.c db.h

# Copy README.md to README when building distribution
dist-hook:
	[ -f README.md ] && cat README.md > README || true
