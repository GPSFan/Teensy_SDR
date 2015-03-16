/* simple software define radio using the Softrock transceiver 
 * the Teensy audio shield is used to capture and generate 16 bit audio
 * audio processing is done by the Teensy 3.1
 * simple UI runs on a 160x120 color TFT display - AdaFruit or Banggood knockoff which has a different LCD controller
 * Copyright (C) 2014  Rich Heslip rheslip@hotmail.com
 * Copyright (C) 2015  Ken McGuire
 * History:
 * 4/14 initial version by R Heslip VE3MKC
 * 6/14 Loftur E. Jónasson TF3LJ/VE2LJX - filter improvements, inclusion of Metro, software AGC module, optimized audio processing, UI changes
 * 1/15 RH - added encoder and SI5351 tuning library by Jason Milldrum <milldrum@gmail.com>
 *    - added HW AGC option which uses codec AGC module
 *    - added experimental waterfall display for CW
 * 2/28 KM adapt to latest Audio Library, Arduino 1.6 and 1.21 of Teensyduino
 * 3/9 KM SSB & CW TX mods, CW keyer with envelope waveshaping
 * ToDo:
 * adjust audio levels & gains for proper drive levels to RXTX
 * work on AGC
 * clean up some of the hard coded HW and UI stuff
 * document Teensy-audio-RXTX interconnections
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
 
#include <Metro.h>
#include <Audio.h>
#include <Wire.h>
#include <SD.h>
#include <Encoder.h>
#include "si5351.h"  // don't use jason branch 3/1/2015, it doesn't work yet
//#include <si5351.h>
#include <Bounce.h>
#include <Adafruit_GFX.h>   // LCD Core graphics library
#include <Adafruit_ST7735.h>// 1.8" TFT Module using Samsung ST7735 chip
//#include <Adafruit_QDTech.h>// 1.8" TFT Module using Samsung S6D02A1 chip
//#include <Adafruit_S6D02A1.h> // Hardware-specific library
//#include <TFT_S6D02A1.h> // Hardware-specific library
#include <SPI.h>
#include "filters.h"

// SW configuration defines
// don't use more than one AGC!
//#define SW_AGC   // define for Loftur's SW AGC - this has to be tuned carefully for your particular implementation
//#define HW_AGC // define for codec AGC - doesn't seem to work consistently. audio library bug ?

//#define CW_WATERFALL // define for experimental CW waterfall - needs faster update rate
#define AUDIO_STATS    // shows audio library CPU utilization etc on serial console

//#define SI5351_FREQ_MULT	100ULL  // when jason branch works this (and other stuff) will be needed
#define SI5351_FREQ_MULT	1ULL   // jason branch uses .01 hz steps, but tuning doesn't work, so this is 1 for now.

extern void agc(void);      // Moved the AGC function to a separate location

//SPI connections for Banggood 1.8" display
const int8_t sclk   = 5;
const int8_t mosi   = 4;
const int8_t dc     = 3;
const int8_t cs     = 2;
const int8_t rst    = 1;

// Switches between pin and ground for USB/LSB mode, wide and narrow filters, TX, CW and Key.
const int8_t ModeSW = 21;    // USB = low, LSB = high
const int8_t FiltSW = 20;    // 200 Hz CW filter = high
const int8_t TxSW = 0;       // TX = low
const int8_t TuneSW = 6;     // low for fast tune - encoder pushbutton
const int8_t CwSW = 12;      // CW mode, only matters in TX low = CW 
const int8_t KeySW = 10;     // CW Key start tone low = tone on

// unused pins 7, 8, 14

int ncofreq  = 11000;        // IF Oscillator
int test_freq = 2000;        // test tone freq
int cw_tone = 700;           // CW tone

// clock generator
Si5351 si5351;

// encoder switch
Encoder tune(16, 17);

Adafruit_ST7735 tft = Adafruit_ST7735(cs, dc, mosi, sclk, rst);
// Adafruit_S6D02A1 tft = Adafruit_S6D02A1(cs, dc, mosi, sclk, rst); // soft SPI
//Adafruit_S6D02A1 tft = Adafruit_S6D02A1(cs, dc,rst);  // hardware SPI

Metro five_sec=Metro(5000); // Set up a 5 second Metro
Metro loo_ms = Metro(100);  // Set up a 100ms Metro
Metro lcd_upd =Metro(100);  // Set up a Metro for LCD updates


// Create the Audio components.  These should be created in the
// order data flows, inputs/sources -> processing -> outputs
//
AudioControlSGTL5000 audioShield;  // Create an object to control the audio shield.

//  RX & TX support
const int myTx = AUDIO_INPUT_MIC;       // Transmit audio comes from mono Mic input
const int myRx = AUDIO_INPUT_LINEIN;    // Receive I & Q audio comes from the stereo Line Input

// FIR filters
AudioInputI2S           i2s1;           // Audio Shield: mic for TX, I & Q line-in for RX

//AudioSynthWaveform      test_tone;

AudioFilterFIR          Hilbert45_I;    // Hilbert filter +45
AudioFilterFIR          Hilbert45_Q;    // Hilbert filter -45

AudioFilterFIR          FIR_BPF;        // 2.4 kHz USB or LSB filter centred at either 12.5 or 9.5 kHz

AudioFilterFIR          postFIR;        // 2700Hz Low Pass filter or 200 Hz wide CW filter at 700Hz on audio output

AudioSynthWaveform      sine1;          // Local Oscillator RX & CW TX
AudioSynthWaveform      sine2;          // Local Oscillator CW TX
AudioSynthWaveform      sine3;          // Local Oscillator CW TX

AudioEffectMultiply     multiply1;      // Mixer (multiply inputs)

AudioEffectEnvelope     envelope1;      // Keyer attack & decay tries to eliminate "Key Clicks"
AudioEffectEnvelope     envelope2;      //

AudioMixer4             Summer1;        // Summer (add I & Q inputs for Rx)
AudioMixer4             Summer2;        // Summer (add for Tx displayinputs)
AudioMixer4             Summer3;        // Summer (select inputs for Rx Tx CW)
AudioMixer4             Summer4;        // Summer (select inputs for Rx Tx CW)

AudioAnalyzePeak        Smeter;         // Measure Audio Peak for S meter
AudioAnalyzeFFT256      myFFT;          // Spectrum Display
AudioAnalyzePeak        AGCpeak;        // Measure Audio Peak for AGC use

AudioOutputI2S          i2s2;           // Output the sum on both channels for RX to HP, I & Q for TX to Line Out 

// Create the Audio connections.

AudioConnection         c1(i2s1, 0, Hilbert45_I, 0);   // Audio Shield ADC to Hilbert phase shifter. Mic for TX, I Line In for RX
AudioConnection         c2(i2s1, 1, Hilbert45_Q, 0);   // Audio Shield ADC to Hilbert phase shifter. Mic for TX, Q Line In for RX

//AudioConnection         c1(test_tone, 0, Hilbert45_I, 0);
//AudioConnection         c2(test_tone, 0, Hilbert45_Q, 0);

AudioConnection         r1(Hilbert45_I, 0, Summer1, 0);     // Sum the Hilbert phase shifted filter outputs to supress the image
AudioConnection         r2(Hilbert45_Q, 0, Summer1, 1);
AudioConnection         r3(Summer1, 0, FIR_BPF, 0);         // 2.4 kHz USB or LSB filter centred at either 12.5 or 9.5 kHz

AudioConnection         r3a(Summer1, 0, Summer2, 0);        // RX FFT path
AudioConnection         r4(FIR_BPF, 0, multiply1, 0);       // IF from BPF to Mixer

AudioConnection         r5(sine1, 0, multiply1, 1);         // Local Oscillator (11 kHz) to Mixer
AudioConnection         r6(multiply1, 0, postFIR, 0);       // 2700Hz Low Pass filter or 200 Hz wide CW filter at 700Hz on audio output

AudioConnection         t8(Hilbert45_I, 0, Summer3, 1);     // Phase shifted Mic audio LSB TX path I
AudioConnection         t9(Hilbert45_Q, 0, Summer4, 1);     // Phase shifted Mic audio LSB TX path Q

AudioConnection         t8a(Hilbert45_Q, 0, Summer3, 2);    // Phase shifted Mic audio USB TX path I
AudioConnection         t9a(Hilbert45_I, 0, Summer4, 2);    // Phase shifted Mic audio USB TX path Q

AudioConnection         r7(postFIR, 0, Summer3, 0);         // RX filtered Audio path
AudioConnection         r8(postFIR, 0, Summer4, 0);         // RX filtered audio path

AudioConnection         c4(Summer3, 0, i2s2, 0);            // Phase shifted Mic audio TX I, Filtered RX audio
AudioConnection         c5(Summer4, 0, i2s2, 1);            // Phase shifted Mic audio TX Q, Filtered RX audio

AudioConnection         r30(postFIR, 0, Smeter, 0);         // S-Meter measure
AudioConnection         r40(Summer3, 0, AGCpeak, 0);        // AGC Gain loop measure

AudioConnection         t10(Summer3, 0, Summer2, 1);        // TX FFT path
AudioConnection         t11(Summer4, 0, Summer2, 2);        // TX FFT path
AudioConnection         t12(sine1, 0, envelope1, 0);        // CW TX path 
AudioConnection         t13(sine2, 0, envelope2, 0);        // CW TX path
AudioConnection         t12a(envelope1, 0, Summer3, 3);     // CW TX path 
AudioConnection         t13a(envelope2, 0, Summer4, 3);     // CW TX path

AudioConnection         c8(Summer2, 0, myFFT, 0);           // FFT for spectrum display

//---------------------------------------------------------------------------------------------------------

//long vfofreq=3560000;
//long vfofreq=7011000;
//long vfofreq=7850000; // CHU
long vfofreq=14236000;  // frequency of the SI5351 VFO
long cursorfreq;  // frequency of the on screen cursor which what we are listening to
int cursor_pos=0;
//long encoder_pos=0, last_encoder_pos=200;
long encoder_pos=0, last_encoder_pos=11000;
//long encoder_pos=0, last_encoder_pos=999;
elapsedMillis volmsec=0;

void setup() 
{
  pinMode(TxSW, INPUT_PULLUP);    // Tx switch, low = Tx
  pinMode(ModeSW, INPUT_PULLUP);  // USB = low, LSB = high
  pinMode(FiltSW, INPUT_PULLUP);  // 500Hz filter = high
  pinMode(TuneSW, INPUT_PULLUP);  // tuning rate = high
  pinMode(CwSW, INPUT_PULLUP);    // CW mode = low
  pinMode(KeySW, INPUT_PULLUP);   // Key = tone on = low
  
  // Audio connections require memory to work.  For more
  // detailed information, see the MemoryAndCpuUsage example
  AudioMemory(20);  //was 12 could maybe be smaller

  // Enable the audio shield and set the output volume.
  audioShield.enable();
  audioShield.inputSelect(myRx);
//  audioShield.volume(127);                 // funny this is said to be 0-1.0 in the audio shield docs
  audioShield.volume(.5);
  audioShield.muteLineout();
  audioShield.micGain(0);
  
#ifdef HW_AGC
  /* COMMENTS FROM Teensy Audio library:
    Valid values for dap_avc parameters
	maxGain; Maximum gain that can be applied
	0 - 0 dB
	1 - 6.0 dB
	2 - 12 dB
	lbiResponse; Integrator Response
	0 - 0 mS
	1 - 25 mS
	2 - 50 mS
	3 - 100 mS
	hardLimit
	0 - Hard limit disabled. AVC Compressor/Expander enabled.
	1 - Hard limit enabled. The signal is limited to the programmed threshold (signal saturates at the threshold)
	threshold
	floating point in range 0 to -96 dB
	attack
	floating point figure is dB/s rate at which gain is increased
	decay
	floating point figure is dB/s rate at which gain is reduced
*/
  audioShield.autoVolumeControl(2,1,0,-5,3,10); // see comments
  audioShield.autoVolumeEnable();

#endif

  // Stop the Audio stuff while manipulating parameters Initial setup for RX
  AudioNoInterrupts();

//  test_tone.begin(1.0,test_freq,TONE_TYPE_SINE);  
//  test_tone.amplitude(.4);
  
  // Local Oscillator at 11 KHz, or 11KHz+/- 700Hz for CW TX
  
  sine1.begin(1.0,ncofreq,TONE_TYPE_SINE);
  sine2.begin(1.0,ncofreq,TONE_TYPE_SINE);  
 
  // Initialize the +/-45 degree Hilbert filters
  Hilbert45_I.begin(hilbert45,HILBERT_COEFFS);
  Hilbert45_Q.begin(hilbertm45,HILBERT_COEFFS);
  
  // Initialize the USB/LSB filter
  FIR_BPF.begin(firbpf_usb,BPF_COEFFS);
  
  // Initialize the Low Pass filter
  postFIR.begin(postfir_lpf,COEFF_LPF);
  
  Summer1.gain(0,1);   // add inputs 1 & 2
  Summer1.gain(1,1);   // add inputs 1 & 2
  Summer1.gain(2,0);
  Summer1.gain(3,0);

  // Initialize envelope parameters
  
  envelope1.delay(0);
  envelope1.attack(10);
  envelope1.hold(0);
  envelope1.decay(10);
  envelope1.sustain(1.0);
  envelope1.release(10);
   
  envelope2.delay(0);
  envelope2.attack(10);
  envelope2.hold(0);
  envelope2.decay(10);
  envelope2.sustain(1.0);
  envelope2.release(10);
 
  // Start the Audio stuff
  AudioInterrupts(); 

   // Is this really needed? nope...

//  SPI.setMOSI(7); // set up SPI for use with the audio card - alternate pins
//  SPI.setSCK(14);

  // initialize the LCD display
//  tft.init();
  tft.initR(INITR_BLACKTAB);   // initialize a S6D02A1S chip, black tab
//  tft.setRotation(1);         // Normal orientation
  tft.setRotation(3);         // Inverted orientation for my mounting
  tft.fillScreen(ST7735_BLACK);
  tft.setCursor(0, 115);
  tft.setTextColor(ST7735_WHITE);
  tft.setTextWrap(true);
  tft.print("Teensy 3.1 SDR");
  
  // Show mid screen tune position
  tft.drawFastVLine(80, 0,60, ST7735_BLUE);
  
  // Set LCD defaults
  tft.setTextColor(ST7735_YELLOW);
  //tft.setTextSize(2);
  
  
  // Set up si5351 clk genenerator

  si5351.init(SI5351_CRYSTAL_LOAD_8PF);  // I used a 25mhz xtal from old ethernet switch so load cap in question  
  si5351.set_correction(+2250);          // I used my freq counter so it's not right on, but close,
  // Set CLK0 to output vfofreq * 4 with a fixed PLL frequency and multiplier if newer si5351 library
  si5351.set_pll(SI5351_PLL_FIXED, SI5351_PLLA);
  si5351.set_freq((unsigned long)vfofreq*4*SI5351_FREQ_MULT, SI5351_PLL_FIXED, SI5351_CLK0);
  delay(3);                             // wait for si5351 to settle
}


void loop() 
{
  static uint8_t mode, filter, modesw_state, filtersw_state, tx, txsw_state, cw, cwsw_state, key, keysw_state;
  // Force a first time USB/LSB Mode and filter update
  static uint8_t oldmode=0xff, oldfilter=0xff, oldtx=0xff, oldcw=0xff,  oldkey=0xff;
  long encoder_change;
  char string[80];   // print format stuff
  static uint8_t waterfall[80];  // array for simple waterfall display
  static uint8_t w_index=0, w_avg;
  
// tune radio using encoder switch  
 if (!tx)  // unless in TX ie. don't change freq while transmitting
{
  encoder_pos=tune.read();
  if (encoder_pos != last_encoder_pos) {
    encoder_change=encoder_pos-last_encoder_pos;
    last_encoder_pos=encoder_pos;
    // press encoder button for fast tuning
    if (digitalRead(TuneSW)) vfofreq+=encoder_change*5;  // tune the master vfo - 5hz steps
    else vfofreq+=encoder_change*500;  // fast tuning 500hz steps
    si5351.set_freq((unsigned long)vfofreq*4*SI5351_FREQ_MULT, SI5351_PLL_FIXED, SI5351_CLK0);
    tft.fillRect(100,115,100,120,ST7735_BLACK);
    tft.setCursor(100, 115);
    tft.setTextColor(ST7735_WHITE);
    cursorfreq=vfofreq+ncofreq; // frequency we are listening to
    sprintf(string,"%d.%03d.%03d",cursorfreq/1000000,(cursorfreq-cursorfreq/1000000*1000000)/1000,
          cursorfreq%1000 );               
    tft.print(string); 
  }
}
else
{
  tune.write(last_encoder_pos);  // if TX, reset to last encoder pos
}


  // every 50 ms, adjust the volume and check the switches
  if (volmsec > 50) {
    float vol = analogRead(15);
    vol = vol / 1023.0;
    audioShield.volume(vol);
    volmsec = 0;
    
    if (!digitalRead(ModeSW)) {
       if (modesw_state==0) { // switch was pressed - falling edge
         mode=!mode; 
         modesw_state=1; // flag switch is pressed
       }
    }
    else modesw_state=0; // flag switch not pressed
    
    if (!digitalRead(FiltSW)) {
       if (filtersw_state==0) { // switch was pressed - falling edge
         filter=!filter; 
         filtersw_state=1; // flag switch is pressed
       }
    }
    else filtersw_state=0; // flag switch not pressed

    if (!digitalRead(TxSW)) {
       if (txsw_state==0) { // switch was pressed - falling edge
         tx=!tx; 
         txsw_state=1; // flag switch is pressed
       }
    }
    else txsw_state=0; // flag switch not pressed
   
     if (!digitalRead(CwSW)) {
       if (cwsw_state==0) { // switch was pressed - falling edge
         cw=!cw; 
         cwsw_state=1; // flag switch is pressed
       }
    }
    else cwsw_state=0; // flag switch not pressed       
    
  }

#ifdef SW_AGC
  agc();  // Automatic Gain Control function
#endif  

// Check for key
    key    = !digitalRead(KeySW);
         if (!key)     // if the key is down      
         {
          if (keysw_state==0)   // and if the state is 0
           { // switch was pressed - falling edge
             key=!key;         
             keysw_state=1; // flag switch as pressed ie key down
           }
         }
         else keysw_state=0; // flag switch not pressed ie key up
    
    if (key != oldkey)       // if old and new are different
   {
       if (keysw_state == 0)  
      {
         envelope1.noteOn();
         envelope2.noteOn();
      }
      else
      {
         envelope1.noteOff();
         envelope2.noteOff();
      }
      oldkey = key;
   }
    
  //
  // Select RX/TX, USB/LSB mode and a corresponding 2.4kHz or 500Hz filter
  //
  
  if (loo_ms.check() == 1)  // this happens every 100ms
  {
//    mode   = !digitalRead(ModeSW);
//    filter = !digitalRead(FiltSW);
    tx     = !digitalRead(TxSW);
    cw     = !digitalRead(CwSW);
    
    if ((mode != oldmode)||(filter != oldfilter)||(tx != oldtx)||(cw != oldcw))
    {
      AudioNoInterrupts();   // Disable Audio while reconfiguring filters
//      tft.drawFastHLine(0,61, 160, ST7735_BLACK);   // Clear LCD BW indication
//      tft.drawFastHLine(0,62, 160, ST7735_BLACK);   // Clear LCD BW indication
     
      tft.drawFastHLine(0,62, 160, ST7735_YELLOW);   // Clear LCD BW indication

     if (tx)
     {       
       
       tft.drawFastVLine(80, 0,60, ST7735_BLACK);       
       
       // Setup TX path switches
       
        Summer2.gain(0,0);
        Summer2.gain(1,.5);   // add inputs 1 & 2 for FFT display
        Summer2.gain(2,.5);
        Summer2.gain(3,0);

       // Setup audio shield config for TX  
        
        audioShield.inputSelect(myTx);
//        audioShield.volume(127);                 // funny this is said to be 0-1.0 in the audio shield docs
//        audioShield.volume(.5);
        audioShield.unmuteLineout();
        audioShield.muteHeadphone();
        audioShield.micGain(0);
        
        // Reset filters for TX
        
        FIR_BPF.end();   // shut off RX path filters to save CPU
        postFIR.end();   // shut off RX path filters to save CPU
       
        // Setup USB/LSB/CW generator
        
        tft.fillRect(60, 85, 30, 7,ST7735_BLACK);// (x, y, w, h, color)

     if (cw)
     {
       
          tft.setCursor(60, 85); // (x, y)
          tft.print("CW TX");         
          
          Summer3.gain(0,0);
          Summer3.gain(1,0);
          Summer3.gain(2,0);
          Summer3.gain(3,1);

          Summer4.gain(0,0);
          Summer4.gain(1,0); 
          Summer4.gain(2,0);
          Summer4.gain(3,1);         
          
        if (mode)  // LSB = 1
        {
          
          // Set the following levels so that your Transciever doesn't cause distortion .68 produces about .8v p-p 
          // This is for CW only and has no bearing on ssb mode.
          
          sine1.begin(.68,ncofreq - cw_tone,TONE_TYPE_SINE); // CW LSB
          sine2.begin(.68,ncofreq - cw_tone,TONE_TYPE_SINE); // CW LSB         
        }
        else
        {
          sine1.begin(0.68,ncofreq + cw_tone,TONE_TYPE_SINE); //CW USB
          sine2.begin(0.68,ncofreq + cw_tone,TONE_TYPE_SINE); //CW USB          
        }
         sine1.phase(90);
         sine2.phase(0);
     }
     else
      {
          sine1.begin(1.0,ncofreq,TONE_TYPE_SINE);
          sine2.begin(1.0,ncofreq,TONE_TYPE_SINE);
        
          si5351.set_freq((unsigned long)(vfofreq+ncofreq)*4*SI5351_FREQ_MULT, SI5351_PLL_FIXED, SI5351_CLK0);  // adjust TX freq for nco offset
        
        if (mode)
        {                           
          Summer3.gain(0,0);
          Summer3.gain(1,1);  // Select TX path LSB
          Summer3.gain(2,0);
          Summer3.gain(3,0);
        
          Summer4.gain(0,0);
          Summer4.gain(1,1);  //Select TX path LSB
          Summer4.gain(2,0);
          Summer4.gain(3,0);
          
        }
        else
        {
          Summer3.gain(0,0);
          Summer3.gain(1,0);
          Summer3.gain(2,1);  // Select TX path USB
          Summer3.gain(3,0);
        
          Summer4.gain(0,0);
          Summer4.gain(1,0);
          Summer4.gain(2,1);  //Select TX path USB
          Summer4.gain(3,0);
        }  
      } 
     }
     else   //RX
     {
       
        si5351.set_freq((unsigned long)vfofreq*4*SI5351_FREQ_MULT, SI5351_PLL_FIXED, SI5351_CLK0);   // return to RX freq 
        sine1.begin(1.0,ncofreq,TONE_TYPE_SINE);                                                     // return NCO to 11KHz
        sine2.begin(1.0,ncofreq,TONE_TYPE_SINE);
      
        tft.fillRect(60, 85, 30, 7,ST7735_BLACK);// (x, y, w, h, color)
        tft.drawFastVLine(80, 0,60, ST7735_BLUE);              
       // Setup RX path switches
       
        Summer2.gain(0,1);   // Select output of Summer1 to display Rx spectrum
        Summer2.gain(1,0);
        Summer2.gain(2,0);
        Summer2.gain(3,0);
        
        Summer3.gain(0,1);   // Select RX path
        Summer3.gain(1,0);
        Summer3.gain(2,0);
        Summer3.gain(3,0);
        
        Summer4.gain(0,1);   // Select RX Path
        Summer4.gain(1,0);
        Summer4.gain(2,0);
        Summer4.gain(3,0);
        
       // Setup audio shield config for RX
       
        audioShield.inputSelect(myRx);
//        audioShield.volume(127);                 // funny this is said to be 0-1.0 in the audio shield docs
//        audioShield.volume(.5);
        audioShield.muteLineout();
        audioShield.unmuteHeadphone();
        audioShield.lineInLevel(10);

      // Filter config for RX depends on mode and filter width         
       
      if (mode)                                     // LSB
      {
        FIR_BPF.begin(firbpf_lsb,BPF_COEFFS);       // 2.4kHz LSB filter       
        if (filter)
        {
          postFIR.begin(postfir_700,COEFF_700);     // 500 Hz filter
          tft.drawFastHLine(72,61,6, ST7735_RED);   // (x, y, w, color)
          tft.drawFastHLine(72,62,6, ST7735_RED);
          tft.fillRect(100, 85, 60, 7,ST7735_BLACK);// Print Mode (x, y, w, h, color)
          tft.setCursor(100, 85);
          tft.print("LSB narrow");
        }
        else
        {
          postFIR.begin(postfir_lpf,COEFF_LPF);     // 2.4kHz filter
          tft.drawFastHLine(61,61,20, ST7735_RED);
          tft.drawFastHLine(61,62,20, ST7735_RED);
          tft.fillRect(100, 85, 60, 7,ST7735_BLACK);// Print Mode
          tft.setCursor(100, 85);
          tft.print("LSB");
        }
      }
      else                                          // USB
      {
        FIR_BPF.begin(firbpf_usb,BPF_COEFFS);       // 2.4kHz USB filter
        if (filter)
        {
          postFIR.begin(postfir_700,COEFF_700);     // 500 Hz filter
          tft.drawFastHLine(83,61,6, ST7735_RED);
          tft.drawFastHLine(83,62,6, ST7735_RED);
          tft.fillRect(100, 85, 60, 7,ST7735_BLACK);// Print Mode
          tft.setCursor(100, 85);
          tft.print("USB narrow");
        }
        else
        {
          postFIR.begin(postfir_lpf,COEFF_LPF);     // 2.4kHz filter
          tft.drawFastHLine(80,61,20, ST7735_RED);
          tft.drawFastHLine(80,62,20, ST7735_RED);
          tft.fillRect(100, 85, 60, 7,ST7735_BLACK);// Print Mode
          tft.setCursor(100, 85);
          tft.print("USB");
        }
      }

    }
      AudioInterrupts(); 
      oldmode = mode;
      oldfilter = filter;
      oldtx = tx;
      oldcw = cw;
    
    } 
  }

  //
  // Draw Spectrum Display
  //
  int scale = 1;
  if (lcd_upd.check() == 1)
  {
    if (myFFT.available()) 
    {

     if (tx & cw) scale = 32; //orig was 1 for RX, 4 works ok for TX and 32 for CW TX
     if (tx & !cw) scale = 4;
     
       //for (int16_t x=0; x < 160; x++) 
       for (int16_t x=0; x < 160; x+=2) 
       {
         int bar=abs(myFFT.output[x*8/10])/scale;
         if (bar >60) bar=60;

         if(x!=80)
         {
           if (!digitalRead(TxSW)) {
           tft.drawFastVLine(x, 60-bar,bar, ST7735_RED);
           }
           else
           {
           tft.drawFastVLine(x, 60-bar,bar, ST7735_GREEN);
           }
           tft.drawFastVLine(x, 0, 60-bar, ST7735_BLACK);    
         }
      }
    } 
 
#ifdef CW_WATERFALL
  // experimental waterfall display for CW -
  // this should probably be on a faster timer since it needs to run as fast as possible to catch CW edges
  //  FFT bins are 22khz/128=171hz wide 
  // cw peak should be around 11.6khz - 
    waterfall[w_index]=0;
    for (uint8_t y=66;y<=68;++y)  // sum of bin powers near cursor - usb only
      waterfall[w_index]+=(uint8_t)(abs(myFFT.output[y])/2); // store bin power readings in circular buffer
    w_avg=w_avg-w_avg/20; // running average power
    int8_t p=w_index;
    for (uint8_t x=158;x>0;x-=2) {
      if (waterfall[p] > w_avg/20+4) tft.fillRect(x,70,2,2,ST7735_WHITE);
      else tft.fillRect(x,70,2,2,ST7735_BLACK);
      if (--p<0 ) p=79;
    }
    if (++w_index >=80) w_index=0;
#endif
  }

#ifdef AUDIO_STATS
  //
  // DEBUG - Microcontroller Load Check
  //
  // Change this to if(1) to monitor load

  /*
  For PlaySynthMusic this produces:
  Proc = 20 (21),  Mem = 2 (8)
  */  
    if (five_sec.check() == 1)
    {
      Serial.print("Proc = ");
      Serial.print(AudioProcessorUsage()); //
      Serial.print(" (");    
      Serial.print(AudioProcessorUsageMax());
      Serial.print("),  Mem = ");
      Serial.print(AudioMemoryUsage());
      Serial.print(" (");    
      Serial.print(AudioMemoryUsageMax());
      Serial.println(")");
    }
#endif
}


