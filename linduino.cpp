#ifndef LIN_DUINO_CPP
#define LIN_DUINO_CPP

#include "linduino.hpp"

LIN::LIN (HardwareSerial &_s, uint8_t _rx, uint8_t _cs) {
  // Device node
  _uart = &_s;
  cs = _cs;
  rx = _rx;
  tx = 255;
}

LIN::LIN (HardwareSerial &_s, uint8_t _rx, uint8_t _cs, uint8_t _tx) {
  // Host Node
  _uart = &_s;
  cs = _cs;
  rx = _rx;
  tx = _tx;
}

void LIN::begin(uint16_t _b, bool host) {
  //_uart->begin(_baud); // We are technically supposed to support auto-baud, but Arduinos.
  pinMode(cs, OUTPUT);
  digitalWrite(cs, HIGH);
  pinMode(rx, INPUT_PULLUP);
  if (tx != 255) pinMode(tx, OUTPUT);

  // Set up the buffers
  memset(frameBuffer.data, 0, sizeof(frameBuffer.data));
  frameBuffer.head = 0;
  frameBuffer.tail = 0;
  frameBuffer.length = 0;

  memset(syncBuffer.data, 0, sizeof(syncBuffer.data));
  syncBuffer.head = 0;
  syncBuffer.tail = 0;
  syncBuffer.length = 0;

  // Start UART
  _baud = _b;
  _baudBit = ((uint32_t)1000000 / (uint32_t)_baud);
  _uart->begin(_baud);
  linState = lin_READY;

  isHostDevice = host;

  reset();

  Serial.println("LIN BEGIN");
  if (isHostDevice) Serial.println("LIN HOST");
  else Serial.println("LIN NODE");
}

void LIN::sleep(bool forceSleep) {
  // Call this when going to sleep
  //_uart->end(); // End the comms?
  if (forceSleep && isHostDevice) {
    writeHeader(0x60);
    int i = 8;
    byte d[i];
    d[0] = 0x00;
    writeData(d, i);
  }
  digitalWrite(cs, LOW);
  linState = lin_SLEEP;
  Serial.println("LIN SLEEP");
}

void LIN::wake() {
  // Wake the transceiver from sleep
  digitalWrite(cs, HIGH);
  linState = lin_READY;
  if (!isHostDevice && tx != 255) {
    _uart->write(0x80);
    _uart->flush();
  }
}

int LIN::available() {
  // Can be used similarly to Serial.available() only this will only report if a LIN frame is ready or not

  // Host
  if (isHostDevice) {
    return _uart->available();
  }

  // Node
  bool _rx = digitalRead(rx);

  if (_rx != lastRX) {
    // If the RX line changed
    lastRX = _rx;
    lastBusActivity = micros();     // Reset activity timer

    // Check for break
    //if (!gotBreakPulse) {
      if (!_rx) {
        breakStart = micros();        // Capture the time the line went low.
      } else {
        uint32_t breakTime = (micros() - breakStart);
        if (breakTime > (_baudBit * 11)) {
          gotBreakPulse = true;
          //Serial.println("LIN BREAK");
        }
      }
    //}
  }

  // It takes a few µs to get serial bytes through, so wait for it.
  while (_uart->available()) {
    uint8_t c = _uart->read();

    // Read bytes to ring buffer to store the frames as they come in.
    frameBufferWrite(c);
  }

  // If we have a flag for a break pulse and the byte is 0, assume its the start of a new frame

  // AVR boards seem to read the break byte altough they shouldnt?
  if (gotBreakPulse) {
    byte b = frameBufferRead();
    if (b == 0x00) {
      syncBufferWrite(frameBuffer.tail);
      gotBreakPulse = false;
      breakCount++;
    }
  }

  return breakCount;
}

int LIN::nextHeader() {
  if (syncBuffer.length == 0) {
    return -1;  // No more headers to process
  }

  uint8_t addr = syncBufferRead();
  //Serial.print("Next header: "); Serial.println(addr);
  frameBuffer.head = addr;

  return 0;
}

int LIN::readID() {
  /*  Read the ID of the LIN frame
      -1: Not enough bytes in buffer
      -2: Break check failed
      -3: Sync check failed, wrong baud?
      -4: ID Parity check failed
  */
  if (frameBuffer.length >= 3) {
    breakCount--;
    //Serial.println("LIN READID");
    uint8_t temp = frameBufferRead();

    if (temp == 0x55) {
      // Sync good, get header byte
      syncBufferRead(); // Clear this entry current entry.
      uint8_t headerByte = frameBufferRead();

      // Check heady parity
      temp = calcIDParity(headerByte & 0x3F);
      if (temp != (headerByte & 0xC0)) {
        // Parity did not match, can't verify header
        return -4;
      } else {
        // Parity good
        return (headerByte & 0x3F); // Returns ID 0-63
      }
    } else {
      // Expected sync, didn't get it.
      return -3;
    }
  } else return -1; // Not ready
}

bool LIN::waitForData(uint8_t size) {
  /*
     Stay in loop until enough data bytes are received.
  */
  uint32_t startTime = millis();

  // Time out after max frame has elapsed.
  while (frameBuffer.length < (size + 1)) {
    // Update LIN
    available();
    // Timeout check
    if ((millis() - startTime) >= ((_baudBit * LIN_MAX_FRAME_SIZE) / 1000)) {
      //Serial.println(frameBuffer.length);
      //Serial.println("TIMEOUT");
      return 0;
    }
  }
  return 1;
}

int LIN::readData(uint8_t * arr, uint8_t len) {
  /*  Read the data bytes of the frame received
      -1: Not enough bytes in buffer (min = 2)
      -2: Checksum of bytes failed (check your LIN version and checksum type)
  */
  // Call this when node is subscribed to ID.

  if (arr == NULL) {
    return -1;
  }

  if (frameBuffer.length > len) {

    //Serial.println("Data start");
    uint8_t byteCount = 0;
    uint8_t tempArr[9];

    //Serial.println(frameBuffer.length);

    // Read in data bytes + CRC sum byte
    while (byteCount < (len + 1)) {
      tempArr[byteCount++] = frameBufferRead();
      /*Serial.print("Byte count: ");
        Serial.print(byteCount);
        Serial.print(" D: 0x");
        Serial.println(tempArr[byteCount - 1], HEX);*/
    }

    byte temp = calcChecksum(tempArr, len);
    /*
      Serial.print("Checksum: 0x");
      Serial.println(temp, HEX);
      Serial.print("Byte count: ");
      Serial.println(byteCount);
    */
    if (temp == tempArr[byteCount - 1]) {
      memcpy(arr, tempArr, len);
      return (byteCount - 1);
    } else {
      return -3;
    }
  } else {
    return -2;  // Not enough bytes
  }
}

int LIN::writeHeader(uint8_t id) {
  /*  HOST CONTROLLER ONLY
       -1: Tried to write when not host node
  */

  if (!isHostDevice) return -1;

  _uart->end();
  pinMode(tx, OUTPUT);
  digitalWrite(tx, LOW);
  delayMicroseconds((_baudBit * 13));
  digitalWrite(tx, HIGH);
  _uart->begin(_baud);
  delayMicroseconds((_baudBit * 2));
  _uart->write(0x55);
  _uart->write((id | calcIDParity(id)));
  _uart->flush();
  return 0;
}

int LIN::writeData(uint8_t *arr, uint8_t len) {
  /*
       -1: array is NULL
  */
  if (arr == NULL) return -1;

  uint8_t sum = calcChecksum(arr, len);
  uint8_t bytesWrote = 0;

  _uart->begin(_baud);
  for (int i = 0; i < len; i++) {
    _uart->write(arr[i]);
    delayMicroseconds((_baudBit * 2));  // Delete when I know better
    bytesWrote++;
  }
  _uart->write(sum);
  _uart->flush();

  return bytesWrote;
}

uint8_t LIN::calcChecksum(uint8_t *arr, uint8_t len, uint8_t id) {
  // V1 checksum. Sum up all data bytes into 8-bit int, subtract sum from 255
  if (arr == NULL) return -1;

  uint8_t sum = 0;
  if (len > 8) len = 8;

  if (id != 255) {
    // LIN 2.0+ Enhanced
    sum += id;
  }
  // LIN 1.3
  for (int i = 0; i < len; i++) {
    if (sum + arr[i] > 0xff) sum++;
    sum += arr[i];
  }
  sum = ~sum;
  return sum;
}

void LIN::reset() {
  gotBreakPulse = false;
  breakCount = 0;
  lastRX = HIGH;
}

byte LIN::dataLengthCode(byte head) {
  byte temp = ((head & 0x30) >> 4);
  switch (temp) {
    case 0: case 1:
      return 2;
      break;
    case 2:
      return 4;
      break;
  case 3: default:
      return 8;
      break;
  }
}

byte LIN::calcIDParity(byte ident) {
  byte p0 = bitRead(ident, 0) ^ bitRead(ident, 1) ^ bitRead(ident, 2) ^ bitRead(ident, 4);
  byte p1 = ~(bitRead(ident, 1) ^ bitRead(ident, 3) ^ bitRead(ident, 4) ^ bitRead(ident, 5));
  return (p0 | (p1 << 1)) << 6;
}

// Frame buffers
int LIN::frameBufferRead() {
  if (frameBuffer.length == 0) {
    return -1;
  }
  int head        = frameBuffer.head;
  frameBuffer.head = (head + 1) % SERIAL_RX_BUFFER_SIZE;
  frameBuffer.length--;

  return frameBuffer.data[head];
}

int LIN::frameBufferWrite(uint8_t c) {
  if (frameBuffer.length >= SERIAL_RX_BUFFER_SIZE) {
    // over flow
    return -1;
  }
  frameBuffer.data[frameBuffer.tail] = c;
  frameBuffer.tail   = (frameBuffer.tail + 1) % SERIAL_RX_BUFFER_SIZE;
  frameBuffer.length++;

  return 1;
}

int LIN::syncBufferRead() {
  if (syncBuffer.length == 0) {
    return -1;
  }
  int head        = syncBuffer.head;
  syncBuffer.head = (head + 1) % 8;
  syncBuffer.length--;

  return syncBuffer.data[head];
}

int LIN::syncBufferWrite(uint8_t s) {
  if (syncBuffer.length >= 8) {
    // over flow
    return -1;
  }
  syncBuffer.data[syncBuffer.tail] = s;
  syncBuffer.tail   = (syncBuffer.tail + 1) % 8;
  syncBuffer.length++;

  return 1;
}

#endif