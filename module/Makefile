SYSDIR?= /usr/src/sys
PROJECTDIR?= ${.CURDIR:H}
DRMKMODDIR?= ${PROJECTDIR}/vendor/drm-kmod
KMODDIR?= /boot/modules

.if !exists(${DRMKMODDIR}/linuxkpi_version.mk)
.error The vendored DRM source is missing; use a complete project source archive
.endif
.include "${DRMKMODDIR}/kconfig.mk"

KMOD?= hypervdrm
SRCS?= hv_driver.c hv_vmbus.c hv_protocol.c hv_kms.c
.PATH: ${PROJECTDIR}/module
SRCS+= device_if.h bus_if.h vmbus_if.h fb_if.h pci_if.h vnode_if.h
SRCS+= opt_acpi.h opt_drm.h opt_platform.h opt_splash.h opt_syscons.h opt_teken.h
EXPORT_SYMS= NO

.include <bsd.kmod.mk>
.include "${DRMKMODDIR}/linuxkpi_version.mk"
.include "${DRMKMODDIR}/compiler_flags.mk"

CFLAGS+= -I${PROJECTDIR}/include -I${PROJECTDIR}/module
CFLAGS+= -I${DRMKMODDIR}/linuxkpi/gplv2/include
CFLAGS+= -I${DRMKMODDIR}/linuxkpi/bsd/include
CFLAGS+= -I${SYSDIR}/compat/linuxkpi/common/include
CFLAGS+= -I${SYSDIR}/compat/linuxkpi/dummy/include
CFLAGS+= -I${DRMKMODDIR}/include -I${DRMKMODDIR}/include/uapi
CFLAGS+= -I${DRMKMODDIR}/drivers/gpu/drm
CFLAGS+= -I${DRMKMODDIR}/drivers/gpu
CFLAGS+= '-DKBUILD_MODNAME="hypervdrm"' '-DLINUXKPI_PARAM_PREFIX=hypervdrm_'
CFLAGS+= ${KCONFIG:C/(.*)/-DCONFIG_\1/}
CWARNFLAGS+= -Wno-cast-qual -Wno-unused-parameter -Wno-gnu-pointer-arith

.if ${LINUXKPI_VERSION_NUMBER} != 61200
.error This module requires the DRM 6.12 API
.endif
.if ${MACHINE_ARCH} != "amd64"
.error Only FreeBSD amd64 is supported
.endif
