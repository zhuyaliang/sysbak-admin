localedir = /usr/share/locale
AM_CPPFLAGS = -DLOCALEDIR=\"$(localedir)\" -D_FILE_OFFSET_BITS=64
LDADD = $(LIBINTL) -lcrypto

LIBS+=-ltinfo
AM_CFLAGS=-D_FILE_OFFSET_BITS=64
AM_CFLAGS+=-DMEMTRACE -DMEMWATCH -DMW_STDIO
AM_CFLAGS+=-I/usr/include/ncursesw

main_files=main.c  progress.c checksum.c progress.h checksum.h bitmap.h sysbak-share.c sysbak-share.h
partclone_extfs_SOURCES=$(main_files) sysbak-extfs.c sysbak-extfs.h
all:
	gcc -DEXTFS -Wall -I. $(AM_CPPFLAGS) -o libtest $(partclone_extfs_SOURCES) `pkg-config --cflags --libs glib-2.0 gio-2.0` -lext2fs -lcom_err -lpthread

clean:
	rm -f partclone.extfs
