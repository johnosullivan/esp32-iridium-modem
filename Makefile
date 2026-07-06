.PHONY: test replay sim-dry-run
test:
	$(MAKE) -C test/host test

replay:
	$(MAKE) -C test/host replay

sim-dry-run:
	python3 scripts/modem_sim.py --fixture test/host/fixtures/mo_send_success.txt --dry-run

.PHONY: clean
clean:
	$(MAKE) -C test/host clean
