#include <Arduino.h>
#include <ArduinoOTA.h>
#include <SPI.h>
#include <LoRa.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <ESP32RotaryEncoder.h>
#include <WiFiManager.h> // https://github.com/tzapu/WiFiManager
#include <FS.h>
#include <SD.h>
#include <TinyGPSPlus.h>
#include <WebServer.h>
#include <ArduinoJson.h>
// ============================================================
// WEB SERVER / JSON
// ============================================================

WebServer server(80); // Create a web server object that listens for HTTP request on port 80
StaticJsonDocument<1024> jsonDocument;
char buffer[1024];



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
#define GREEN_LED_PIN   33

// Rotary Encoder
#define ROTARY_CLK_PIN  25
#define ROTARY_DT_PIN   26
#define ROTARY_SW_PIN   27

RotaryEncoder rotaryEncoder(ROTARY_CLK_PIN, ROTARY_DT_PIN, ROTARY_SW_PIN, -1, 2);

// SD Card
#define SD_CS_PIN  32
bool sdCardPresent = false;

// GPS
#define GPS_RX_PIN 16
#define GPS_TX_PIN 17
#define GPS_BAUD_RATE 9600
TinyGPSPlus gps;

HardwareSerial gpsSerial(2); // Use UART2 for GPS

unsigned long lastTransmit = 0;
unsigned long lastGPSFix = 0;

// ============================================================
// GPS STATE
// ============================================================

struct GPSState {
    float lat;
    float lon;
    float alt;
    int sats;
    float hdop;
    float speed;
    float course;
    uint32_t date;
    uint32_t time;
    bool valid;
};

GPSState gpsState = {0.0, 0.0, 0.0, 0, 99.9, 0.0, 0.0, 0, 0, false};

unsigned long lastGPSPrint = 0;  // Throttles serial print outputs

// wifi
WiFiManager wm;

// ============================================================
// CONFIGURATION
// ============================================================ 
byte NODE_ID                = 0x10;
byte BASE_ID                = 0xAA; 
#define XOR_KEY               0x6A
int scalingFactors[] = {7, 8, 9, 10, 11, 12}; // for SF7 to SF12

// ============================================================
// STATE MACHINE
// ============================================================ 

unsigned long msgCount = 0;
bool RED_LED_STATE = LOW;
bool YELLOW_LED_STATE = LOW;
bool GREEN_LED_STATE = LOW;

// ============================================================
// MENU & UI STATE
// ============================================================ 
enum MenuMode { DISPLAY_HOME, SELECT_ITEM, EDIT_ITEM };
MenuMode currentMode = DISPLAY_HOME;

int currentMenuIndex = 0; // 0 = SF, 1 = TX Power, 2 = Sync Word, 3 = Node ID
const int TOTAL_MENU_ITEMS = 4;

// Active settings
int currentSFIndex = 3;    // Default index for scalingFactors (9) -> SF10
int currentTxPower = 14;   // Default TX Power (2 to 20)
int currentSyncWord = 0x12; // Default LoRa sync word (0x12)
byte currentNodeID = 0x10; // Default Node ID (0x10)

bool updateScreenReq = true; // Flag to trigger screen redraws
unsigned long menuTimeout = 0; // To return to home screen after inactivity
// ============================================================
// Helpers
// ============================================================

void updateBottomMenu() {
    if (!updateScreenReq) return;
    updateScreenReq = false; // Reset flag

    lcd.setCursor(0, 3); // Go to the 4th row

    // Create string buffers for our 4 items
    char sfStr[7];
    char pwrStr[7];
    char syncStr[7];
    char nodeIdStr[7];

    sprintf(sfStr, "SF%d", scalingFactors[currentSFIndex]);
    sprintf(pwrStr, "P:%d", currentTxPower);
    sprintf(syncStr, "S:%02X", currentSyncWord);
    sprintf(nodeIdStr, "N:%02X", currentNodeID);
    

    // If we are in SELECT mode, wrap the item in brackets [ ]
    // If we are in EDIT mode, wrap the item in asterisks * * to show it's active
    if (currentMode == SELECT_ITEM || currentMode == EDIT_ITEM) {
        char lBrack = (currentMode == EDIT_ITEM) ? '*' : '[';
        char rBrack = (currentMode == EDIT_ITEM) ? '*' : ']';

        if (currentMenuIndex == 0) sprintf(sfStr, "%cSF%d%c", lBrack, scalingFactors[currentSFIndex], rBrack);
        if (currentMenuIndex == 1) sprintf(pwrStr, "%cP:%d%c", lBrack, currentTxPower, rBrack);
        if (currentMenuIndex == 2) sprintf(syncStr, "%cS:%02X%c", lBrack, currentSyncWord, rBrack);
        if (currentMenuIndex == 3) sprintf(nodeIdStr, "%cN:%02X%c", lBrack, currentNodeID, rBrack);
    }

    // Print to clean standard 20-character width layout
    // Format string ensures explicit spacing to prevent old characters hanging around
    char finalLine[21];
    snprintf(finalLine, sizeof(finalLine), "%-5s%-5s%-5s%-5s", sfStr, pwrStr, syncStr, nodeIdStr);
    lcd.print(finalLine);
}

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
  lcd.setCursor(0, 2);
  lcd.print("RSSI: " + String(LoRa.packetRssi()));
  lcd.print(" Snr: " + String(LoRa.packetSnr()));

  if(sdCardPresent == true){
    String logEntry = "From: 0x" + String(sender, HEX) + ", To: 0x" + String(recipient, HEX) + ", ID: " + String(incomingMsgId) + ", RSSI: " + String(LoRa.packetRssi()) + ", Snr: " + String(LoRa.packetSnr()) + ", Msg: " + incoming;
    File logFile = SD.open("/lora_log.txt", FILE_APPEND);
    if (logFile) {
      logFile.println(logEntry);
      logFile.close();
    } else {
      Serial.println("Error opening log file for writing.");
    }
  }

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
  updateScreenReq = true; // Force the bottom line to redraw itself cleanly
}

void knobCallback(long value) {
    menuTimeout = millis(); // Reset timeout on activity
    updateScreenReq = true;

    if (currentMode == DISPLAY_HOME) {
        // First turn wakes up the menu selection
        currentMode = SELECT_ITEM;
        rotaryEncoder.setBoundaries(0, TOTAL_MENU_ITEMS - 1, true);
        rotaryEncoder.setEncoderValue(currentMenuIndex);
    }
    else if (currentMode == SELECT_ITEM) {
        currentMenuIndex = value;
    }
    else if (currentMode == EDIT_ITEM) {
        // We are modifying a specific value
        if (currentMenuIndex == 0) { // Editing SF
            currentSFIndex = value;
        } else if (currentMenuIndex == 1) { // Editing TX Power
            currentTxPower = value;
        } else if (currentMenuIndex == 2) { // Editing Sync Word
            currentSyncWord = value;
        } else if (currentMenuIndex == 3) { // Editing Node ID
            currentNodeID = (byte)value;
        }
    }
}

void buttonCallback(unsigned long duration) {
    menuTimeout = millis();
    updateScreenReq = true;

    if (currentMode == DISPLAY_HOME) {
        // Pressing button on home screen enters selection mode
        currentMode = SELECT_ITEM;
        rotaryEncoder.setBoundaries(0, TOTAL_MENU_ITEMS - 1, true);
        rotaryEncoder.setEncoderValue(currentMenuIndex);
    }
    else if (currentMode == SELECT_ITEM) {
        // Selected an item! Switch to EDIT mode and update boundaries for that item
        currentMode = EDIT_ITEM;
        if (currentMenuIndex == 0) {
            // SF Array size is 6 (indexes 0 to 5)
            rotaryEncoder.setBoundaries(0, 5, false); 
            rotaryEncoder.setEncoderValue(currentSFIndex);
        } else if (currentMenuIndex == 1) {
            // TX Power range: 2dBm to 20dBm
            rotaryEncoder.setBoundaries(2, 20, false);
            rotaryEncoder.setEncoderValue(currentTxPower);
        } else if (currentMenuIndex == 2) {
            // Sync Word range: 0x00 to 0xFF (0 to 255)
            rotaryEncoder.setBoundaries(0, 255, false);
            rotaryEncoder.setEncoderValue(currentSyncWord);
        } else if (currentMenuIndex == 3) {
            // Node ID range: 0x00 to 0xFF (0 to 254)
            rotaryEncoder.setBoundaries(0, 254, false);
            rotaryEncoder.setEncoderValue(currentNodeID);
        }
    }
    else if (currentMode == EDIT_ITEM) {
        // Pressed again while editing -> Save settings and go back to selection
        
        // Apply the settings directly to the LoRa radio:
        if (currentMenuIndex == 0) {
            LoRa.setSpreadingFactor(scalingFactors[currentSFIndex]);
            Serial.println("Spreading Factor set to SF" + String(scalingFactors[currentSFIndex]));
        } else if (currentMenuIndex == 1) {
            LoRa.setTxPower(currentTxPower);
            Serial.println("TX Power set to " + String(currentTxPower) + " dBm");
        } else if (currentMenuIndex == 2) {
            LoRa.setSyncWord(currentSyncWord);
            Serial.println("Sync Word set to 0x" + String(currentSyncWord, HEX));
        } else if (currentMenuIndex == 3) {
            // Node ID is used in our code logic but not a LoRa setting, so just print it
            NODE_ID = currentNodeID;
            Serial.println("Node ID set to 0x" + String(currentNodeID, HEX));
        }
                
        // Drop back down to item selection mode
        currentMode = SELECT_ITEM;
        rotaryEncoder.setBoundaries(0, TOTAL_MENU_ITEMS - 1, true);
        rotaryEncoder.setEncoderValue(currentMenuIndex);
    }
}

// ============================================================
// LOG GPS DATA TO SD CARD
// ============================================================
void logGPSData(unsigned long msgCount = 0) {
    if (!sdCardPresent) return;

    String logEntry = String(msgCount) + "," + 
                      String(gpsState.lat, 5) + "," +
                      String(gpsState.lon, 5) + "," +
                      String(gpsState.alt, 1) + "," +
                      String(gpsState.sats) + "," +
                      String(gpsState.hdop, 1) + "," +
                      String(gpsState.speed, 1) + "," +
                      String(gpsState.course, 1) + "," +
                      String(gpsState.date) + "," +
                      String(gpsState.time);

    File logFile = SD.open("/gps_log.txt", FILE_APPEND);
    if (logFile) {
        logFile.println(logEntry);
        logFile.close();
    } else {
        Serial.println("Error opening GPS log file for writing.");
    }
}


// ============================================================
// SEND PACKET
// ============================================================

void sendPacket() {
    digitalWrite(YELLOW_LED_PIN, HIGH); // Status indicator for outbound packet

    char payload[96];
    char encrypted[96];
    unsigned long uptimeSeconds = millis() / 1000UL;

    // Construct comma-delimited data payload
    String payloadStr =
        String(msgCount) + "," +
        String(gpsState.lat, 5) + "," +
        String(gpsState.lon, 5) + "," +
        String(gpsState.alt, 1) + "," +
        String(gpsState.sats) + "," +
        String(gpsState.hdop, 1) + "," +
        String(gpsState.speed, 1) + "," +
        String(gpsState.course, 1) + "," +
        String(gpsState.date) + "," +
        String(gpsState.time) + "," +
        String(uptimeSeconds);

    payloadStr.toCharArray(payload, sizeof(payload));

    Serial.print(F("TX: ")); Serial.println(payload);

    // Apply symmetric XOR Encryption
    strncpy(encrypted, payload, sizeof(encrypted));
    for (size_t i = 0; i < strlen(encrypted); i++) {
        encrypted[i] ^= XOR_KEY;
    }
    uint8_t payloadLen = strlen(encrypted);

    // Broadcast construction
    LoRa.beginPacket();
    LoRa.write(BASE_ID);
    LoRa.write(NODE_ID);
    LoRa.write(msgCount);
    LoRa.write(payloadLen);
    LoRa.write((uint8_t*)encrypted, payloadLen);
    LoRa.endPacket();

    logGPSData(msgCount); // Log GPS data to SD card if present

    msgCount++;
    digitalWrite(YELLOW_LED_PIN, LOW); 
}

void processGPS() {
    // 1. READ GPS SERIAL CONSTANTLY (Prevents buffer overflow)
    while (gpsSerial.available() > 0) {
        gps.encode(gpsSerial.read());
    }

    // 2. EVALUATE FIX STATUS & CONTROL GREEN LED
    // TinyGPS+ location.isValid() ensures we have a true 2D/3D satellite fix
    if (gps.location.isValid() && gps.location.age() < 2000) {
        gpsState.lat = gps.location.lat();
        gpsState.lon = gps.location.lng();
        gpsState.alt = gps.altitude.meters();
        gpsState.sats = gps.satellites.value();
        gpsState.hdop = gps.hdop.value() / 100.0;
        gpsState.valid = true;
        gpsState.speed = gps.speed.mph(); // Update speed to ensure it's current
        gpsState.course = gps.course.value(); // Update course to ensure it's current
        gpsState.date = gps.date.value(); // Update date to ensure it's current
        gpsState.time = gps.time.value(); // Update time to ensure it's current

        digitalWrite(GREEN_LED_PIN, HIGH); // Steady light indicating active fix
    } else {
        gpsState.valid = false;
        digitalWrite(GREEN_LED_PIN, LOW);  // Turn off if fix is lost or stale
    }

    // 3. OPTIONAL: THROTTLE SERIAL LOGGING FOR DEBUGGING (Every 2 seconds)
    if (gpsState.valid && (millis() - lastGPSPrint > 2000)) {
        lastGPSPrint = millis();
        Serial.printf("Valid Fix! Sats: %d, Lat: %.5f, Lon: %.5f, HDOP: %.2f\n", 
                    gpsState.sats, gpsState.lat, gpsState.lon, gpsState.hdop);
    }

    // 4. TRANSMIT PACKET TIMER (Every 5 seconds - ONLY WITH VALID FIX)
    if (gpsState.valid && (millis() - lastTransmit > 5000)) {
        lastTransmit = millis();
        sendPacket(); 
    }

    // 5. MENU SYSTEMS & TIMEOUTS
    updateBottomMenu();

    if ((currentMode == SELECT_ITEM || currentMode == EDIT_ITEM) && millis() - menuTimeout > 30000) {
        currentMode = DISPLAY_HOME;
        updateScreenReq = true;
    }
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
    rotaryEncoder.setEncoderType( EncoderType::FLOATING );
    rotaryEncoder.buttonPressed();

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
    // wm.setConfigPortalTimeout(900);
    //automatically connect using saved credentials if they exist
    //If connection fails it starts an access point with the specified name
    if(wm.autoConnect("AutoConnectAP")){
        Serial.println("connected...yeey :)");
    }
    else {
        Serial.println("Configportal running");
    }
}

void setupOTA() {
    ArduinoOTA.setHostname("ESP32-Gantry");
    ArduinoOTA.onStart([]() {
        Serial.println("OTA Update Start");
    });
    ArduinoOTA.onEnd([]() {
        Serial.println("OTA Update End");
    });
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
        int percentage = (progress / (total /100));
        Serial.printf("OTA Progress: %u%%\n", percentage);
    });
    ArduinoOTA.onError([](ota_error_t error) {
        Serial.printf("OTA Error[%u]: ", error);
        if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
        else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
        else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
        else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
        else if (error == OTA_END_ERROR) Serial.println("End Failed");
    });
    ArduinoOTA.begin();
}

void setupSDCard() {
    if(!SD.begin(SD_CS_PIN)){
    Serial.println("Card Mount Failed");
    return;
  }
  uint8_t cardType = SD.cardType();

  if(cardType == CARD_NONE){
    Serial.println("No SD card attached");
    return;
  }

  Serial.print("SD Card Type: ");
  if(cardType == CARD_MMC){
    Serial.println("MMC");
  } else if(cardType == CARD_SD){
    Serial.println("SDSC");
  } else if(cardType == CARD_SDHC){
    Serial.println("SDHC");
  } else {
    Serial.println("UNKNOWN");
  }

  sdCardPresent = true;

  uint64_t cardSize = SD.cardSize() / (1024 * 1024);
  Serial.printf("SD Card Size: %lluMB\n", cardSize);
}

void setup() {
    Serial.begin(115200);
    // Start Serial 2 with the defined RX and TX pins and a baud rate of 9600
    gpsSerial.begin(GPS_BAUD_RATE, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);
    Serial.println("Serial 2 started at 9600 baud rate");
    setupLora();
    setupLeds();
    setupRotaryEncoder();
    setupWiFi();
    setupOTA();
    setupSDCard();

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
    ArduinoOTA.handle();
    processGPS();
    onReceive(LoRa.parsePacket());
}