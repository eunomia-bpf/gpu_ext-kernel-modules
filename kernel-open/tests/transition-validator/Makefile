CC ?= cc
CFLAGS ?= -O2
CFLAGS += -std=c11 -Wall -Wextra -Werror -I../../common/inc

.PHONY: all test clean

all: transition_validator_test

transition_validator_test: transition_validator_test.c ../../common/inc/nv-gpu-transition-validator.h
	$(CC) $(CFLAGS) -o $@ transition_validator_test.c

test: transition_validator_test
	./transition_validator_test

clean:
	$(RM) transition_validator_test
