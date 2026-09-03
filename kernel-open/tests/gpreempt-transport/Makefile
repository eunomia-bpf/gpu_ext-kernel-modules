CC ?= cc
CFLAGS ?= -O2
CFLAGS += -std=c11 -Wall -Wextra -Werror -I../../common/inc

.PHONY: all test clean
all: gpreempt_transport_test
gpreempt_transport_test: gpreempt_transport_test.c ../../common/inc/nv-gpreempt-transport.h \
    ../../common/inc/nv-gpu-timeslice-control.h ../../common/inc/nv-gpu-transition-validator.h \
    ../../common/inc/nv-gpu-rpc-diagnostic.h \
    ../../../src/nvidia/arch/nvalloc/unix/include/nv-gpu-sched-hooks.h
	$(CC) $(CFLAGS) -o $@ $<
test: gpreempt_transport_test
	./gpreempt_transport_test
clean:
	$(RM) gpreempt_transport_test
