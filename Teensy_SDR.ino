/* simple software define radio using the Softrock transceiver 
 * the Teensy audio shield is used to capture and generate 16 bit audio
 * audio processing is done by the Teensy 3.1
 * simple UI runs on a 160x120 color TFT display - AdaFruit or Banggood knockoff which has a different LCD controller
 * Copyright (C) 2014  Rich Heslip rheslip@hotmail.com
 * History:
 * 4/14 initial version by R Heslip VE3MKC
 * 6/14 Loftur E. Jónasson TF3LJ/VE2LJX - filter improvements, inclusion of Metro, software AGC module, optimized audio processing, UI changes
 * 1/15 RH - added encoder and SI5351 tuning library by Jason Milldrum <milldrum@gmail.com>
 *    - added HW AGC option which uses codec AGC module
 *    - added experimental waterfall display for CW
 * 2/28 KM adapt to latest Audio Library, Arduino 1.6 and 1.21 of Teensyduino
 * ToDo:
 * implement transmit mode: should be able to do this by switching hilbert filters down to 300-2.7khz and adding a mixer route to line out.
 * clean up some of the hard coded HW and UI stuff 
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
#include "si5351.h"  // don't use jason branch 3/1/2015
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
#define HW_AGC // define for codec AGC - doesn't seem to work consistently. audio library bug ?

//#define CW_WATERFALL // define for experimental CW waterfall - needs faster update rate
#define AUDIO_STATS    // shows audio library CPU utilization etc on serial console

//#define SI5351_FREQ_MULT	100ULL
#define SI5351_FREQ_MULT	1ULL   // jason branch uses .01 hz steps, but tuning doesn't work, so this is 1 for now.

extern void agc(void);      // Moved the AGC function to a separate location

//SPI connections for Banggood 1.8" display
const int8_t sclk   = 5;
const int8_t mosi   = 4;
const int8_t dc     = 3;
const int8_t cs     = 2;
const int8_t rst    = 1;

// Switches between pin and ground for USB/LSB mode, wide and narrow filters
const int8_t ModeSW =21;    // USB = low, LSB = high
const int8_t FiltSW =20;    // 200 Hz CW filter = high
const int8_t TxSW = 0;    // TX = low
const int8_t TuneSW =6;    // low for fast tune - encoder pushbutton

// unused pins 4,5, 10 (SDCS)

int ncofreq  = 11000;       // IF Oscillator
int test_freq = 2000;         // test tone freq

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
const int myTx = AUDIO_INPUT_MIC;
const int myRx = AUDIO_INPUT_LINEIN;

// FIR filters
AudioInputI2S           i2s1;           // Audio Shield: mic for TX, line-in for RX

AudioFilterFIR          Hilbert45_I;    //Hilbert filter +45
AudioFilterFIR          Hilbert45_Q;    //Hilbert filter -45

AudioFilterFIR          FIR_BPF;        // 2.4 kHz USB or LSB filter centred at either 12.5 or 9.5 kHz

AudioFilterFIR          postFIR;        // 2700Hz Low Pass filter or 200 Hz wide CW filter at 700Hz on audio output

AudioSynthWaveform      test_tone;
AudioAnalyzeFFT256      myFFT;          // Spectrum Display
AudioSynthWaveform      sine1;          // Local Oscillator  
AudioSynthWaveform      sine2;          // Local Oscillator 

AudioEffectMultiply     multiply1;      // Mixer (multiply inputs)


AudioOutputI2S          i2s2;        // Output the sum on both channels for RX, I & Q for TX  
AudioMixer4             Summer1;     // Summer (add I & Q inputs for Rx)
AudioMixer4             Summer2;     // Summer (add for Tx displayinputs)
AudioMixer4             Summer3;     // Summer (select inputs for Rx Tx CW)
AudioMixer4             Summer4;     // Summer (select inputs for Rx Tx CW)

//AudioAnalyzePeak        Smeter;        // Measure Audio Peak for S meter
//AudioMixer4             AGC;           // Summer (add inputs)
//AudioAnalyzePeak        AGCpeak;       // Measure Audio Peak for AGC use

AudioConnection         c1(i2s1, 0, Hilbert45_I, 0);
AudioConnection         c2(i2s1, 1, Hilbert45_Q, 0);

//AudioConnection         c1(test_tone, 0, Hilbert45_I, 0);
//AudioConnection         c2(test_tone, 0, Hilbert45_Q, 0);


AudioConnection         r1(Hilbert45_I, 0, Summer1, 0);     // Sum the shifted filter outputs to supress the image
AudioConnection         r2(Hilbert45_Q, 0, Summer1, 1);
AudioConnection         r3(Summer1, 0, FIR_BPF, 0);         // 2.4 kHz USB or LSB filter centred at either 12.5 or 9.5 kHz

AudioConnection         r3a(Summer1, 0, Summer2, 0);         // RX FFT path
AudioConnection         r4(FIR_BPF, 0, multiply1, 0);       // IF from BPF to Mixer

AudioConnection         r5(sine1, 0, multiply1, 1);         // Local Oscillator to Mixer (11 kHz)
AudioConnection         r6(multiply1, 0, postFIR, 0);       // 2700Hz Low Pass filter or 200 Hz wide CW filter at 700Hz on audio output

AudioConnection         t8(Hilbert45_I, 0, Summer3, 1);
AudioConnection         t9(Hilbert45_Q, 0, Summer4, 1);

AudioConnection         t8a(Hilbert45_Q, 0, Summer3, 2);
AudioConnection         t9a(Hilbert45_I, 0, Summer4, 2);

AudioConnection         c4(Summer3, 0, i2s2, 1);
AudioConnection         c5(Summer4, 0, i2s2, 0);

AudioConnection         r7(postFIR,0, Summer3, 0);
AudioConnection         r8(postFIR,0, Summer4, 0);

//AudioConnection         r30(postFIR,0, Smeter, 0);        // S-Meter measure
//AudioConnection         r31(postFIR,0, AGC, 0);           // AGC Gain loop adjust
//AudioConnection         r40(AGC, 0, AGCpeak, 0);          // AGC Gain loop measure


AudioConnection         t10(Summer3, 0, Summer2, 1);        // TX FFT path
AudioConnection         t11(Summer4, 0, Summer2, 2);        // TX FFT path
AudioConnection         t12(sine1, 0, Summer3, 3);          // CW TC path 
AudioConnection         t13(sine2, 0, Summer4, 3);          // CW TX path

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
  pinMode(TxSW, INPUT_PULLUP); // Tx switch, low = Tx
  pinMode(ModeSW, INPUT_PULLUP);  // USB = low, LSB = high
  pinMode(FiltSW, INPUT_PULLUP);  // 500Hz filter = high
  pinMode(TuneSW, INPUT_PULLUP);  // tuning rate = high
  
  // Audio connections require memory to work.  For more
  // detailed information, see the MemoryAndCpuUsage example
  AudioMemory(20);  //was 12

  // Enable the audio shield and set the output volume.
  audioShield.enable();
  audioShield.inputSelect(myRx);
  audioShield.volume(127);
  audioShield.unmuteLineout();
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

  test_tone.begin(1.0,test_freq,TONE_TYPE_SINE);
  
  test_tone.amplitude(.4);
  
  // Local Oscillator at 11 kHz
  
  sine1.begin(1.0,ncofreq,TONE_TYPE_SINE);
  sine2.begin(1.0,ncofreq,TONE_TYPE_SINE);
  
  sine1.phase(90);
  
  // Initialize the +/-45 degree Hilbert filters
  Hilbert45_I.begin(hilbert45,HILBERT_COEFFS);
  Hilbert45_Q.begin(hilbertm45,HILBERT_COEFFS);
  
  // Initialize the USB/LSB filter
  FIR_BPF.begin(firbpf_usb,BPF_COEFFS);
  
  // Initialize the Low Pass filter
  postFIR.begin(postfir_lpf,COEFF_LPF);

 
  // Start the Audio stuff
  AudioInterrupts(); 

  SPI.setMOSI(7); // set up SPI for use with the audio card - alternate pins
  SPI.setSCK(14);

  // initialize the LCD display
//  tft.init();
  tft.initR(INITR_BLACKTAB);   // initialize a S6D02A1S chip, black tab
//  tft.setRotation(1);  // Normal orientation
  tft.setRotation(3);    // Inverted orientation
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
  // set up clk gen

  si5351.init(SI5351_CRYSTAL_LOAD_8PF);  // used 25mhz xtal from old ethernet switch so load cap in question  
  si5351.set_correction(+2250);  // I used my freq counter so it's not right on, but close,
  // Set CLK0 to output vfofreq * 4 with a fixed PLL frequency and multiplier if newer si5351 library
  si5351.set_pll(SI5351_PLL_FIXED, SI5351_PLLA);
  si5351.set_freq((unsigned long)vfofreq*4*SI5351_FREQ_MULT, SI5351_PLL_FIXED, SI5351_CLK0);
  delay(3);
}


void loop() 
{
  static uint8_t mode, filter, modesw_state, filtersw_state, tx, txsw_state;
  // Force a first time USB/LSB Mode and filter update
  static uint8_t oldmode=0xff, oldfilter=0xff, oldtx=0xff;
  long encoder_change;
  char string[80];   // print format stuff
  static uint8_t waterfall[80];  // array for simple waterfall display
  static uint8_t w_index=0, w_avg;

// tune radio using encoder switch  
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
  }

#ifdef SW_AGC
  agc();  // Automatic Gain Control function
#endif  

  //
  // Select RX/TX, USB/LSB mode and a corresponding 2.4kHz or 500Hz filter
  //
  if (loo_ms.check() == 1)
  {
//    mode   = !digitalRead(ModeSW);
//    filter = !digitalRead(FiltSW);
    tx     = !digitalRead(TxSW);
    if ((mode != oldmode)||(filter != oldfilter)||(tx != oldtx))
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
        Summer2.gain(1,1);   // add inputs 1 & 2 for FFT display
        Summer2.gain(2,1);
        Summer2.gain(3,0);

       // Setup audio shield config for TX  
        
        audioShield.inputSelect(myTx);
        audioShield.volume(127);
        audioShield.unmuteLineout();
        audioShield.muteHeadphone();
        audioShield.micGain(0);
        
        // Reset filters for TX
        
        FIR_BPF.end();   // shut off RX path filters to save CPU
        postFIR.end();   // shut off RX path filters to save CPU
       
        // Setup USB/LSB generator
        
        if (mode)
        {
          sine1.phase(0);      // CW only
          sine2.phase(90);
                           
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
          sine1.phase(90);    // CW only
          sine2.phase(0);
                           
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
     else   //RX
     {
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
        audioShield.volume(127);
        audioShield.muteLineout();
        audioShield.unmuteHeadphone();
        audioShield.lineInLevel(10);

      // Filter config for RX depends on mode and filter width         
       
      if (mode)                                     // LSB
      {
        FIR_BPF.begin(firbpf_lsb,BPF_COEFFS);       // 2.4kHz LSB filter       
        if (filter)
        {
          postFIR.begin(postfir_700,COEFF_700);     // 500 Hz LSB filter
          tft.drawFastHLine(72,61,6, ST7735_RED);
          tft.drawFastHLine(72,62,6, ST7735_RED);
          tft.fillRect(100, 85, 60, 7,ST7735_BLACK);// Print Mode
          tft.setCursor(100, 85);
          tft.print("LSB narrow");
        }
        else
        {
          postFIR.begin(postfir_lpf,COEFF_LPF);     // 2.4kHz LSB filter
          tft.drawFastHLine(61,61,20, ST7735_RED);
          tft.drawFastHLine(61,62,20, ST7735_RED);
          tft.fillRect(100, 85, 60, 7,ST7735_BLACK);// Print Mode
          tft.setCursor(100, 85);
//          sine1.phase(0);
//          sine2.phase(90);
          tft.print("LSB");
        }
      }
      else                                          // USB
      {
        FIR_BPF.begin(firbpf_usb,BPF_COEFFS);       // 2.4kHz USB filter
        if (filter)
        {
          postFIR.begin(postfir_700,COEFF_700);     // 500 Hz LSB filter
          tft.drawFastHLine(83,61,6, ST7735_RED);
          tft.drawFastHLine(83,62,6, ST7735_RED);
          tft.fillRect(100, 85, 60, 7,ST7735_BLACK);// Print Mode
          tft.setCursor(100, 85);
          tft.print("USB narrow");
        }
        else
        {
          postFIR.begin(postfir_lpf,COEFF_LPF);     // 2.4kHz LSB filter
          tft.drawFastHLine(80,61,20, ST7735_RED);
          tft.drawFastHLine(80,62,20, ST7735_RED);
          tft.fillRect(100, 85, 60, 7,ST7735_BLACK);// Print Mode
          tft.setCursor(100, 85);
//          sine1.phase(90);
//          sine2.phase(0);
          tft.print("USB");
        }
      }

    }
      AudioInterrupts(); 
      oldmode = mode;
      oldfilter = filter;
      oldtx = tx;
    
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
      if (tx)
      {
      scale=4;  //orig was 1 for RX, 4 works ok for TX
      }
      else 
      {
      scale=1;
      }
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


