include $(top_srcdir)/build/modmake.rulesam
MODNAME=mod_battlecity
MOD_CFLAGS=`pkg-config --cflags --libs gstreamer-1.0`


mod_LTLIBRARIES = mod_battlecity.la
mod_battlecity_la_SOURCES  = mod_battlecity.c
mod_battlecity_la_CFLAGS   = $(AM_CFLAGS)
mod_battlecity_la_LIBADD   = $(switch_builddir)/libfreeswitch.la
mod_battlecity_la_LDFLAGS  = -avoid-version -module -no-undefined -shared
