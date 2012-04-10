include ../../../extra.mk

PLUGIN = highlight-overview${PLUGIN_SUFFIX}
SRCS = highlight-overview.c

include ../../../buildsys.mk

plugindir := ${plugindir}/plugins

CFLAGS += ${PLUGIN_CFLAGS}
CPPFLAGS += -I../../.. -I../.. -I../../common $(COMMON_CFLAGS) $(GUI_CFLAGS) -DLOCALEDIR=\"$(localedir)\" -DSHAREDIR=\"$(prefix)/share\"
LIBS += $(COMMON_LIBS) $(GUI_LIBS)

