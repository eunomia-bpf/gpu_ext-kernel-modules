CC ?= cc
CFLAGS ?= -O2
CFLAGS += -std=c11 -Wall -Wextra -Werror -I../../common/inc

.PHONY: all test clean
all: gpreempt_transport_test
gpreempt_transport_test: gpreempt_transport_test.c ../../common/inc/nv-gpreempt-transport.h
	$(CC) $(CFLAGS) -o $@ $<
test: gpreempt_transport_test
	./gpreempt_transport_test
clean:
	$(RM) gpreempt_transport_test
