# scalextricArcSniffer
A radio packet sniffer for Scalextric Arc Controllers. This captures the radio packets exchanged between the Scalextric ARC base unit and the controllers.

I developed this using Segger Embedded Studio with the Nordic V12.2.0 SDK.

# Prerequisits

A [BBC Microbit](http://microbit.org/) or similar Nordic nrf51822 based board
[Segger J-Link firmware](https://www.segger.com/bbc-micro-bit.html) for the Microbit
[Segger Embedded Studio](https://www.segger.com/embedded-studio.html)
[Nordic SDK V12.2.0 SDK](https://developer.nordicsemi.com/nRF5_SDK/nRF5_SDK_v12.x.x/)

# Setup

The Microbit should be flashed with the J-Link firmware above.
The Nordic SDK should downloaded and extracted alongside the checkout of this code.
The Embedded Studio project should be opened from 'microbit/blank/arm5_no_packs'
It should build and run without any problems.

# Configuration

There are a number of defines which affect the behaviour of the application:

LOG_LANE -- This is the number of the lane that you wish to monitor

LOG_UNKNOWN -- Set to 1 to capture the packets sent by the base on channel 79.

LOG_PAIRING -- Set to 1 to capture the packets sent by the base on channel 81.

# Output

```
Elapsed Time,CRC Status,Frequency Bin,Pkt[0],Pkt[1],Pkt[2],Pkt[3],Pkt[4],Pkt[5]
940,1,5,4,3,10,81,0,81
35,1,5,4,3,10,81,0,81
59,1,41,4,1,10,1,0,81
58,1,79,4,6,10,0,0,0
152,1,5,4,1,10,81,0,81
35,1,5,4,1,10,81,0,81
59,1,41,4,5,10,1,0,81
58,1,79,4,2,10,0,0,0
152,1,5,4,5,10,81,0,81
35,1,5,4,5,10,81,0,81
60,1,41,4,1,10,1,0,81
59,1,79,4,6,10,0,0,0
```

