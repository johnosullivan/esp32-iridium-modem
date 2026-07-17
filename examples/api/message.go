package main

import (
	"encoding/json"
	"fmt"

	"github.com/joncalhoun/qson"
)

// RockBlockMessage is the RockBLOCK mobile-originated webhook payload.
// See https://docs.groundcontrol.com/iot/rockblock/web-services/receiving-mo
type RockBlockMessage struct {
	Imei             int64   `json:"imei"`              // Unique IMEI of the RockBLOCK
	Serial           int32   `json:"serial"`            // RockBLOCK serial number
	Momsn            int32   `json:"momsn"`             // MO message sequence number
	TransmitTime     string  `json:"transmit_time"`     // UTC time the message was transmitted
	IridiumLatitude  float64 `json:"iridium_latitude"`  // Approximate latitude at transmit time
	IridiumLongitude float64 `json:"iridium_longitude"` // Approximate longitude at transmit time
	IridiumCep       int     `json:"iridium_cep"`       // Position accuracy estimate in km
	Data             string  `json:"data"`              // Message payload, hex-encoded
}

// ParseRockBlockMessage converts raw application/x-www-form-urlencoded
// webhook bytes into a RockBlockMessage.
func ParseRockBlockMessage(raw []byte) (RockBlockMessage, error) {
	var message RockBlockMessage
	if len(raw) == 0 {
		return message, fmt.Errorf("empty webhook body")
	}

	asJSON, err := qson.ToJSON(string(raw))
	if err != nil {
		return message, fmt.Errorf("qson: %w", err)
	}

	if err := json.Unmarshal(asJSON, &message); err != nil {
		return message, fmt.Errorf("json: %w", err)
	}
	return message, nil
}
