include ../../../extra.mk

PLUGIN = message-overview${PLUGIN_SUFFIX}
SRCS = message-overview.c

include ../../../buildsys.mk

plugindir := ${plugindir}/plugins

CFLAGS += ${PLUGIN_CFLAGS}
CPPFLAGS += -I../../.. -I../.. -I../../common $(COMMON_CFLAGS) $(GUI_CFLAGS) -DLOCALEDIR=\"$(localedir)\" -DSHAREDIR=\"$(prefix)/share\"
LIBS += $(COMMON_LIBS) $(GUI_LIBS)

