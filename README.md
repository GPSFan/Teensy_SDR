# Teensy_SDR
Software defined radio using Teensy 3.1, Teensy Audio shield and Softrock

This is a software defined radio using the Arduino compatible Teensy 3.1 and audio shield from PJRC. 
It takes advantage of the audio library supplied by PJRC to implement the audio signal processing chain.

Dependencies - most of these libraries are available from PJRC and can be installed with the Teensyduino package:

Audio (Teensy Audio library, code currently written for the beta release),
SD (not used but needed for the audio libraries to compile),
SPI,
Wire,
Metro,
Bounce,
Encoder,
SI5351 (github.com/NT7S/Si5351),
Adafruit Graphics library (adafruit.com),
Driver for your display (I used a Banggood knockoff of the AdaFruit 1.8" TFT)


Mods by GPSFan   Ken McGuire  WA2VFN  2/28/2015

- update code for compatibility with Teensyduino 1.2 and audio library 1.02 ... working
- SSB transmit mode ... appears to be working
- TX - RX  switching ... appears to be working

Still TODO:

- CW transmit mode ... implememted, needs more work
- band switching
- internal keyer
- UI improvements - configuration settings etc

project blog: rheslip.blogspot.com

