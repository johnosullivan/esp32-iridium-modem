package main

import (
	"testing"
)

const sampleWebhook = "imei=300234060379030&serial=12345&momsn=42&transmit_time=23-10-16%2012%3A00%3A00&iridium_latitude=51.50&iridium_longitude=-0.12&iridium_cep=5&data=48656c6c6f"

func TestParseRockBlockMessage(t *testing.T) {
	t.Parallel()

	msg, err := ParseRockBlockMessage([]byte(sampleWebhook))
	if err != nil {
		t.Fatalf("ParseRockBlockMessage: %v", err)
	}
	if msg.Imei != 300234060379030 {
		t.Fatalf("imei: got %d", msg.Imei)
	}
	if msg.Momsn != 42 {
		t.Fatalf("momsn: got %d", msg.Momsn)
	}
	if msg.Data != "48656c6c6f" {
		t.Fatalf("data: got %q", msg.Data)
	}
}

func TestParseRockBlockMessageEmpty(t *testing.T) {
	t.Parallel()

	if _, err := ParseRockBlockMessage(nil); err == nil {
		t.Fatal("expected error for empty body")
	}
}

func TestWebhookOutcomeValid(t *testing.T) {
	t.Parallel()

	for _, name := range WebhookOutcomeNames() {
		if !WebhookOutcome(name).IsValid() {
			t.Fatalf("%q should be valid", name)
		}
	}
	if WebhookOutcome("nope").IsValid() {
		t.Fatal("invalid outcome reported as valid")
	}
}

func BenchmarkParseRockBlockMessage(b *testing.B) {
	raw := []byte(sampleWebhook)
	b.ReportAllocs()
	for i := 0; i < b.N; i++ {
		if _, err := ParseRockBlockMessage(raw); err != nil {
			b.Fatal(err)
		}
	}
}
