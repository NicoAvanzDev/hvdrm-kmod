# The top-level targets use POSIX make syntax (GNU make and FreeBSD make).
DRMKMODDIR= vendor/drm-kmod
SYSDIR= /usr/src/sys
KMODDIR= /boot/modules
PREFIX= /usr/local
DESTDIR=
SESSION_USER=

all:
	@sh tools/check-host.sh "$(SYSDIR)" "$(DRMKMODDIR)"
	sh tools/build.sh all "$(SYSDIR)" "$(DRMKMODDIR)" "$(KMODDIR)" "$(DESTDIR)"

install:
	@sh tools/check-host.sh "$(SYSDIR)" "$(DRMKMODDIR)"
	sh tools/build.sh install "$(SYSDIR)" "$(DRMKMODDIR)" "$(KMODDIR)" "$(DESTDIR)"
	sh tools/install-session.sh "$(PREFIX)" "$(DESTDIR)" "$(SESSION_USER)"

check:
	sh tests/run.sh

package: all
	sh tools/package.sh "$(DRMKMODDIR)"

clean:
	sh tools/build.sh clean "$(SYSDIR)" "$(DRMKMODDIR)" "$(KMODDIR)" "$(DESTDIR)"

.PHONY: all install check package clean
