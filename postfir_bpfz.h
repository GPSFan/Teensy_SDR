/*

FIR filter designed with
http://t-filter.appspot.com

sampling frequency: 44100 Hz

fixed point precision: 16 bits

* 0 Hz - 8000 Hz
  gain = 0
  desired attenuation = -70 dB
  actual attenuation = n/a

* 9500 Hz - 12500 Hz
  gain = 1
  desired ripple = 0.5 dB
  actual ripple = n/a

* 14000 Hz - 22000 Hz
  gain = 0
  desired attenuation = -70 dB
  actual attenuation = n/a



#define FILTER_TAP_NUM 66

static int filter_taps[FILTER_TAP_NUM] = {
*/
  53,
  76,
  -140,
  -124,
  208,
  172,
  -229,
  -141,
  119,
  -20,
  165,
  336,
  -612,
  -766,
  1119,
  1189,
  -1499,
  -1408,
  1516,
  1205,
  -964,
  -413,
  -253,
  -1010,
  2059,
  2928,
  -4190,
  -5035,
  6245,
  6918,
  -7792,
  -8164,
  8483,
  8483,
  -8164,
  -7792,
  6918,
  6245,
  -5035,
  -4190,
  2928,
  2059,
  -1010,
  -253,
  -413,
  -964,
  1205,
  1516,
  -1408,
  -1499,
  1189,
  1119,
  -766,
  -612,
  336,
  165,
  -20,
  119,
  -141,
  -229,
  172,
  208,
  -124,
  -140,
  76,
  53

