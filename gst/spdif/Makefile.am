plugin_LTLIBRARIES = libgstspdif.la

libgstspdif_la_SOURCES = \
	plugin.c \
	gstbasespdifenc.c \
	gstaacspdifenc.c \
	gstaacspdifbin.c \
	gstaacspdifsink.c

libgstspdif_la_CFLAGS = \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_BASE_CFLAGS) \
	$(GST_CFLAGS)
libgstspdif_la_LIBADD = \
	$(GST_PLUGINS_BASE_LIBS) -lgsttag-$(GST_MAJORMINOR) \
	$(GST_BASE_LIBS) \
	$(GST_LIBS)
libgstspdif_la_LDFLAGS = $(PACKAGE_LIBS) $(GST_PLUGIN_LDFLAGS)
libgstspdif_la_LIBTOOLFLAGS = --tag=disable-static

noinst_HEADERS = \
	gstbasespdifenc.h \
	gstaacspdifenc.h \
	gstaacspdifbin.h \
	gstaacspdifsink.h
