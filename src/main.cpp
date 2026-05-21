#include <Arduino.h>
#include <SPI.h>
#include <LoRa.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <ESP32RotaryEncoder.h>
#include <WiFiManager.h> // https://github.com/tzapu/WiFiManager



// ============================================================
// PIN DEFINITIONS
// ============================================================ 

// LoRa
#define LORA_SS        5
#define LORA_RST       14
#define LORA_DIO0      2

// LCD
#define COLUMS    20 //LCD columns
#define ROWS      4  //LCD rows
LiquidCrystal_I2C lcd(PCF8574_ADDR_A21_A11_A01, 4, 5, 6, 16, 11, 12, 13, 14, POSITIVE);

// LEDs
#define RED_LED_PIN    15
#define YELLOW_LED_PIN  4
#define GREEN_LED_PIN   16

// Rotary Encoder
#define ROTARY_CLK_PIN  25
#define ROTARY_DT_PIN   26
#define ROTARY_SW_PIN   27

RotaryEncoder rotaryEncoder( ROTARY_CLK_PIN, ROTARY_DT_PIN, ROTARY_SW_PIN );

// wifi
WiFiManager wm;

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
bool RED_LED_STATE = LOW;
bool YELLOW_LED_STATE = LOW;
bool GREEN_LED_STATE = LOW;

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

  int xord = 0;

  String incoming = "";

  while (LoRa.available()) {
    incoming += (char)LoRa.read();
  }

  if (incomingLength != incoming.length()) {   // check length for error
    Serial.println("error: message length does not match length");
    return;                             // skip rest of function
  }

  // XOR decryption (symetric)
  if (recipient != 0xFF) { // if the message is unicast (not broadcast)
    String decrypted = "";
    for (int i = 0; i < incoming.length(); i++) {
      decrypted += (char)(incoming[i] ^ XOR_KEY);
    }
    incoming = decrypted;

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

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("F:" + String(sender, HEX));
  lcd.print(" T:" + String(recipient, HEX));
  lcd.print(" ID:" + String(incomingMsgId));
  lcd.setCursor(0, 1);
  lcd.print(incoming);
  lcd.setCursor(0, 3);
  lcd.print("RSSI: " + String(LoRa.packetRssi()));
  lcd.print(" Snr: " + String(LoRa.packetSnr()));

  // LED command from FF
  if (recipient == 0xFF) {
    if (incoming == "1") {
      RED_LED_STATE = HIGH;
    } 
    if (incoming == "0") {
      RED_LED_STATE = LOW;
    }
    digitalWrite(RED_LED_PIN, RED_LED_STATE);
  }
}

void knobCallback( long value )
{
    // This gets executed every time the knob is turned

    Serial.printf( "Value: %i\n", value );
}

void buttonCallback( unsigned long duration )
{
    // This gets executed every time the pushbutton is pressed

    Serial.printf( "boop! button was down for %u ms\n", duration );
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

void setupLeds() {
    pinMode(RED_LED_PIN, OUTPUT);
    pinMode(YELLOW_LED_PIN, OUTPUT);
    pinMode(GREEN_LED_PIN, OUTPUT);

    digitalWrite(RED_LED_PIN, LOW);
    digitalWrite(YELLOW_LED_PIN, LOW);
    digitalWrite(GREEN_LED_PIN, LOW); 
}

void setupRotaryEncoder() {
    // This tells the library that the encoder has its own pull-up resistors
    rotaryEncoder.setEncoderType( EncoderType::HAS_PULLUP );

    // Range of values to be returned by the encoder: minimum is 1, maximum is 10
    // The third argument specifies whether turning past the minimum/maximum will
    // wrap around to the other side:
    //  - true  = turn past 10, wrap to 1; turn past 1, wrap to 10
    //  - false = turn past 10, stay on 10; turn past 1, stay on 1
    rotaryEncoder.setBoundaries( 1, 10, true );

    // The function specified here will be called every time the knob is turned
    // and the current value will be passed to it
    rotaryEncoder.onTurned( &knobCallback );

    // The function specified here will be called every time the button is pushed and
    // the duration (in milliseconds) that the button was down will be passed to it
    rotaryEncoder.onPressed( &buttonCallback );

    // This is where the inputs are configured and the interrupts get attached
    rotaryEncoder.begin();
}

void setupWiFi() {
    wm.setConfigPortalBlocking(false);
    wm.setConfigPortalTimeout(60);
    //automatically connect using saved credentials if they exist
    //If connection fails it starts an access point with the specified name
    if(wm.autoConnect("AutoConnectAP")){
        Serial.println("connected...yeey :)");
    }
    else {
        Serial.println("Configportal running");
    }
}

void setup() {
    Serial.begin(115200);
    setupLora();
    setupLeds();
    setupRotaryEncoder();
    setupWiFi();

    while (lcd.begin(COLUMS, ROWS, LCD_5x8DOTS) != 1) //colums, rows, characters size
      {
        Serial.println(F("PCF8574 is not connected or lcd pins declaration is wrong. Only pins numbers: 4,5,6,16,11,12,13,14 are legal."));
        delay(5000);   
      }

      lcd.print(F("PCF8574 is OK...")); //(F()) saves string to flash & keeps dynamic memory free
      delay(2000);

      lcd.clear();
}

void loop() {

    wm.process();
    // Send a packet every 5 seconds
    if (millis() - lastTransmit > 5000) {
        lastTransmit = millis();
        // sendPacket();  
    }

    // Check for received packets
    onReceive(LoRa.parsePacket());
    
}