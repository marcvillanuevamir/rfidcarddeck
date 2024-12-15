/***************************************************
   CODI EL CONSPIRADOR (O CONSPIRADORA) - MFRC522 via WiFI (OSC)
   Written by Marc Villanueva Mir marcvillanuevamir@gmail.com
   Last edit: 15/12/2024
   Hardware:
  - Arduino Uno R4 WiFi
  - MFRC522
  - NTAG213 NFC tags

  MFRC522 pinout:
  VCC 3.3V  ||  RST 9 ||  GND GND ||  MISO 12 ||  MOSI 11 ||  SCK 13  ||  NSS 10
 ****************************************************/

#include <SPI.h>
#include <MFRC522.h>  // https://github.com/miguelbalboa/rfid
#include "Arduino.h"
#include <ArduinoOSCWiFi.h>  //https://github.com/hideakitai/ArduinoOSC
#include "Arduino_LED_Matrix.h"
ArduinoLEDMatrix matrix;

//toggle to turn on/off debugging
#define DEBUG true  // default: true. change to false in order to prevent serial communication with computer
#define DEBUG_SERIAL \
  if (DEBUG) Serial

// ARDUINO ID
const char id_address = 'A';           // specify the ID of the board you are programming (A-B-C)
const uint8_t numCards = 30;           // specify how many cards are used
const uint8_t numCandidateCards = 23;  // first candidate card (23-30)

// WiFi stuff
const char* ssid = "your-ssid";  // network ssid (update)
const char* pwd = "your-pwd";    // network password (update)

IPAddress ip;  // Arduino's static IP is defined according to the ID
const IPAddress gateway(192, 168, 10, 1);
const IPAddress subnet(255, 255, 255, 0);

// for ArduinoOSC
const char* host = "192.168.10.46";  // computer's IP address (update)
const int recv_port = 54321;         // Arduino receive port
const int qlab_port = 53000;         // QLab listening port
const int max_port = 5000;           // Max listening port
const int ableton_port = 7404;       // Ableton listening port
const IPAddress outIP[] = {
  IPAddress(192, 168, 10, 201),
  IPAddress(192, 168, 10, 202),
  IPAddress(192, 168, 10, 203)
};

// message codes (from QLab/ Max)
const int idle = 0;
const int openGate = 123;
const int closeGate = 124;
const int matrixOn = 61;
const int matrixOff = 62;
const int randomOn = 35;
const int randomOff = 36;
const int ledEmergencia = 39;
const int emergenciaOff = 40;
const int ledFinal = 41;
const int emergency = 251;
const int premissa = 252;
const int counterReset = 254;
const int endGame = 255;

// message codes (to QLab/ Max)
const int callback = 101;

// send / receive variables
int received = idle;  // incoming messages (from QLab)
int counter = idle;

int cardArray[numCards];  // memory holder of cards that have been played so far

// led variables
uint8_t r;
uint8_t g;
uint8_t b;
uint8_t ledTarget[3];  // array for led processing (R, G, B)
uint8_t m = 0;         // master for led brightness control (0-255)

// frames for the led matrix
const uint32_t red[] = {
  0xffffffff,
  0xffffffff,
  0xffffffff,
};
const uint32_t clear[] = {
  0, 0, 0, 0xFFFF
};

// GPIO pins, variables canals dels leds, dades de temps, flags
const uint8_t rPin = 3;
const uint8_t gPin = 5;
const uint8_t bPin = 6;
const uint8_t ledPin[3] = { rPin, gPin, bPin };

// variables per emmagatzemar dades de temps
unsigned long currentMillis = 0;   // variable per emmagatzemar els milisegons enregistrats per l'arduino
unsigned long previousMillis = 0;  // variable per emmagatzemar els últims segons enregistrats
unsigned long interval = 1000;     // variable per definir l'interval d'espera entre una activació d'un botó i la següent

//global declarations
#define RST_PIN 9                  // define reset pin for RC522
#define SS_PIN 10                  // define SS pin (SDA) for RC522
MFRC522 mfrc522(SS_PIN, RST_PIN);  // create and name RC522 object
MFRC522::StatusCode status;        // variable to get card status
bool sendOSC(int card, int count);

// see write_ntag213 sketch
uint8_t buffer[18];  // data transfer buffer (16+2 bytes data+CRC)
uint8_t size = sizeof(buffer);
String readData = "";          // string variable to store read data
String currentData = "ready";  // store last read data
uint8_t pageAddr = 0x06;       // We will read 16 bytes (page 6,7,8 and 9).

// flags
bool isConnected = false;      // flag when the connection to the network has been confirmed
bool gateOpen = true;          // toggle the reading
bool lastGateState = false;    // store the last gate state to check if it has changed
bool emergencyState = false;   // toggle the emergency state
bool emergencyReady = false;   // whether is ready to enter the emergency state
bool globalEmergency = false;  // whether the emergency state has been already declared or not
bool premissaReady = false;    // whether is ready to enter the final scene

// led flags
bool ledRandomOn = false;
bool ledFadeOn = false;
bool ledEmergenciaOn = false;
bool ledEmergenciaOff = false;
bool ledFinalOn = false;

// constant OSC addresses
const String counter_address = String("/arduino/") + id_address + String("/counter");
const String gate_address = String("/arduino/") + id_address + String("/gate");
const String error_address = String("/arduino/") + id_address + String("/error");
const String text_address = String("/arduino/") + id_address + String("/text");
const String fadeStart_msg = String("/cue/") + id_address + String("fade") + "/start";
const String start_emergency_msg = String("/cue/") + String(id_address) + String("SEM/start");
const String join_emergency_msg = String("/cue/") + String(id_address) + String("JEM/start");
const String start_premissa_msg = String("/cue/PREMISSA/start");

// Format and send OSC message to QLab
bool sendOSC(int card, int count) {
  if (!emergencyReady && !premissaReady) {
    if (count <= 2 && card < numCandidateCards) {                                    // start sugar subset for the first scene
      String countNumber = String(id_address) + String(id_address) + String(count);  // example: BB2
      String count_result = "/cue/" + countNumber + "/start";                        // send text start message
      OscWiFi.send(host, qlab_port, count_result);
      delay(10);
      OscWiFi.send(host, max_port, counter_address, count);
      Serial.println(count_result);
    } else {
      String cueNumber = id_address + String(card);         // example: B2
      String card_result = "/cue/" + cueNumber + "/start";  // send cue start message
      OscWiFi.send(host, qlab_port, card_result);
      Serial.println(card_result);
      delay(10);
      OscWiFi.send(host, max_port, counter_address, count);
      delay(10);
      // fade out if nuclear is active
      if (emergencyState) {
        OscWiFi.send(host, qlab_port, fadeStart_msg);
        DEBUG_SERIAL.println("Fading out...");
      }
    }
  } else if (emergencyReady) {
    if (card >= numCandidateCards) {  // don't trigger the emergency state if a candidate was killed
      OscWiFi.send(host, max_port, error_address, "Candidate delayed Emergency");
      DEBUG_SERIAL.println("Candidate delayed Emergency");
      String cueNumber = id_address + String(card);         // example: B2
      String card_result = "/cue/" + cueNumber + "/start";  // send cue start message
      OscWiFi.send(host, qlab_port, card_result);
      Serial.println(card_result);
      delay(10);
      OscWiFi.send(host, max_port, counter_address, count);
    } else {
      emergencyReady = false;
      emergencyState = true;
      DEBUG_SERIAL.print("Entered emergency ");
      OscWiFi.send(host, max_port, error_address, "Entered emergency");
      if (!globalEmergency) {  //first Arduino to reach emergency state
        globalEmergency = true;
        OscWiFi.send(host, qlab_port, start_emergency_msg);
        delay(10);
        OscWiFi.send(host, max_port, error_address, "Started Emergency");
        DEBUG_SERIAL.println("for the first time.");
        DEBUG_SERIAL.println(start_emergency_msg);
        DEBUG_SERIAL.print("Global emergency: ");
        DEBUG_SERIAL.println(globalEmergency);
      }
    }
  } else if (premissaReady == true) {
    premissaReady = false;
    DEBUG_SERIAL.println("Premissa Collapse!");
    OscWiFi.send(host, max_port, error_address, "MISSA");
    OscWiFi.send(host, qlab_port, start_premissa_msg);
  }
  return true;
}


void setup() {
  DEBUG_SERIAL.begin(115200);
#if DEBUG == true
  while (!Serial)
    ;
#elif DEBUG == false
  Serial.begin(115200);
#endif
  delay(100);
  DEBUG_SERIAL.println(F("El conspirador (o conspiradora) - INITIALIZING"));
  SPI.begin();         //initialise SPI bus
  mfrc522.PCD_Init();  //initialise MFRC522;
  for (int i = 0; i < 3; i++) {
    pinMode(ledPin[i], OUTPUT);
  }
  matrix.begin();
  delay(10);

  // WiFi stuff
  switch (id_address) {
    case 'A':
      ip = outIP[0];
      break;
    case 'B':
      ip = outIP[1];
      ¡ break;
    case 'C':
      ip = outIP[2];
      break;
  }
  Serial.print("Board: ");
  Serial.println(id_address);
  WiFi.begin(ssid, pwd);
  WiFi.config(ip, gateway, subnet);
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(500);
    static int count = 0;
    if (count++ > 5) {
      Serial.println();
      Serial.println("WiFi connection timeout, retry");
      WiFi.begin(ssid, pwd);
      count = 0;
    }
  }
  DEBUG_SERIAL.print("WiFi status: ");
  DEBUG_SERIAL.println(WiFi.status());
  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("WiFi connected, IP = ");
    Serial.println(WiFi.localIP());
    matrix.loadFrame(red);  // the matrix won't be cleared until callback message from the computer
  }

  // subscribe osc messages
  OscWiFi.subscribe(recv_port, "/arduino/msg",
                    [](const OscMessage& m) {
                      Serial.print(m.address());
                      received = m.arg<int>(0);
                      Serial.print(" received from QLab: ");
                      Serial.println(received);
                    });

  OscWiFi.subscribe(recv_port, "/arduino/globalemergency",
                    [](const OscMessage& m) {
                      Serial.print(m.address());
                      globalEmergency = m.arg<int>(0);
                      Serial.print(" has updated global emergency to: ");
                      Serial.println(globalEmergency);
                      if (globalEmergency == 1) {
                        OscWiFi.send(host, max_port, error_address, "Global emergency On");
                        emergencyReady = false;
                        emergencyState = true;
                      } else if (globalEmergency == 0) {
                        OscWiFi.send(host, max_port, error_address, "Global emergency Off");
                        emergencyState = false;
                      }
                    });

  OscWiFi.subscribe(recv_port, "/arduino/updatecounter",
                    [](const OscMessage& m) {
                      Serial.print(m.address());
                      counter = m.arg<int>(0);
                      Serial.print(" received from Max: ");
                      Serial.println(counter);
                    });

  //initialise RC522 RFID reader
  DEBUG_SERIAL.println(F("Initializing RC522 ..."));
  bool result = mfrc522.PCD_PerformSelfTest();  //perform a RC522 self-test
  if (result) {
    DEBUG_SERIAL.println("Self-test OK.");
    mfrc522.PCD_SetAntennaGain(0xFF);  //set antenna gain for RC522
    delay(50);
    DEBUG_SERIAL.println(F("RC522 online."));
  } else {
    while (!result) {
      DEBUG_SERIAL.println(F("RC522 DEFECT or UNKNOWN"));  // WARNING: BLOCKING CODE
      result = mfrc522.PCD_PerformSelfTest();              // attempt to reconnect
      OscWiFi.send(host, max_port, error_address, "RC522 DEFECT or UNKNOWN");
      delay(1000);
    }
  }
  delay(10);

  //----Read and adjust information----
  uint8_t gain = mfrc522.PCD_GetAntennaGain();  //read RC522 antenna's gain
  if (gain < 112) {                             //check if antenna gain has been properly set to maximum and else repeat the instruction
    mfrc522.PCD_SetAntennaGain(0xFF);
    DEBUG_SERIAL.print(F("Setting antenna gain: "));
    gain = mfrc522.PCD_GetAntennaGain();
    DEBUG_SERIAL.println(gain);
  }
  DEBUG_SERIAL.print(F("Antenna gain: "));
  DEBUG_SERIAL.println(gain);
  DEBUG_SERIAL.println(F("Ready."));
}

void loop() {
  currentMillis = millis();

  // first connection - status report
  if (!isConnected) {
    OscWiFi.send(host, max_port, error_address, "Ready");
    delay(10);
    OscWiFi.send(host, max_port, counter_address, counter);
    isConnected = true;
    matrix.loadFrame(clear);
  }

  // report gate state to Max
  gateReport();

  if (gateOpen) {
    if (mfrc522.PICC_IsNewCardPresent()) {  //check if a new card is present
      if (mfrc522.PICC_ReadCardSerial()) {  // select a card
        // Read data
        memset(buffer, 0, sizeof(buffer));  // Clear the buffer before each read (optional)
        size = sizeof(buffer);              // Ensure size is set correctly
        status = (MFRC522::StatusCode)mfrc522.MIFARE_Read(pageAddr, buffer, &size);
        // error report
        if (status != MFRC522::STATUS_OK) {
          DEBUG_SERIAL.print(F("MIFARE_Read() failed: "));
          DEBUG_SERIAL.println(mfrc522.GetStatusCodeName(status));
          OscWiFi.send(host, max_port, error_address, "ERROR! Reading failed");
          return;
        } else {
          OscWiFi.send(host, max_port, error_address, "OK");
        }
        readData = "";                   // Clear previous data
        for (byte i = 0; i < 16; i++) {  // Store the data read in `readData` variable
          readData += (char)buffer[i];
        }

        if (readData != currentData) {
          Serial.print(F("Readed data: "));
          Serial.print(readData);
          if (counter < numCards) {
            cardArray[counter] = readData.toInt();  // store card number in the corresponding array position
          }
          counter++;  // increment counter
          Serial.print("  Counter: ");
          Serial.println(counter);

          sendOSC(readData.toInt(), counter);

          currentData = readData;
          mfrc522.PICC_HaltA();  //stop the reading
          delay(300);
          mfrc522.PCD_Init();
        }
      }
    }
  }

  // incoming messages from QLab or Max
  switch (received) {
    case matrixOn:
      matrix.loadFrame(red);
      received = idle;
      break;
    case matrixOff:
      matrix.loadFrame(clear);
      received = idle;
      break;
    case randomOn:
      ledRandomOn = true;
      received = idle;
      break;
    case randomOff:
      ledRandomOn = false;
      received = idle;
      ledBlackout();
      break;
    case ledEmergencia:
      m = 0;
      ledEmergenciaOn = true;
      received = idle;
      break;
    case emergenciaOff:
      m = 255;
      ledEmergenciaOn = false;
      ledEmergenciaOff = true;
      emergencyState = false;
      received = idle;
      break;
    case counterReset:
      Serial.println("Reset received!");
      received = idle;
      counter = idle;
      //peaksOn = false;
      readData = "";
      OscWiFi.send(host, max_port, counter_address, counter);
      delay(5);
      OscWiFi.send(host, max_port, error_address, "Reset received!");
      for (int i = 0; i < numCards; i++) {
        cardArray[i] = 0;
      }
      emergencyReady = false;
      premissaReady = false;
      emergencyState = false;
      ledRandomOn = false;
      ledFinalOn = false;
      currentData = "";
      ledBlackout();
      break;
    case emergency:
      emergencyReady = true;
      OscWiFi.send(host, max_port, error_address, "Emergency ready");
      received = idle;
      break;
    case premissa:
      premissaReady = true;
      OscWiFi.send(host, max_port, error_address, "Premissa ready");
      received = idle;
      break;
    case openGate:
      if (isConnected) {
        gateOpen = true;
        received = idle;
      }
      break;
    case closeGate:
      gateOpen = false;
      received = idle;
      break;
    case endGame:
      received = idle;
      OscWiFi.send(host, max_port, error_address, "END");
      // send numbers to max, which will transform them into a list and send them to QLab
      for (int i = 0; i < counter; i++) {
        OscWiFi.send(host, max_port, text_address, cardArray[i]);
        DEBUG_SERIAL.print(cardArray[i]);
        DEBUG_SERIAL.print(" ");
      }
      DEBUG_SERIAL.println();
      break;
    case ledFinal:
      received = idle;
      m = 0;
      ledFinalOn = true;
      break;
  }

  // LEDS
  if (ledRandomOn) {
    if (received != randomOff) {
      uint8_t randVal = random(0, 200);
      for (int i = 0; i < 3; i++) {
        analogWrite(ledPin[i], randVal);
      }
    }
  }
  if (ledEmergenciaOn) {
    if (currentMillis - previousMillis > 40) {
      previousMillis = currentMillis;
      if (m < 255) {
        m++;
        int rValue = map(m, 0, 255, 0, 20);
        int gValue = map(m, 0, 255, 0, 200);
        int bValue = map(m, 0, 255, 0, 30);
        analogWrite(rPin, rValue);
        analogWrite(gPin, gValue);
        analogWrite(bPin, bValue);
        DEBUG_SERIAL.print(rValue);
        DEBUG_SERIAL.print(" ");
        DEBUG_SERIAL.print(gValue);
        DEBUG_SERIAL.print(" ");
        DEBUG_SERIAL.println(bValue);
      } else {
        m = 255;
        ledEmergenciaOn = false;
        DEBUG_SERIAL.println("Led Emergència at max value");
      }
    }
  }
  if (ledEmergenciaOff) {
    if (currentMillis - previousMillis > 40) {
      previousMillis = currentMillis;
      if (m >= 255) {
        m--;
        int rValue = map(m, 0, 255, 0, 20);
        int gValue = map(m, 0, 255, 0, 200);
        int bValue = map(m, 0, 255, 0, 30);
        analogWrite(rPin, rValue);
        analogWrite(gPin, gValue);
        analogWrite(bPin, bValue);
        DEBUG_SERIAL.print(rValue);
        DEBUG_SERIAL.print(" ");
        DEBUG_SERIAL.print(gValue);
        DEBUG_SERIAL.print(" ");
        DEBUG_SERIAL.println(bValue);
      } else {
        m = 0;
        ledEmergenciaOff = false;
        DEBUG_SERIAL.println("Led Emergència off");
      }
    }
  }
  if (ledFinalOn) {
    if (currentMillis - previousMillis > 200) {
      previousMillis = currentMillis;
      if (m < 255) {
        m++;
        int rValue = m;
        int gValue = map(m, 0, 255, 0, 211);
        int bValue = map(m, 0, 255, 0, 91);
        analogWrite(rPin, rValue);
        analogWrite(gPin, gValue);
        analogWrite(bPin, bValue);
        DEBUG_SERIAL.print(rValue);
        DEBUG_SERIAL.print(" ");
        DEBUG_SERIAL.print(gValue);
        DEBUG_SERIAL.print(" ");
        DEBUG_SERIAL.println(bValue);
      } else {
        m = 255;
        ledFinalOn = false;
      }
    }
  }
  OscWiFi.update();  // should be called to receive + send osc
}

void gateReport() {
  if (gateOpen != lastGateState) {
    OscWiFi.send(host, max_port, gate_address, gateOpen);
    DEBUG_SERIAL.print("Gate has changed to ");
    DEBUG_SERIAL.println(gateOpen);
  }
  lastGateState = gateOpen;
}

void ledBlackout() {
  for (int i = 0; i < 3; i++) {
    analogWrite(ledPin[i], 0);
  }
  ledRandomOn = false;
  if (emergencyState) {
    m = 0;
    ledEmergenciaOn = true;  // EXPERIMENTAL !!! How to come back to this led state?
  } else {
    ledEmergenciaOn = false;
  }
  ledFinalOn = false;
  DEBUG_SERIAL.println("Led blackout");
}
