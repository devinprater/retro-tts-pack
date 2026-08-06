CC = winegcc
CFLAGS ?= -O2 -Wall -Wextra
all: wintalker_cli.exe
wintalker_cli.exe: wintalker_cli.c
	$(CC) $(CFLAGS) -m64 -o $@ $<
clean:
	rm -f wintalker_cli.exe wintalker_cli.exe.so
.PHONY: all clean
