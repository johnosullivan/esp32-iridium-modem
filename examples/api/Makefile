.PHONY: tools generate fmt lint test bench benchstat run debug tidy

# Tooling from:
# https://blog.stackademic.com/these-go-productivity-tools-will-drastically-improve-your-workflow-d34dbd6d9bb4

tools:
	go install golang.org/x/tools/gopls@latest
	go install github.com/golangci/golangci-lint/cmd/golangci-lint@latest
	go install github.com/air-verse/air@latest
	go install github.com/go-delve/delve/cmd/dlv@latest
	go install golang.org/x/perf/cmd/benchstat@latest
	go install github.com/abice/go-enum@latest
	go install mvdan.cc/gofumpt@latest

generate:
	go generate ./...

fmt:
	gofumpt -l -w .

lint:
	golangci-lint run ./...

test:
	go test ./...

bench:
	go test -bench=. -benchmem -count=6 ./... | tee bench.txt

# Compare two saved benchstat runs: make benchstat BEFORE=before.txt AFTER=after.txt
benchstat:
	@test -n "$(BEFORE)" -a -n "$(AFTER)" || (echo "usage: make benchstat BEFORE=before.txt AFTER=after.txt" >&2; exit 1)
	benchstat $(BEFORE) $(AFTER)

run:
	air

debug:
	dlv debug .

tidy:
	go mod tidy
