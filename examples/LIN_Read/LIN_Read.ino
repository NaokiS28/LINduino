#include "linduino.hpp"

// LIN interface
#define LIN_TX 1          // TX Pin
#define LIN_RX 0          // RX Pin
#define LIN_CS 2          // CS pin, active high
#define LIN_BAUD 1000     // Baudrate, LIN can be 1-20 Kbaud

LIN lin(Serial, LIN_RX, LIN_CS, LIN_TX);

const byte ledPin = LED_BUILTIN;

void setup() {
  pinMode(ledPin, OUTPUT);

  // You cannot share a UART port, so Serial is not available unless you use a Mega and set LIN to Serial1, Serial2 or Serial3.
  // Serial.begin(9600);
  lin.begin(LIN_BAUD, LIN_NODE);
}

bool ledState = LOW;

void loop() {
    // Check for LIN frames
  if (lin.available()) {
    int id = lin.readID();
    if (id > 0) {
        //Serial.print("LIN ID: 0x");
        //Serial.println(temp, HEX);

        switch(id){
            // Add your ID (0-63 or 0x00-0x3F) subscriptions here
            case 1:
                byte arr[8];
                if (lin.waitForData(1)) {
                    int8_t inBytes = lin.readData(arr, 1);
                    if(inBytes >= 1){
                        digitalWrite(ledPin, arr[0]);    
                    }
                }
                break;

            default:
                // If you do not respond to the ID received here, ignore it.
                break;
        }
    } else {
        //Serial.print("LIN Header Error: ");
        //Serial.println(id);
    }
  }
}