.PHONY: test
test:
	$(MAKE) -C test/host test

.PHONY: clean
clean:
	$(MAKE) -C test/host clean
