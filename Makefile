CC = gcc
CFLAGS = -Wall -O2
LDFLAGS =

all: termux-monitor-iface

termux-monitor-iface: termux-monitor-iface.c
	$(CC) $(CFLAGS) -o termux-monitor-iface termux-monitor-iface.c $(LDFLAGS)

install: termux-monitor-iface
	install -d $(PREFIX)/bin
	install termux-monitor-iface $(PREFIX)/bin

clean:
	rm -f termux-monitor-iface
	rm -rf release/

release: all
	mkdir -p release/
	cp termux-monitor-iface release/
	strip -s release/termux-monitor-iface

