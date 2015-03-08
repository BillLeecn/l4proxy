AM_CFLAGS = -I$(top_srcdir)/libev

#AM_LDFLAGS = -static

noinst_LIBRARIES = libev.a
libev_a_SOURCES = $(top_srcdir)/libev/ev.c

bin_PROGRAMS = l4proxyd
l4proxyd_SOURCES = main.c daemon.c proxy.c fifobuf.c \
                   backends/backend.c backends/redirect.c
l4proxyd_LDADD = libev.a
l4proxyd_CFLAGS = $(AM_CFLAGS) -Wall -Werror -Wno-error=strict-aliasing -Wno-error=unused-result
