package main

import (
	"bytes"
	"encoding/json"
	"io"
	"log/slog"
	"net/http"
	"net/http/httptest"
	"testing"

	"github.com/gin-gonic/gin"
)

func TestHandleRockBlockWebhookAccepted(t *testing.T) {
	t.Parallel()
	gin.SetMode(gin.TestMode)

	router := gin.New()
	router.POST("/", handleRockBlockWebhook(slog.Default()))

	req := httptest.NewRequest(http.MethodPost, "/", bytes.NewBufferString(sampleWebhook))
	req.Header.Set("Content-Type", "application/x-www-form-urlencoded")
	rec := httptest.NewRecorder()
	router.ServeHTTP(rec, req)

	if rec.Code != http.StatusOK {
		t.Fatalf("status: got %d body %s", rec.Code, rec.Body.String())
	}

	var resp webhookResponse
	if err := json.Unmarshal(rec.Body.Bytes(), &resp); err != nil {
		t.Fatalf("decode: %v", err)
	}
	if resp.Outcome != WebhookOutcomeAccepted {
		t.Fatalf("outcome: got %q", resp.Outcome)
	}
}

func TestHandleRockBlockWebhookEmptyBody(t *testing.T) {
	t.Parallel()
	gin.SetMode(gin.TestMode)

	router := gin.New()
	router.POST("/", handleRockBlockWebhook(slog.Default()))

	req := httptest.NewRequest(http.MethodPost, "/", http.NoBody)
	rec := httptest.NewRecorder()
	router.ServeHTTP(rec, req)

	if rec.Code != http.StatusBadRequest {
		t.Fatalf("status: got %d", rec.Code)
	}

	body, _ := io.ReadAll(rec.Body)
	var resp webhookResponse
	if err := json.Unmarshal(body, &resp); err != nil {
		t.Fatalf("decode: %v", err)
	}
	if resp.Outcome != WebhookOutcomeParseError && resp.Outcome != WebhookOutcomeBadRequest {
		t.Fatalf("outcome: got %q", resp.Outcome)
	}
}

func TestListenAddr(t *testing.T) {
	t.Setenv("ADDR", "")
	t.Setenv("PORT", "")
	if got := listenAddr(); got != defaultAddr {
		t.Fatalf("default: got %q", got)
	}

	t.Setenv("PORT", "9090")
	if got := listenAddr(); got != ":9090" {
		t.Fatalf("port: got %q", got)
	}

	t.Setenv("ADDR", "127.0.0.1:7070")
	if got := listenAddr(); got != "127.0.0.1:7070" {
		t.Fatalf("addr: got %q", got)
	}
}
