# The top-level targets use POSIX make syntax (GNU make and FreeBSD make).
DRMKMODDIR= vendor/drm-kmod
SYSDIR= /usr/src/sys
KMODDIR= /boot/modules

all:
	@sh tools/check-host.sh "$(SYSDIR)" "$(DRMKMODDIR)"
	sh tools/build.sh module "$(SYSDIR)" "$(DRMKMODDIR)" "$(KMODDIR)"

deps:
	sh tools/fetch-deps.sh "$(DRMKMODDIR)"

core:
	@sh tools/check-host.sh "$(SYSDIR)" "$(DRMKMODDIR)"
	sh tools/build.sh core "$(SYSDIR)" "$(DRMKMODDIR)" "$(KMODDIR)"

install:
	@sh tools/check-host.sh "$(SYSDIR)" "$(DRMKMODDIR)"
	sh tools/build.sh install "$(SYSDIR)" "$(DRMKMODDIR)" "$(KMODDIR)"

check:
	sh tests/run.sh

package:
	sh tools/package.sh "$(DRMKMODDIR)"

clean:
	sh tools/build.sh clean "$(SYSDIR)" "$(DRMKMODDIR)" "$(KMODDIR)"

.PHONY: all deps core install check package clean
