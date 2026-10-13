# Test-only RAM display: never installed or included in the package.
PROJECTDIR= ${.CURDIR:H:H}
KMOD= hypervdrm_fixture
SRCS= hv_fixture.c hv_kms.c hv_protocol.c
.include "${PROJECTDIR}/module/Makefile"
CFLAGS+= -DHV_TRACE
