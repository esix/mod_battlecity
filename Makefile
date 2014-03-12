MOD_CFLAGS=`pkg-config --cflags --libs gstreamer-1.0` `pkg-config --cflags --libs cairo`

LOCAL_OBJS=stream-controller.o gst-helper.o renderer.o world.o
local_depend: $(LOCAL_OBJS)

BASE=../../../..
include $(BASE)/build/modmake.rules
