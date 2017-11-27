plugindir = $(libdir)/xfce4/panel/plugins
plugin_LTLIBRARIES = libappletbatt.la

libappletbatt_la_CFLAGS =						\
	-DPACKAGE_LOCALE_DIR=\"$(localedir)\"			\
	@LIBXFCE4PANEL_CFLAGS@					\
	@LIBXFCE4UI_CFLAGS@ -g

libappletbatt_LDFLAGS = 						\
	-avoid-version 						\
	-module 						\
	-no-undefined 						\
	-export-symbols-regex '^xfce_panel_module_(preinit|init|construct)'

libappletbatt_la_LIBADD =						\
	@LIBXFCE4PANEL_LIBS@					\
	@LIBXFCE4UI_LIBS@

libappletbatt_la_SOURCES =		\
	main.c

desktopdir = $(datadir)/xfce4/panel/plugins
desktop_DATA = applet-batt.desktop

EXTRA_DIST = 								\
	applet-batt.desktop.in

DISTCLEANFILES =							\
	$(desktop_DATA)

%.desktop: %.desktop.in
	cp $< $@
