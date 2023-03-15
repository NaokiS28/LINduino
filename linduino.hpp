#ifndef LIN_DUINO_HPP
#define LIN_DUINO_HPP

/*
   LINduino - (C) 2023 NaokiS
   ==========================
   LIN bus driver for Arduino supporting host and node modes, break detection and error checking

    LIN bus supports up to 16 nodes officially, citing physical wire limits.

    This library is intended for getting started with LIN bus on arduino platforms,
    it does not support extended frames natively.

    LIN node devices can ignore a message, subscribe to a message or publish a response. 
    In simpler non-LIN terms, this means it either is commanded by a message or replies with data.

    The "message" is the ID number. If the ID is one that the node subscribes to,
    then it will listen to the following data bits. If it is one that is publishes to,
    then it will reply with up to 5 data bytes + a CRC sum byte.

    The format for data byte field length is... confusing. So many people say different things.

    The LIN 1.3 specification uses a data length code... *sometimes*.
      "If required (e.g. compatibility to LIN Specification 1.1) the IDENTIFIER bits ID4 and ID5
      may define the number of data fields NDATA in a message. This divides the set of
      64 identifiers in four subsets of sixteen identifiers, with 2, 4, and 8 data fields, respectively."

    This means LIN 1.3, if using DLC, would have up to 64 message types.
      60 "Signal carrying" frames:
      0-31: 2 bytes
      32-47: 4 bytes
      48-60: 8 bytes
      2 for diagnostics over LIN
      1 for user defined extentions
      and 1 that is reserved.

    But LIN 2.0 does not specify any DLC encoding and it just says that the data field may be 0 - 8 data bytes.

    My advice... Just plan how many data bytes each ID needs.

    LIN 1.3: The identifiers 0x3C, 0x3D, 0x3E, and 0x3F with their respective IDENTIFIER FIELDS 0x3C, 0x7D, 0xFE,
      and 0xBF (all 8-byte messages) are reserved for command frames (e.g. sleep mode) and extended frames.
      ... or otherwise known as IDs 60-64.
*/

#include <Arduino.h>

#define T_TIMEOUT 25000   // Bits at given baudrate

#define LIN_NODE 0
#define LIN_HOST 1

#define LIN_MAX_FRAME_SIZE (13 + 2 + 20 + 5 + 90)   // Break + pause, Header + pause, data bytes + checksum

#if defined(__AVR_ATmega1280__) || defined(__AVR_ATmega2560__) || defined(__AVR_ATmega328__) || defined(__AVR_MICRO__) || defined(__AVR_MINI__)
#define __AVR_BOARD__
#endif

struct LIN_Frame {
  uint8_t id;
  uint8_t length;   // Not offical
  uint8_t data[8];
  uint8_t checksum;
};


class LIN {
  public:
    /*
      Constructor:
        @param &_s The HardwareSerial port to use for this library
        @param _rx The serial port RX pin
        @param _cs The transciever CS pin
        @return LIN object
    */
    LIN (HardwareSerial &_s, uint8_t _rx, uint8_t _cs);

    /*
      Constructor:
        @param &_s The HardwareSerial port to use for this library
        @param _rx The serial port RX pin
        @param _cs The transciever CS pin
        @param _tx The serial port TX pin
        @return LIN object
    */
    LIN (HardwareSerial &_s, uint8_t _rx, uint8_t _cs, uint8_t _tx);

    /*
      begin():
        Run setup functions for LIN instance
        @param _b The baudrate to use (1-20Kbps). Typical speeds might be 2.4 kpbs, 9.6 kbps and 19.2 kbps
        @param _rx Specficy the device type, LIN_HOST or LIN_NODE (optional, assumes LIN_HOST)
        @return void
    */
    void begin(uint16_t _b, bool host = true);   // Setup LIN instance

    /*
      sleep():
        Put the LIN transceiver into sleep mode
        @param forceSleep Host only: Send diagnostic frame to make nodes sleep
        @return void
    */
    void sleep(bool forceSleep = false);   // Sleep LIN bus. MCU stays awake

    /*
      wake():
        Wake up the LIN transceiver. When LIN_NODE, send out wake up pulse.
        This is not needed on LIN_HOST.
        @return void
    */
    void wake();

    /*
      available():
        LIN_NODE: Get number of frames (LIN headers) stored in buffer.
        LIN_HOST: Get number of data bytes received from node.
        @return Number of LIN headers or data bytes received
    */
    int available();

    /*
      waitForData():
        Wait until either required data has arrived or time-out occurs
        @param size Number of bytes to wait until received
        @return Whether data has arrived (1) or timeout (0) occured
    */
    bool waitForData(uint8_t size);

    /*
      dataAvailable():
        LIN_NODE: How many data bytes have been received. *This can include other header and data bytes too*
        @return Amount of bytes in buffer
    */
    int dataAvailable() {
      return frameBuffer.length;
    }

    /*
      readID():
        Reads the ID of the next frame in the buffer.
        @return ID of frame,
          -1: Not enough bytes in buffer
          -2: Break check failed
          -3: Sync check failed, wrong baud?
          -4: ID Parity check failed
    */
    int readID();

    /*
      readData():
        Reads data bytes. Must be called after readID(). This only runs a checksum sanity check, nothing else. It only reads as much data as passed array can hold.
        @param arr A pointed to a char/uint8_t array.
        @param arrSize Maximum size of the array passed. Limited to 8 bytes.
        @return Amount of bytes read in.
          -1: Array passed is NULL
          -2: Not enough bytes in buffer (min = 2 for D0 and checksum)
          -3: Checksum of bytes failed (check your LIN version and checksum type)
    */
    int readData(uint8_t * arr, uint8_t arrSize);

    /*
      writeHeader():
        LIN_HOST ONLY: Write a new header frame to the bus to start transer.
        @param id ID to send from 0-64 (0x00-0x3F). Data Length Code needs to be encoded first.
        @return Status:
          0: Sucess
          -1: Tried to write when not host node
    */
    int writeHeader(uint8_t id);

    /*
      writeData():
        Writes data bytes. Must be called after writeHeader(). It only writes as much data as passed array can hold.
        @param arr A pointed to a char/uint8_t array.
        @param arrSize Maximum size of the array passed. Limited to 8 bytes.
        @return Amount of bytes written out.
          -1: Array passed is null.
    */
    int writeData(uint8_t *arr, uint8_t len);

    /*
      calcChecksum():
        Calculates a checksum for the given data array. You must specify an id (with parity) to send as LIN 2.0+ enhanced checksum.
        @param arr A pointed to a char/uint8_t array.
        @param len Size of the array. Limited to first 8 bytes.
        @return Checksum as 8-bit int.
          -1: Array passed is null.
    */
    uint8_t calcChecksum(uint8_t *arr, byte len, uint8_t id = 255);

    /*
      dataLengthCode():
        LIN_NODE: Returns DLC encoded length of data bytes. Only used on LIN 1.3 and earlier, not required.
        @param head Protected ID byte, parity not required.
        @return Length of data byte field.
    */
    uint8_t dataLengthCode(byte head);

    /*
      linVersion:
        Sets the LIN bus version to use. *Only 10, 11, 12, 13, 20, 21 or 22 are valid!*
    */
    uint8_t linVersion = 13;

  private:
    bool gotBreakPulse = false;
    bool isHostDevice = false;

    uint8_t breakCount = 0;

    void reset();     // Not sure if this is needed any more

    uint8_t calcIDParity(byte ident);

    int nextHeader();

    enum linStates {
      lin_POR,    // 0: Power On Reset. LIN receiver has MCU powered down (in theory)
      lin_READY,    // 1: Ready mode. MCU powered on, RX on, TX off (We go into this as TX is normally high at idle)
      lin_TOFF,   // 2: TX Off, not transmitting data (This mode is not used here)
      lin_OP,     // 3: Operation mode, send and receive enabled
      lin_SLEEP     // 4: Sleep mode, LIN receiver will turn off MCU
    } linState;

    // UART Port
    bool lastRX = HIGH;

    HardwareSerial * _uart;  // Pointer to the Serial class. Is a pointer as we dont want to copy the class, but use it directly.
    uint16_t _baud;
    uint16_t _baudBit;      // Time in µs that one LIN bit takes
    uint8_t cs, rx, tx;

    uint32_t lastBusActivity = 0;   // Maximum time is about 4 seconds before nodes should sleep.
    uint32_t breakStart = 0;

    // Framebuffers

    struct {
      uint8_t data[SERIAL_RX_BUFFER_SIZE];  // Set by HardwareSerial.h
      uint8_t head, tail, length;
    } frameBuffer;

    int frameBufferRead();
    int frameBufferWrite(uint8_t c);

    struct {
      uint8_t data[8];  // This holds the sync break byte position within frameBuffer for 8 messages to know where the frame starts.
      uint8_t head, tail, length;
    } syncBuffer;

    int syncBufferRead();
    int syncBufferWrite(uint8_t s);

};

#endif