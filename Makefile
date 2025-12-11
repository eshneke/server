CC = gcc
CFLAGS = -std=c11 -O2 -Wall -Wextra -D_XOPEN_SOURCE=700
TARGET = simple-server
SRCS = server.c

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) -o $@ $<

install:
	install -D $(TARGET) $(DESTDIR)/usr/bin/$(TARGET)
	install -D simple-server.service $(DESTDIR)/usr/lib/systemd/system/simple-server.service

uninstall:
	rm -f $(DESTDIR)/usr/bin/$(TARGET)
	rm -f $(DESTDIR)/usr/lib/systemd/system/simple-server.service

clean:
	rm -f $(TARGET)

.PHONY: clean install uninstall