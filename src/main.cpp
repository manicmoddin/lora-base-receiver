#include <Arduino.h>
#include <SPI.h>
#include <LoRa.h>

// ============================================================
// PIN DEFINITIONS
// ============================================================ 

// LoRa
#define LORA_SS        5
#define LORA_RST       14
#define LORA_DIO0      2

// ============================================================
// CONFIGURATION
// ============================================================ 
byte NODE_ID                = 0xAA; 
#define XOR_KEY                0x6A

// ============================================================
// STATE MACHINE
// ============================================================ 

unsigned long lastTransmit = 0;
unsigned long msgCount = 0;

// ============================================================
// Helpers
// ============================================================

void onReceive(int packetSize) {
  if (packetSize == 0) return;          // if there's no packet, return

  // read packet header bytes:
  int recipient = LoRa.read();          // recipient address
  byte sender = LoRa.read();            // sender address
  byte incomingMsgId = LoRa.read();     // incoming msg ID
  byte incomingLength = LoRa.read();    // incoming msg length

  String incoming = "";

  while (LoRa.available()) {
    incoming += (char)LoRa.read();
  }

  if (incomingLength != incoming.length()) {   // check length for error
    Serial.println("error: message length does not match length");
    return;                             // skip rest of function
  }

  // if the recipient isn't this device or broadcast,
  if (recipient != NODE_ID && recipient != 0xFF) {
    Serial.println("This message is not for me.");
    // return;                             // skip rest of function
  }

  // if message is for this device, or broadcast, print details:
  Serial.println("Received from: 0x" + String(sender, HEX));
  Serial.println("Sent to: 0x" + String(recipient, HEX));
  Serial.println("Message ID: " + String(incomingMsgId));
  Serial.println("Message length: " + String(incomingLength));
  Serial.println("Message: " + incoming);
  Serial.println("RSSI: " + String(LoRa.packetRssi()));
  Serial.println("Snr: " + String(LoRa.packetSnr()));
  Serial.println();
}

void setupLora() {
    LoRa.setPins(LORA_SS, LORA_RST, LORA_DIO0);
    if (!LoRa.begin(433E6)) {
        Serial.println(F("LoRa init failed!"));
        while (1);
    }
    Serial.println(F("LoRa init succeeded."));
    LoRa.setTxPower(14); // 14 dBm is the default, and the maximum for the 433 MHz band
    LoRa.setSpreadingFactor(10); // SF7 is the default, but range increases as SF increases
    // LoRa.setSignalBandwidth(125E3); // 125 kHz is the default
    // LoRa.setCodingRate4(5); // 4/5 is the default  
    LoRa.receive();
} 

void setup() {
    Serial.begin(115200);
    setupLora();
}

void loop() {
    // Send a packet every 5 seconds
    if (millis() - lastTransmit > 5000) {
        lastTransmit = millis();
        // sendPacket();  
    }

    // Check for received packets
    onReceive(LoRa.parsePacket());
}