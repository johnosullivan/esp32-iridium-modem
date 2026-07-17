package main

import (
	"context"
	"errors"
	"fmt"
	"io"
	"log/slog"
	"net"
	"net/http"
	"os"
	"os/signal"
	"strings"
	"syscall"
	"time"

	"github.com/gin-gonic/gin"
)

const (
	defaultAddr     = ":8080"
	defaultMaxBody  = 64 << 10 // 64 KiB — RockBLOCK MO payloads are tiny
	shutdownTimeout = 10 * time.Second
	readHeaderTO    = 5 * time.Second
	readTO          = 10 * time.Second
	writeTO         = 10 * time.Second
	idleTO          = 60 * time.Second
)

type webhookResponse struct {
	Outcome WebhookOutcome `json:"outcome"`
	Message string         `json:"message,omitempty"`
}

func main() {
	logger := slog.New(slog.NewJSONHandler(os.Stdout, &slog.HandlerOptions{
		Level: slog.LevelInfo,
	}))
	slog.SetDefault(logger)

	if err := run(context.Background(), logger); err != nil {
		logger.Error("server stopped", "err", err)
		os.Exit(1)
	}
}

func run(ctx context.Context, logger *slog.Logger) error {
	addr := listenAddr()
	setGinMode()

	router := gin.New()
	router.Use(gin.Recovery())
	router.Use(requestLogger(logger))

	router.GET("/healthz", func(c *gin.Context) {
		c.JSON(http.StatusOK, gin.H{"status": "ok"})
	})
	router.POST("/", handleRockBlockWebhook(logger))

	srv := &http.Server{
		Addr:              addr,
		Handler:           router,
		ReadHeaderTimeout: readHeaderTO,
		ReadTimeout:       readTO,
		WriteTimeout:      writeTO,
		IdleTimeout:       idleTO,
		BaseContext: func(net.Listener) context.Context {
			return ctx
		},
	}

	errCh := make(chan error, 1)
	go func() {
		logger.Info("listening", "addr", addr)
		if err := srv.ListenAndServe(); err != nil && !errors.Is(err, http.ErrServerClosed) {
			errCh <- err
			return
		}
		errCh <- nil
	}()

	sigCtx, stop := signal.NotifyContext(ctx, os.Interrupt, syscall.SIGTERM)
	defer stop()

	select {
	case <-sigCtx.Done():
		logger.Info("shutdown signal received")
	case err := <-errCh:
		return err
	}

	shutdownCtx, cancel := context.WithTimeout(context.Background(), shutdownTimeout)
	defer cancel()
	if err := srv.Shutdown(shutdownCtx); err != nil {
		return fmt.Errorf("shutdown: %w", err)
	}
	return <-errCh
}

func handleRockBlockWebhook(logger *slog.Logger) gin.HandlerFunc {
	return func(c *gin.Context) {
		c.Request.Body = http.MaxBytesReader(c.Writer, c.Request.Body, defaultMaxBody)

		rawData, err := io.ReadAll(c.Request.Body)
		if err != nil {
			var maxBytesErr *http.MaxBytesError
			if errors.As(err, &maxBytesErr) {
				writeWebhookError(c, http.StatusRequestEntityTooLarge, WebhookOutcomeBadRequest, "body too large")
				return
			}
			logger.Warn("read body failed", "err", err)
			writeWebhookError(c, http.StatusBadRequest, WebhookOutcomeBadRequest, err.Error())
			return
		}

		message, err := ParseRockBlockMessage(rawData)
		if err != nil {
			logger.Warn("parse webhook failed", "err", err)
			writeWebhookError(c, http.StatusBadRequest, WebhookOutcomeParseError, err.Error())
			return
		}

		if err := message.Validate(); err != nil {
			logger.Warn("invalid webhook payload", "err", err)
			writeWebhookError(c, http.StatusBadRequest, WebhookOutcomeParseError, err.Error())
			return
		}

		attrs := []any{
			"imei", message.Imei,
			"serial", message.Serial,
			"momsn", message.Momsn,
			"transmit_time", message.TransmitTime,
			"lat", message.IridiumLatitude,
			"lon", message.IridiumLongitude,
			"cep_km", message.IridiumCep,
			"data_hex", message.Data,
		}
		if plain, decErr := message.DecodedData(); decErr == nil {
			attrs = append(attrs, "data_text", plain)
		}
		logger.Info("rockblock mo received", attrs...)

		// RockBLOCK expects HTTP 200 when the MO message is accepted.
		c.JSON(http.StatusOK, webhookResponse{Outcome: WebhookOutcomeAccepted})
	}
}

func writeWebhookError(c *gin.Context, status int, outcome WebhookOutcome, msg string) {
	c.JSON(status, webhookResponse{Outcome: outcome, Message: msg})
}

func requestLogger(logger *slog.Logger) gin.HandlerFunc {
	return func(c *gin.Context) {
		start := time.Now()
		c.Next()
		logger.Info(
			"http",
			"method", c.Request.Method,
			"path", c.FullPath(),
			"status", c.Writer.Status(),
			"latency_ms", time.Since(start).Milliseconds(),
			"client_ip", c.ClientIP(),
		)
	}
}

func listenAddr() string {
	if v := strings.TrimSpace(os.Getenv("ADDR")); v != "" {
		return v
	}
	if v := strings.TrimSpace(os.Getenv("PORT")); v != "" {
		if strings.Contains(v, ":") {
			return v
		}
		return ":" + v
	}
	return defaultAddr
}

func setGinMode() {
	if mode := strings.TrimSpace(os.Getenv("GIN_MODE")); mode != "" {
		gin.SetMode(mode)
		return
	}
	gin.SetMode(gin.ReleaseMode)
}
