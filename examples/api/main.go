package main

import (
  "log"
  "encoding/json"

  "github.com/gin-gonic/gin"
  "github.com/joncalhoun/qson"
)

// rockblock message structure
type RockBlockMessage struct {
	Imei int64 					`json:"imei"`				// The unique IMEI of your RockBLOCK
	Serial int32 				`json:"serial"`				// The serial number of your RockBLOCK
	Momsn int32 				`json:"momsn"`				// The Message Sequence Number set by RockBLOCK when the message was sent from the device to the Iridium Gateway.
	TransmitTime string 		`json:"transmit_time"`		// The date & time (always UTC) that the message was transmitted.
	IridiumLatitude float64 	`json:"iridium_latitude"`	// The approximate latitude of the RockBLOCK at the time it transmitted.
	IridiumLongitude float64 	`json:"iridium_longitude"`	// The approximate longitude of the RockBLOCK at the time it transmitted.
	IridiumCep int 				`json:"iridium_cep"`		// An estimate of the accuracy (in km) of the position information in the previous two fields.
	Data string 				`json:"data"`				// Message hex-encoded.
}	

func main() {
  r := gin.Default()

  r.POST("/", func(c *gin.Context) {
	// retrieve the raw data from the HTTP request
    rawData, err := c.GetRawData()
	if err != nil {
		log.Println(err)
		c.JSON(400, gin.H{"message": err}) // return parsing error message
		return 
	}
	// convert the URL params to JSON form using "qson"
	b, err := qson.ToJSON(string(rawData))
	if err != nil {
		log.Println(err)
		c.JSON(400, gin.H{"message": err}) // return parsing error message
		return
	}
	log.Println("qson:", string(b))
	
	message := RockBlockMessage{}
	err = json.Unmarshal(b, &message)
	if err != nil {
		log.Println(err)
		c.JSON(400, gin.H{"message": err}) // return parsing error message
		return
	}

	log.Println("IRIDIUM_IMEI:", message.Imei)
	log.Println("IRIDIUM_SERIAL:", message.Serial)
	log.Println("IRIDIUM_MOMSN:", message.Momsn)
	log.Println("IRIDIUM_TRANSMIT_TIME:", message.TransmitTime)
	log.Println("IRIDIUM_LATITUDE:", message.IridiumLatitude)
	log.Println("IRIDIUM_LONGITUDE:", message.IridiumLongitude)
	log.Println("IRIDIUM_CEP:", message.IridiumCep)
	log.Println("IRIDIUM_DATA:", message.Data)
	
	// return a general 200 OK status to iridium service provider ex. RockBlock
	c.JSON(200, nil)
  })

  r.Run()
}