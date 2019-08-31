localedir = /usr/share/locale
AM_CPPFLAGS = -DLOCALEDIR=\"$(localedir)\" -D_FILE_OFFSET_BITS=64
LDADD = $(LIBINTL) -lcrypto

LIBS+=-ltinfo
AM_CFLAGS=-D_FILE_OFFSET_BITS=64
AM_CFLAGS+=-DMEMTRACE -DMEMWATCH -DMW_STDIO
AM_CFLAGS+=-I/usr/include/ncursesw

main_files=main.c partclone.c progress.c checksum.c torrent_helper.c partclone.h progress.h gettext.h checksum.h torrent_helper.h bitmap.h
partclone_extfs_SOURCES=$(main_files) extfsclone.c extfsclone.h
all:
	gcc -DEXTFS -Wall -I. $(AM_CPPFLAGS) -o partclone.extfs $(partclone_extfs_SOURCES) `pkg-config --cflags --libs glib-2.0` -lext2fs -lcom_err -lpthread -lcrypto

clean:
	rm -f partclone.extfs
