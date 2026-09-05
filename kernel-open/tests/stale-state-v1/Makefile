CC ?= cc
CFLAGS ?= -O2
CFLAGS += -std=c11 -Wall -Wextra -Werror -I../../common/inc

.PHONY: all test clean

all: stale_state_v1_test

stale_state_v1_test: stale_state_v1_test.c \
		../../common/inc/nv-gpu-stale-state-v1.h \
		../../common/inc/nvtypes.h
	$(CC) $(CFLAGS) -o $@ stale_state_v1_test.c

test: stale_state_v1_test
	./stale_state_v1_test

clean:
	$(RM) stale_state_v1_test
