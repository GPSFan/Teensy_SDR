/*

FIR filter designed with
http://t-filter.appspot.com

sampling frequency: 44100 Hz

fixed point precision: 16 bits

* 0 Hz - 2700 Hz
  gain = 1
  desired ripple = 0.1 dB
  actual ripple = n/a

* 4000 Hz - 22000 Hz
  gain = 0
  desired attenuation = -70 dB
  actual attenuation = n/a



#define FILTER_TAP_NUM 66

static int filter_taps[FILTER_TAP_NUM] = {
*/
  155,
  91,
  77,
  23,
  -72,
  -203,
  -350,
  -484,
  -571,
  -579,
  -486,
  -288,
  -4,
  321,
  627,
  841,
  897,
  751,
  394,
  -136,
  -757,
  -1348,
  -1767,
  -1875,
  -1560,
  -765,
  497,
  2138,
  4002,
  5883,
  7555,
  8808,
  9479,
  9479,
  8808,
  7555,
  5883,
  4002,
  2138,
  497,
  -765,
  -1560,
  -1875,
  -1767,
  -1348,
  -757,
  -136,
  394,
  751,
  897,
  841,
  627,
  321,
  -4,
  -288,
  -486,
  -579,
  -571,
  -484,
  -350,
  -203,
  -72,
  23,
  77,
  91,
  155


