# Oligo Grace Pendant Custom Firmware (model G42-931)


### Table of contents

[1. Motivation](#motivation)  
[2. Hacking the Oligo Grace Pendant](#hacking-the-oligo-grace-pendant)  
[3. Building and Uploading the Custom Firmware](#building-and-uploading-the-custom-firmware)  
[4. Power Consumption](#power-consumption)  
[5. Contributing](#contributing)  
[6. Licenses and Credits](#licenses-and-credits)


## Motivation

This project started as a consequence of poor product service. After only 3 years of use the brightness settings memory of my Oligo Grace pendant duo stopped working causing the 2 pendants to have different random brightness at power on.

Contacted with a detailed description of the problem Oligo support suggested resetting the device as described in the owners manual. But this kind of reset only applies to a model variant I did not have. After reporting this to Oligo support I was asked to contact my dealer to return the pendant duo for reprogramming. So I asked my dealer for an offer and got the info that cost for repair will be between 20 and 30 EUR excluding transport, if only reprogramming is required, and will take more than 6 weeks including shipping. I asked for a fixed service price including transport but got no answer.

Having a price span between 50 EUR and, worst case, the price of a new pendant duo is something probably no one will be happy with, and waiting around 6 weeks for the repair to be completed may not feasible without a temporary replacement. For classic ceiling lights mounting a replacement light takes only a few minutes but the pendant duo uses a special ceiling case that hides the transformer and unmounting and remounting this case takes quite a while.


## Hacking the Oligo Grace Pendant

At this point I decided to have a closer look at the inside of pendants - maybe they can be fixed without help from the manufacturer. After only a few minutes looking at the PCB I was pretty sure that self-service is possible, because on the PCB was a combination of the reference applications of several popular chips: MPM3620 5V step down converter, ATmega328P MCU and BCR421U LED driver:

![Schematic](assets/Schematic.png "schematic of Oligo Grace Pendant (model G42-931")

The schematic was obtained by taking a resistance meter with 2 needles for contacting the soldered component pins on the PCB. Knowing what to look for helped a lot but it still took around a day to get most of the details. Writing the prototype of a custom firmware with almost identical functionality took another day. Considering the cost of the product design, schematic design, software development, hardware components and assembly I asked myself how much profit manufacturer and dealer get out of each pendant sold.

Technical notes:

- Oligo uses a PWM frequency of approximately 31 kHz. If you use lower frequencies
  (e.g. around 5 kHz) you will probably be able to "hear" your LEDs.
- The fixed brightness steps are 3.5 %, 16 %, 32 % and 100 %.
- The brightness settings memory failure was probably caused by writing to often to
  the same EEPROM address. The custom firmware uses a delayed backup strategy to 
  avoid writing every user setting change to EEPROM. Manually changing the EEPROM
  address is also possible.
- When turning power off and on very quickly within a second the Oligo firmware shows 
  a tendency to hang (random brightness, no reaction to gestures, power cycle required
  to reset).
- The custom firmware adds a gesture to lock the proximity sensor by a proximity 
  of more than 15 seconds during power up.
- The Oligo firmware is read protected - it is not possible to backup the original
  firmware.
- According to Oligo support the pendants are specified for around 15000-20000 power
  on cycles.


## Building and Uploading the Custom Firmware

This is an Arduino project so building the firmware is rather straightforward. First modify your Arduino IDE boards.txt as described at the top of the INO file. After restarting the Arduino IDE you will be able to select the Oligo board variant of the Arduino Nano.

For uploading the firmware you need the following components:
- ATmega ISP, e.g. Arduino as ISP incl. USB/TTL adapter
- Molex Pico-EZmate connector, e.g. part no. 369200606, to attach ISP to Oligo board

I placed an Arduino Pro Mini 5V on a breadboard, added 2 LEDs to D8 and D9 for monitoring the ISP status, soldered a pin header to one end of the Molex cable, connected the pin header to D10 to D13 and uploaded the Arduino example project "ArduinoISP" after commenting in `#define USE_OLD_STYLE_WIRING`. 

Note: Do not connect the 5 V of the ISP to the 5 V of the Oligo board.

When the LED connected to D9 of the ISP is slowly fading in and out, the ISP is ready. If you have reached this point I recommend to disconnect DTR between the USB/TTL converter and the ISP to avoid accidentally flashing the ISP instead of the Oligo ATmega328P.

![FirmwareUpload](assets/FirmwareUpload.jpg "wiring the pendant for firmware upload")

Next you should test if the wiring between the ISP and the Oligo board is good by reading from the Oligo ATmega328P with avrdude. You need to power up the Oligo Grace pendant with 24 V. The following commands are for Windows. You will probably need to adjust the paths depending on your Arduino installation. Use the Arduino output during the upload of the Arduino ISP firmware as orientation.

Example:

    C:\Users\<username>\AppData\Local\Arduino15\packages\arduino\tools\avrdude\6.3.0-arduino17/bin/avrdude -C C:\Users\<username>\AppData\Local\Arduino15\packages\arduino\tools\avrdude\6.3.0-arduino17/etc/avrdude.conf -v -V -p atmega328p -c stk500v1 -P COM3 -b 19200 -D -U signature:r:-:i

This command should output the MCU signature confirming you have found a ATmega328 on the Oligo board and the current setting of the fuses. Compare the fuse values with the values documented at the top of the INO file.

> :warning: **WARNING**: If you decide to continue you will void the warranty
  of your Oligo Grace pendant and you risk permanently bricking your luminaire.
  You may only **USE THIS PROJECT AT YOUR OWN RISK**.
  I do not provide any warranty and I will not assume any responsibility for any
  damage you cause yourself or others by using this project.

After a successful upload of the custom firmware you should not notice significant functional differences, but it should fix the brightness settings memory.

It is probably not worth to go to such lengths to fix this problem. Using the Oligo repair service is the easiest way for most. This project just gives you another option.


## Power Consumption

In most of my projects I put part of the focus on the power consumption. With a LED luminaire you already save a lot of power if it is a replacement for an Edison light bulb. Nonetheless I wanted to know how much power the Oligo pendant needs and if the power consumption can be reduced any further. A measurement in brightness step mode yields the following results:

|  mode             | current [mA] @ 24 V DC | power [W] |
| ----------------: | ---------------------: | --------: |
|           standby |                     25 |       0.6 |
|  3.5 % brightness |                     46 |       1.1 |
| 16.5 % brightness |                    127 |       3.0 |
| 31.9 % brightness |                    225 |       5.4 |
|                on |                    638 |      15.3 |

Maximum power consumption is 15.3 W per pendant. Saving power when the luminaire is switched on is trivial by reducing the brightness to the level you need.

But the standby current of 25 mA seems unnecessarily high, especially if you consider that having a pendant duo will double this value. Looking at the schematic you can see that almost 8 mA pass from 24 V through FETs Q2 and Q4, just to keep the LED drivers disabled. The LED driver only needs 3.3 V to enable so using 5 V instead of 24 V would have been more than enough. This is a serious hardware design quirk that cannot be amended by software. The probable reason for this decision is that 5 V are not available on the top board - an extra wire and an extra soldering pad would have been required and would have increased production costs per pendant marginally. The only good thing is that the extra power consumption will be reduced slightly from a total of 0.37 W (0 V U3 EN) to about 0.23 W (5 V U3 EN) with increasing brightness.

The remaining 10 mA current consumption result from the combination of the proximity sensor module and the ATmega328P. The proximity sensor needs to stay on in standby mode, otherwise it would be impossible to turn on the light by gesture. An ATmega328P at 8 MHz will probably account for 8 mA of the 10 mA, so there may still be potential for power optimization.

My tests with an Arduino Nano 16 MHz board at 5 V show that the standby current can be reduced by almost 14 mA to ~3.4 mA and the power on current by ~7 mA to ~10 mA without loosing functionality when using passive waiting (CPU idle mode) instead of active waiting (delay). The effect on the Oligo board will be significantly less because the ATmega328P already saves ~50 % power by running at 8 MHz. 

A measurement with the custom firmware (version 1.0.4.0) in brightness step mode proofed this to be true. The supply current reduction (idle vs. delay) in standby mode is ~4 mA and at power on ~2 mA. This is not much by absolute standards at less than 1 kWh/year, but the relative effect from the ATmega328P perspective is still high with ~50 % and ~25 % power reduction respectively considering that an ATmega328P by itself uses ~8 mA at 8 MHz.

|   mode            | current [mA] @ 24 V DC | power [W] |
| ----------------: | ---------------------: | --------: |
|           standby |                26 → 22 |       0.5 |
|  4.0 % brightness |                52 → 52 |       1.2 |
| 16.0 % brightness |              128 → 127 |       3.0 |
| 32.0 % brightness |              229 → 227 |       5.5 |
|                on |              641 → 638 |      15.3 |

Note that the power consumptions listed above do not include the mains power adapter. One can expect that the total power consumption of a pendant duo with power adapter is slightly higher by at least 1 W / 80 %, making it around 2 W for standby and around 36 W when on.


## Known Problems

After about 10 months with the custom firmware both pendants switched to full brightness within the same minute and they did not react to the proximity sensor any more. The 8 MHz CPU clock was still running (L fuse: 0xA2, pin A1), but that was all I was able to diagnose from the outside. After reflashing the firmware the pendant reacted to the proximity sensor again, but in an arbitrary kind of way. Temporarily disabling the idle sleep mode restored the expected behaviour and it kept working after the sleep mode was activated again. Effectively I just flashed the firmware several times.

After a code review I made some minor code modification like removing the interrupt disable/enable from the ISRs, making sure that all variables used in the ISRs are marked volatile and increased the fading duration from 500 ms to 2 s. With this modified firmware the second pendant worked immediately after reflashing only once.

But if these code changes have any relation to the problem is hard to say without an in-circuit debugger. What I'm missing is a power fail detection to prevent writing to the EEPROM during that time. The ATmega328P brown out detection does not raise an interrupt but resets the MCU. This can happen in the middle of a write operation and may be unhealthy for the EEPROM data consistency. But this should not affect the flash data and the firmware uses an EEPROM checksum.


## Contributing

If you want something clarified or improved you may raise an [issue](https://github.com/jnsbyr/arduino-oligograce).


## Licenses and Credits

Copyright (c) 2022 [Jens B.](https://github.com/jnsbyr/arduino-oligograce)

[![License: Apache 2.0](https://img.shields.io/badge/License-Apache%202.0-blue.svg)](http://www.apache.org/licenses/LICENSE-2.0)

The code was edited with [Visual Studio Code](https://code.visualstudio.com).

The firmware was build using the [Arduino IDE](https://www.arduino.cc/en/software/).

The schematic was created using [KiCad](https://kicad.org/).

The badges in this document are provided by [img.shields.io](https://img.shields.io/).
