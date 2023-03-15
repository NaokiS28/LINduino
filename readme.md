# LINduino
A library for Arduinos to read from and write to a LIN bus using the MCP2003 transceiver IC or equivilant. This library supports both LIN 1.3 and LIN 2.0+, using classic or enhanced checksumming. This library is intended for getting started with LIN bus on Arduino platforms. It does not support extended frames natively, however these are software definable.

#### LIN Bus brief
LIN bus supports up to 16 nodes officially, citing physical wire limits.

LIN node devices can ignore a message, subscribe to a message or publish a response. 
In simpler non-LIN terms, this means it either is commanded by a message or replies with data.

The "message" is the ID number. If the ID is one that the node subscribes to, then it will listen to the following data bits. If it is one that is publishes to, then it will reply with up to 8 data bytes + a checksum sum byte.

LIN supports 64 message types:
* 60 "Signal carrying" frames from ID 0 to ID 59.
* 2 for diagnostics over LIN (60 and 61).
* 1 for user defined extentions (62).
* 1 that is reserved (63).

#### Data byte length

The LIN 1.3 specification uses a data length code... *sometimes*.
```
"If required (e.g. compatibility to LIN Specification 1.1) the IDENTIFIER bits ID4 and ID5
may define the number of data fields NDATA in a message. This divides the set of
64 identifiers in four subsets of sixteen identifiers, with 2, 4, and 8 data fields, respectively."
```
This means a LIN 1.3 ID , if using DLC, would have use the following data byte lengths:
* 0-31: 2 bytes
* 32-47: 4 bytes
* 48-60: 8 bytes

LIN 2.0 does not specify any DLC encoding and it just says that the data field may be 0 - 8 data bytes. This means you will need to know how many bytes to expect for each ID header.


#### This library is still being worked on and there are a couple of known issues:
* Reading might fail sometimes. Probably related to the ring buffer being used.
* The library won't stop you using faster or slower baud, but you will break it if you go too fast.
* Break pulses are ready a bit wacky atm. The AVR UART will read in `0x00` even when it's not supposed to (due to missing stop bits). This means that whilst it's easy to verify a break pulse, it also uses a quirk with the AVRs that might not be present in other MCUs. Future versions will just omit using this quirk.
* Its all kinds of jank(tm). LIN bus is not a sane protocol IMO.
* Sleep support is there, but not well made.
* All official documentation uses the terms Master and Slave for devices. I replace these with Host device and Node device. It's not a protest, nor a grievance, it's just preference on my part.

#### Some things to note when using the LIN bus:
* Data rate is between 1-20Kbps. Bear in mind that the atmega328 and probably atmega2560 have sucky baudrate generators and this might cause tolerance issues.
* There is only a data length if you want it - You can use Data Length Codes (DLCs) to encode the length of the data but it is not always used and completely absent in LIN 2.0. The LIN specifications state "amount of data bytes for an ID will be agreed upon by both the host and node devices." IE you need to program both devices to use them.
* There is no start of frame or escape characters, period, except the break pulse. Unlike JVS, this makes doing sanity checking very annoying and hard IMO. Make sure you know what ID was last sent and that you always use a fixed data length for that ID. About the best feature is that it has a checksum for the data portion.
* As data might not be immediately present to read when publishing a Protected ID, **you need to wait for it**. The library includes `waitForData(numberOfBytes);` for this reason, it will either give the go ahead to read on return or will timeout, altough this blocks until either data is returned or the timeout occurs (only 4ms). If you do not want the loop to block, use `available();` instead as on a HOST_DEVICE instance, it will return how many bytes were returned from the node. Neither do ***any sanity checking whatsoever***... I mean they can't really.

## Wiring
This library requires a MCP2003 or compatible transceiver. You can connect two devices together using TX and RX if you want for testing, but in a automotive environment you **must** have a transceiver.

Use the following schematic to wire up your transceiver to the Arduino.
TODO: add schematic
* TX = Arduino TX
* RX = Arduino RX
* CS = Arduino GPIO pin, typically pin 3.


## Usage
To use this library you'll need to assign a hardware UART port (Serial) to the library. On the Uno you only have one hardware UART called `Serial`. On the Mega you have 4 UARTs `Serial, Serial1, Serial2, Serial3`. If you need to use serial debugging when using this library, you need to use a Mega or a Leonardo like Arduino that has multiple ports.

You can create a LIN instance with the following constructor:
```c++
LIN lin(Serial, UART_RX, LIN_CS, UART_TX);
```
Note that `UART_TX` is not needed if you are making a node device, it's only used to make the break pulse.

Then you can start the instance using:
```c++
lin.begin(LIN_BAUD, LIN_HOST);
```
Baud rate can be a baud between 1000 and 20000 bits per seconds (1-20 Kbps).
Library modes are LIN_HOST for sending out headers and LIN_NODE for other devices such as sensors, lights etc. If you do not specify a mode, LIN_HOST will be assumed.

If you are making a host controller, you can send LIN headers out using `writeHeader(HEADER_ID);`. If you also need to send out data bytes, this must be done after the header and by using `writeData(ARRAY, SIZE);`, where ARRAY is a byte array with your data and SIZE is the number of bytes to send not including the checksum (this is calculated for you).

A typical host device example might have a function for each ID:
```c++
void writeLightData(){
    // Write data to device.
    byte array[6];
    array[0] = 7;
    array[1] = 15;
    array[2] = 31;
    array[3] = 63;
    array[4] = 127;
    array[5] = 255;

    lin.writeHeader(0x10);
    lin.writeData(array, 6);
}

int readButtonData(){
    // Read in two bytes of button data, return as single int.
    byte array[2];

    lin.writeHeader(0x05);
    if(lin.waitForData(2) == 0) {
        // Timed out
        return -1;
    } else {
        lin.readData(array, 2);
        int buttons = ((array[0] << 8) | array[1]);
        return buttons;
    }
}
```

If you are making a node device, you can check for any headers by using the following:
```c++
if(lin.available()){
    int id = lin.readID();
    if (id >= 0) {
        // If ID is a negative number, a fault occured.
        switch(id){
            // Add your ID (0-63 or 0x00-0x3F) subscriptions here
            case 1: 
                // Do things when ID 1 is received.
                // Send bytes with writeData(ARRAY, SIZE);
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
```

Examples of writing to the LIN bus as a HOST and reading as a NODE can be found in the examples folder.

## Full list of public functions:
```c++
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
```