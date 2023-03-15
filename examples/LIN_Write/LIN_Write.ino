#include <linduino.hpp>

// LIN interface
#define LIN_TX 1          // TX Pin
#define LIN_RX 0          // RX Pin
#define LIN_CS 2          // CS pin, active high
#define LIN_BAUD 1000     // Baudrate, LIN can be 1-20 Kbaud

LIN lin(Serial, LIN_RX, LIN_CS, LIN_TX);

const byte ledPin = 13;

void setup() {
  pinMode(ledPin, OUTPUT);

  // You cannot share a UART port, so Serial is not available unless you use a Mega and set LIN to Serial1, Serial2 or Serial3.
  // Serial.begin(9600);
  lin.begin(LIN_BAUD, LIN_HOST);
}

bool ledState = LOW;

void loop() {
  // You are supposed to send LIN frames in a scheduled manner, but you don't *have* to.
  ledState = !ledState;

  uint8_t arr[8];
  arr[0] = ledState;

  lin.writeHeader(0x01);
  lin.writeData(arr, 1);

  digitalWrite(ledPin, ledState);

  delay(500);
}