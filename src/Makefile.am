bin_PROGRAMS = rftg
noinst_PROGRAMS = learner dumpnet
if BUILD_SERVER
noinst_PROGRAMS += server ai_client
endif
dist_bin_SCRIPTS = do_train

rftg_SOURCES = engine.c init.c ai.c loadsave.c gui.c net.c net.h rftg.h \
               client.c client.h comm.c comm.h
learner_SOURCES = engine.c init.c ai.c learner.c net.c net.h rftg.h
dumpnet_SOURCES = net.c dumpnet.c net.h
server_SOURCES = server.c engine.c init.c ai.c loadsave.c net.c net.h rftg.h \
		 comm.c comm.h
ai_client_SOURCES = ai_client.c engine.c init.c ai.c net.c net.h rftg.h comm.c \
			comm.h

dist_pkgdata_DATA = cards.txt campaign.txt images.data

AM_CFLAGS = -Wall

rftg_CFLAGS = -Wall @GTK_CFLAGS@ @GTK_MAC_CFLAGS@ -DRFTGDIR=\"$(pkgdatadir)\"
rftg_LDADD = @GTK_LIBS@ @GTK_MAC_LIBS@

server_LDADD = -lmysqlclient -lpthread

SUBDIRS = network

ACLOCAL_AMFLAGS = -I m4

EXTRA_DIST = config.rpath m4/ChangeLog osx
