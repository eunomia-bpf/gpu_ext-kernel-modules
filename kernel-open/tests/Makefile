.PHONY: all test clean

all:
	$(MAKE) -C transition-validator all
	$(MAKE) -C stale-state-v1 all

test:
	$(MAKE) -C transition-validator test
	$(MAKE) -C stale-state-v1 test

clean:
	$(MAKE) -C transition-validator clean
	$(MAKE) -C stale-state-v1 clean
