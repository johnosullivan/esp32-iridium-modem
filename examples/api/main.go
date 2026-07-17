package main

import (
	"log"
	"net/http"
	"os"

	"github.com/gin-gonic/gin"
)

func main() {
	addr := ":8080"
	if v := os.Getenv("PORT"); v != "" {
		addr = ":" + v
	}

	r := gin.Default()
	r.POST("/", handleRockBlockWebhook)

	log.Printf("listening on %s", addr)
	if err := r.Run(addr); err != nil {
		log.Fatal(err)
	}
}

func handleRockBlockWebhook(c *gin.Context) {
	rawData, err := c.GetRawData()
	if err != nil {
		log.Printf("read body: %v", err)
		c.JSON(http.StatusBadRequest, gin.H{
			"outcome": WebhookOutcomeBadRequest,
			"message": err.Error(),
		})
		return
	}

	message, err := ParseRockBlockMessage(rawData)
	if err != nil {
		log.Printf("parse webhook: %v", err)
		c.JSON(http.StatusBadRequest, gin.H{
			"outcome": WebhookOutcomeParseError,
			"message": err.Error(),
		})
		return
	}

	log.Printf("IRIDIUM_IMEI: %d", message.Imei)
	log.Printf("IRIDIUM_SERIAL: %d", message.Serial)
	log.Printf("IRIDIUM_MOMSN: %d", message.Momsn)
	log.Printf("IRIDIUM_TRANSMIT_TIME: %s", message.TransmitTime)
	log.Printf("IRIDIUM_LATITUDE: %f", message.IridiumLatitude)
	log.Printf("IRIDIUM_LONGITUDE: %f", message.IridiumLongitude)
	log.Printf("IRIDIUM_CEP: %d", message.IridiumCep)
	log.Printf("IRIDIUM_DATA: %s", message.Data)

	// RockBLOCK expects HTTP 200 when the MO message is accepted.
	c.JSON(http.StatusOK, gin.H{"outcome": WebhookOutcomeAccepted})
}
