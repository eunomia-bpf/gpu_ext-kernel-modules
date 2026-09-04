CC ?= cc
CFLAGS ?= -O2
CFLAGS += -std=c11 -Wall -Wextra -Werror -I../../common/inc \
	-I../../../src/nvidia/arch/nvalloc/unix/include

.PHONY: all test semantic-test clean

all: sched_init_diagnostic_test

sched_init_diagnostic_test: sched_init_diagnostic_test.c \
	../../common/inc/nv-gpu-sched-init-diagnostic.h \
	../../../src/nvidia/arch/nvalloc/unix/include/nv-gpu-sched-hooks.h
	$(CC) $(CFLAGS) -o $@ $<

semantic-test: semantic_source_test.sh
	./semantic_source_test.sh \
		../../../src/nvidia/src/kernel/gpu/fifo/kernel_channel_group_api.c \
		../../nvidia/nv-gpu-sched-hooks.c

test: sched_init_diagnostic_test semantic-test
	./sched_init_diagnostic_test

clean:
	$(RM) sched_init_diagnostic_test
