# RockBLOCK MO webhook example (Go)

Small Gin server that accepts RockBLOCK mobile-originated webhooks (form-encoded),
parses them into a typed payload, and acknowledges with HTTP 200.

## Quick start

```bash
cd examples/api
go mod tidy
go run .
# POST form body to http://localhost:8080/
```

Override the listen port with `PORT=9090 go run .`.

## Productivity tooling

Wired for the workflow described in
[These Go productivity tools will drastically improve your workflow](https://blog.stackademic.com/these-go-productivity-tools-will-drastically-improve-your-workflow-d34dbd6d9bb4):

| Tool | Purpose | Command |
|------|---------|---------|
| **gopls** | Language server (editor intelligence) | installed via `make tools` |
| **golangci-lint** | Parallel lint suite (`errcheck`, `staticcheck`, `gofumpt`, …) | `make lint` |
| **air** | Live reload on `.go` changes | `make run` |
| **dlv** (Delve) | Interactive debugger | `make debug` |
| **benchstat** | Statistical benchmark comparison | `make bench` then `make benchstat BEFORE=… AFTER=…` |
| **go-enum** | Enum codegen for `WebhookOutcome` | `make generate` |
| **gofumpt** | Stricter `gofmt` | `make fmt` |

Install everything once:

```bash
make tools
```

Day-to-day loop:

```bash
make run          # air watches and restarts
make lint         # before push
make test
make bench        # writes bench.txt for benchstat
```

## Layout

| File | Role |
|------|------|
| `main.go` | HTTP server + webhook handler |
| `message.go` | `RockBlockMessage` + `ParseRockBlockMessage` |
| `outcome.go` | `go-enum` source for `WebhookOutcome` |
| `outcome_enum.go` | Generated (`go generate`) — do not edit |
| `message_test.go` | Unit tests + `BenchmarkParseRockBlockMessage` |
| `.golangci.yml` | Linter set from the article |
| `.air.toml` | Live-reload config |

## gopls (editor)

Optional VS Code / Cursor `settings.json` snippet from the article:

```json
"gopls": {
  "ui.inlayhint.hints": {
    "parameterNames": true,
    "assignVariableTypes": true
  },
  "analyses": {
    "unusedparams": true,
    "shadow": true
  }
}
```
