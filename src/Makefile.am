bin_PROGRAMS = rftg
noinst_PROGRAMS = learner dumpnet dumprole
dist_bin_SCRIPTS = do_train

rftg_SOURCES = engine.c init.c ai.c gui.c net.c net.h rftg.h
learner_SOURCES = engine.c init.c ai.c learner.c net.c net.h rftg.h
dumpnet_SOURCES = net.c engine.c dumpnet.c init.c rftg.h net.h
dumprole_SOURCES = net.c engine.c dumprole.c init.c rftg.h net.h

dist_pkgdata_DATA = cards.txt images.data

rftg_CFLAGS = -Wall @GTK_CFLAGS@ -DDATADIR=\"$(pkgdatadir)\"
rftg_LDADD = @GTK_LIBS@

SUBDIRS = network

ACLOCAL_AMFLAGS = -I m4

EXTRA_DIST = config.rpath m4/ChangeLog
