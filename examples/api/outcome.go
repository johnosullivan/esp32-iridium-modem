package main

//go:generate go run github.com/abice/go-enum@v0.6.0 -f=$GOFILE --marshal --names

// WebhookOutcome is the result of handling a RockBLOCK MO webhook.
// ENUM(accepted, bad_request, parse_error)
type WebhookOutcome string
