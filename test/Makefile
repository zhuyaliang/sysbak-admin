localedir = /usr/share/locale
AM_CPPFLAGS = -DLOCALEDIR=\"$(localedir)\" -D_FILE_OFFSET_BITS=64
LDADD = $(LIBINTL) -lcrypto

LIBS+=-ltinfo
AM_CFLAGS=-D_FILE_OFFSET_BITS=64
AM_CFLAGS+=-DMEMTRACE -DMEMWATCH -DMW_STDIO
AM_CFLAGS+=-I/usr/include/ncursesw

main_files=main.c 
all:
	gcc -DEXTFS -Wall -I. main.c -o libtest `pkg-config --cflags --libs glib-2.0 gio-2.0 sysbak-admin`

clean:
	rm -f partclone.extfs
