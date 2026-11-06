CC ?= gcc
CFLAGS ?= -O2 -Wall -Wextra -fPIC
LDFLAGS ?= -shared -ldl

PREFIX ?= /usr
LIBDIR ?= $(PREFIX)/lib
SANEDIR ?= $(LIBDIR)/sane

TARGET = build/libbrother5_longpaper.so

all: $(TARGET)

build:
	mkdir -p build

$(TARGET): src/brother5_longpaper.c | build
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

clean:
	rm -rf build

install: all
	install -d $(DESTDIR)$(LIBDIR)
	install -m 755 $(TARGET) $(DESTDIR)$(LIBDIR)/libbrother5_longpaper.so
	@echo "Installation complete."

uninstall:
	rm -f $(DESTDIR)$(LIBDIR)/libbrother5_longpaper.so
	@echo "Uninstallation complete."

.PHONY: all clean install uninstall
