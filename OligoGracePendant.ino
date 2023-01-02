/*****************************************************************************
 *
 * Oligo Grace Pendant
 *
 * file:     OligoGracePendant.ino
 * encoding: UTF-8
 * created:  27.12.2022
 *
 * ****************************************************************************
 *
 * Copyright (C) 2022 Jens B.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * ****************************************************************************
 *
 * description:
 *
 * customized firmware for Oligo Grace LED Pendant with proximity sensor (model G42-931)
 *
 * - created to fix settings persistence and startup failure
 * - provides most features of the original:
 *   - light on on power on
 *   - proximity < 600 ms: on/off
 *   - proximity > 800 ms: dim up/down
 *   - dim limit reached: 2 short blinks
 *   - dim hold > 5 s: toggle dim mode continuous/3 steps (3 longs blinks)
 *   - limit brightness to minimum when board temperature exceeds 85 °C
 *
 * ****************************************************************************
 *
 * Oligo Grace MCU: ATmega328P - 5V
 *
 * - standby power consumption per pendant: 0.6 W (24 V, 26 mA)
 * - power on current per pendant: ~0.8 A
 * - power on consumption per pendant: 1.5 W ... 15 W
 *
 *
 * Oligo factory MCU settings:
 *
 * - 8 MHz internal RC oscillator (L fuse: 0xE2)
 * - oscillator calibrated (0xA8)
 * - no bootloader, watchdog disabled (H fuse: 0xDF)
 * - 2.7 V BOD enabled (E fuse: 0xFD)
 * - flash and EEPROM locked (0x30)
 *
 *
 * Custom firmware MCU settings:
 *
 * - H, L and E fuse: no change required
 * - flash and EEPROM not locked
 *
 *
 * Custom firmware Arduino IDE settings:
 *
 * - Arduino Nano 8 MHz variant, add to Arduino boards.txt:
 *
 *   ## Oligo Grace (ATmega328P 8 MHz)
 *   ## --------------------------
 *   nano.menu.cpu.oligo=Oligo Grace (ATmega328P 8 MHz)
 *   nano.menu.cpu.oligo.upload.maximum_size=30720
 *   nano.menu.cpu.oligo.upload.maximum_data_size=2048
 *   nano.menu.cpu.oligo.upload.speed=19200
 *   nano.menu.cpu.oligo.bootloader.low_fuses=0xE2
 *   nano.menu.cpu.oligo.bootloader.high_fuses=0xDF
 *   nano.menu.cpu.oligo.bootloader.extended_fuses=0xFD
 *   nano.menu.cpu.oligo.bootloader.file=
 *   nano.menu.cpu.oligo.build.mcu=atmega328p
 *   nano.menu.cpu.oligo.build.f_cpu=8000000L
 *
 * - vscode: manually add "__AVR_ATmega328P__" to c_cpp_properties.json
 *
 *
 * Things required to upload firmware:
 *
 * - Molex Pico-EZmate connector to attach ISP to Oligo board, e.g. part no. 369200606
 * - ATmega ISP, e.g. Arduino as ISP incl. USB/TTL adapter
 *
 *****************************************************************************
 *
 * prototype developed on: Sparkfun Pro Micro
 *
 * - vscode: manually add "__AVR_ATmega32U4__" to c_cpp_properties.json
 *
 *****************************************************************************
 *
 * possible additional features:
 * - smoother continuous dim
 * - board over temperature hysteresis
 * - fade on/off
 * - reduce IO clock to 4 MHz
 * - 5 short blinks when board over temperature is detected for the 1st time
 * - hold > 15s at power up to enable/disable proximity sensor
 * - sync setting of 2 pendants
 * - change setting over air (WiFi/Bluetooth)
 *
 *****************************************************************************/

#define VERSION "1.0.0.0" // 01.01.2023

// Arduino includes, LGPL licence
#include <EEPROM.h>
#include <HardwareSerial.h>
#include <wiring_private.h>

// AVR includes, modified BSD licence
#include <avr/io.h>
#include <avr/sfr_defs.h>
#include <avr/wdt.h>
#include <util/crc16.h>

//#define DEBUG // comment in if serial debugging is needed, will also disables bottom panel
#ifdef DEBUG
  #define printDebug(x) Serial.print(x)
  #define printlnDebug(x) Serial.println(x)
#else
  #define printDebug(x)
  #define printlnDebug(x)
#endif

// inputs pins
#define PIN_PROXIMITY  2
#define PIN_NTC       A7

// outputs pins
#define PIN_BRIGHT_P1  9 // bottom
#define PIN_BRIGHT_P2 10 // top
#define PIN_BRIGHT_P3 11 // N/C

// EEPROM parameters
#define EEPROM_ADDRESS 0x22  // max. 0x200
#define EEPROM_MAGIC   0xAFFE

#define BACKUP_DELAY   10000 // [ms]

// proximity gesture timing
#define DURATION_MIN             100 // [ms]
#define DURATION_POWER_MAX       600 // [ms]
#define DURATION_DIM_MIN         800 // [ms]
#define DURATION_TOGGLE_STEP    5000 // [ms]
#define DURATION_DIM_STEP_SMALL   80 // [ms]
#define DURATION_DIM_STEP_BIG   DURATION_DIM_MIN // [ms]

// brightness parameters
#define BRIGHTNESS_BIG_STEPS    3 // number of dim steps between min and max
#define BRIGHTNESS_MIN          4 // [%]
#define BRIGHTNESS_MAX         85 // [%]
#define BRIGHTNESS_STEP_SMALL   2 // [%]
#define BRIGHTNESS_STEP_BIG   ((BRIGHTNESS_MAX - BRIGHTNESS_MIN)/BRIGHTNESS_BIG_STEPS) // [%]
#define BRIGHTNESS_DEFAULT    (BRIGHTNESS_MAX - BRIGHTNESS_STEP_BIG) // [%], step 2/3

#define BLINK_PERIOD_FAST  50 // [ms]
#define BLINK_PERIOD_SLOW 800 // [ms]

// board temperature parameters
#define NTC_TEMPERATURE_MAX      85 // [°C]
#define DIVIDER_RESISTANCE    47000 // [Ohm]
#define NTC_RESISTANCE     47000.0f // [Ohm]
#define NTC_TEMP_NOM          25.0f // [°C]
#define NTC_B_COEFFICIENT   4131.0f // B constant for 25-100°C
#define KELVIN_CELSIUS      273.15f // [°C]


// persistent settings
struct Settings
{
  int magic = EEPROM_MAGIC;
  int brightness = BRIGHTNESS_DEFAULT; // [%]
  bool dimBigSteps = true;
  uint16_t crc = 0;
};
Settings settings;

// ISR variables
volatile int near = false;
volatile int blinkToggleCount = 0;
volatile unsigned long approach = 0;
volatile bool initalApproach = false;
volatile bool outputEnabled = false;

bool dimUp = true;
unsigned long settingsModified = 0;
unsigned long blinkToggled = 0;
int blinkBrightness = 0;
int blinkPeriod = 0;
int adcResolution = 0;


// operating modes
enum Mode
{
  MODE_DEFAULT,
  MODE_BLINK_SLOW,
  MODE_BLINK_FAST,
  MODE_BLINK_END
};

void setMode(Mode mode)
{
  switch (mode)
  {
    case MODE_DEFAULT:
      blinkToggleCount = 0;
      break;

    case MODE_BLINK_FAST:
      blinkToggleCount = 4;
      blinkPeriod = BLINK_PERIOD_FAST;
      blinkToggled = 0;
      break;

    case MODE_BLINK_SLOW:
      blinkToggleCount = 6;
      blinkPeriod = BLINK_PERIOD_SLOW;
      blinkToggled = 0;
      break;

    case MODE_BLINK_END:
      blinkToggleCount = -1;
      break;
  }
}

/**
 * calculate CRC16 (for EEPROM backup)
 */
template<typename T> uint16_t calculateCRC16(T& t)
{
  uint16_t crc16 = 0;

  uint8_t* ptr = (uint8_t*)&t;
  for (int count = sizeof(T); count; --count)
  {
    crc16 = _crc16_update(crc16, *ptr++);
  }

  return crc16;
}

/**
 * get NTC temperature
 *
 * @return temperature [°C] or 999.0f on error
 */
int getNtcTemperature()
{
  // sample input 3 times
  size_t count = 3;
  unsigned long sum = 0;
  for (size_t i=0; i<count; i++)
  {
    sum += analogRead(PIN_NTC);
    delay(1);
  }

  // calculate temperature
  float temperature = 999.0f; // invalid
  if (sum > count*adcResolution/3)
  {
    float relation = (float)adcResolution*count/(float)sum;
    float resistance = (relation - 1.0f)*DIVIDER_RESISTANCE;

    // Steinhart-Hart equation
    temperature = 1.0f/(log(resistance/NTC_RESISTANCE)/NTC_B_COEFFICIENT + 1.0f/(NTC_TEMP_NOM + KELVIN_CELSIUS)) - KELVIN_CELSIUS;
  }

  /** extra debug
  printDebug("sum/adc/rel/res/temp:");
  printDebug(sum);
  printDebug("/");
  printDebug(adcResolution);
  printDebug("/");
  printDebug(relation);
  printDebug("/");
  printDebug(resistance);
  printDebug("/");
  printlnDebug(temperature);
  */

  return round(temperature);
}

/**
 * ISR for proximity digital input
 *
 * toggle power on short pulse
 */
void proximity()
{
  noInterrupts();

  near = digitalRead(PIN_PROXIMITY);
  unsigned int now = millis();
  if (near)
  {
    // proximity start
    approach = now;
    initalApproach = true;
    if (outputEnabled && blinkToggleCount < 0)
    {
      setMode(MODE_DEFAULT);
    }
  }
  else
  {
    unsigned int duration = now - approach;
    if (initalApproach && duration >= DURATION_MIN && duration <= DURATION_POWER_MAX)
    {
      // short proximity pulse: toggle power
      outputEnabled = !outputEnabled;
      if (outputEnabled)
      {
        setMode(MODE_DEFAULT);
      }
    }
  }

  interrupts();
}

/**
 * Arduino setup
 */
void setup()
{
  // @todo change I/O clock to 4 MHz (8 MHz / 2) currently disabled: messes with millis()
  //noInterrupts();
  //CLKPR = _BV(CLKPCE);
  //CLKPR = _BV(CLKPS0);
  //interrupts();

  // init serial debug
  #ifdef DEBUG
    Serial.begin(115200);
    delay(4000);
    printlnDebug("init ...");
  #endif

  // configure proximity sensor input
  pinMode(PIN_PROXIMITY, INPUT);
  noInterrupts();
  attachInterrupt(digitalPinToInterrupt(PIN_PROXIMITY), proximity, CHANGE);
  interrupts();

  // configure LED brightness control outputs (PWM via timer 1 on pins D9 and D10)
  TCCR1A = 0;
  TCCR1B = 0;
  TCNT1 = 0;
  ICR1 = F_CPU/40000 - 1;  // 40 kHz (so that you can't hear the LEDs)
  OCR1A = ICR1; // 100 % (off, inverted)
  OCR1B = ICR1; // 100 % (off, inverted)
  TCCR1A = _BV(COM1A1) | _BV(COM1B1) | _BV(WGM11); // clear output on match
  TCCR1B = _BV(WGM13) | _BV(WGM12) | _BV(CS10);    // fast PWM with TOP=ICR1 (mode 14)

  // configure ADC
  analogReference(DEFAULT); // ATmega328: via AVCC pin
  adcResolution = 1023;

  // restore settings from EEPROM
  Settings restoredSettings;
  EEPROM.get(EEPROM_ADDRESS, restoredSettings);
  if (restoredSettings.magic == EEPROM_MAGIC)
  {
    // magic match
    uint16_t eepromCRC = restoredSettings.crc;
    restoredSettings.crc = 0;
    uint16_t expectedCRC = calculateCRC16(restoredSettings);
    if (eepromCRC == expectedCRC)
    {
      // CRC match, restore
      settings = restoredSettings;
      printlnDebug("settings restored from EEPROM");
    }
  }

  // turn output on at power up, prefer dim up
  outputEnabled = true;
  dimUp = settings.brightness == BRIGHTNESS_MIN? false : true;
  if (settings.brightness == BRIGHTNESS_MIN || settings.brightness == BRIGHTNESS_MAX)
  {
    setMode(MODE_BLINK_END);
  }
  else
  {
    setMode(MODE_DEFAULT);
  }
  initalApproach = false;

  // enable watchdog
  wdt_enable(WDTO_2S);

  printlnDebug("init done");
}

/**
 * Arduino main loop
 */
void loop()
{
  #ifdef DEBUG
    delay(300);
  #endif

  /* extra debug
  printDebug("near:");
  printDebug(near);
  printDebug(" initial:");
  printDebug(initalApproach);
   */

  // feed watchdog
  wdt_reset();

  delay(10);

  // get board temperature
  int ntcTemperature = getNtcTemperature();
  printDebug("temp:");
  printlnDebug(ntcTemperature);

  // check proximity to dim brightness and to toggle dim direction
  unsigned int now = millis();
  if (outputEnabled && near)
  {
    unsigned int duration = now - approach;
    if (duration >= DURATION_DIM_MIN)
    {
      if (initalApproach)
      {
        // inital proximity, change dimmer direction
        dimUp = !dimUp;
        initalApproach = false;
      }
      else
      {
        // dimmer step size selection
        if (blinkToggleCount < 0 && blinkPeriod == BLINK_PERIOD_FAST
            && ((!dimUp && settings.brightness == BRIGHTNESS_MIN) || (dimUp && settings.brightness == BRIGHTNESS_MAX)))
        {
          // continued proxymity after blinking at top/bottom
          if (duration >= DURATION_TOGGLE_STEP)
          {
            // toggle step size and signal mode change (3 slow blinks)
            settings.dimBigSteps = !settings.dimBigSteps;
            settingsModified = now;
            setMode(MODE_BLINK_SLOW);
          }
        }
      }

      // change brightness
      bool changeBrightness = blinkToggleCount == 0;
      if (changeBrightness)
      {
        // init dim behaviour: fast small steps
        int brightnessStep = BRIGHTNESS_STEP_SMALL;
        int dimDelay = DURATION_DIM_STEP_SMALL;
        if (settings.dimBigSteps)
        {
          // use slow big step
          brightnessStep = BRIGHTNESS_STEP_BIG;
          dimDelay = DURATION_DIM_STEP_BIG;
        }
        if (dimUp)
        {
          // dim up
          if (settings.brightness < BRIGHTNESS_MAX)
          {
            settings.brightness += brightnessStep;
            settingsModified = now;

            if (settings.brightness >= BRIGHTNESS_MAX)
            {
              // new at limit, signal
              setMode(MODE_BLINK_FAST);
            }
          }
          else
          {
            // limit value
            settings.brightness = BRIGHTNESS_MAX;
          }
        }
        else
        {
          // dim down
          if (settings.brightness > BRIGHTNESS_MIN)
          {
            settings.brightness -= brightnessStep;
            settingsModified = now;

            if (settings.brightness <= BRIGHTNESS_MIN)
            {
              // new at limit, signal
              setMode(MODE_BLINK_FAST);
            }
          }
          else
          {
            // limit value
            settings.brightness = BRIGHTNESS_MIN;
          }
        }

        // modify approach start time for periodic repeat
        approach += dimDelay;
        initalApproach = false;
      }
    }
  }

  // control output (on, off, brightness)
  int dutyCycle = 0;
  if (outputEnabled)
  {
    if (blinkToggleCount > 0)
    {
      // blink
      int period = blinkPeriod;
      if (!blinkToggled)
      {
        // start of 1st blink period with LED fully off or on
        blinkToggled = now;
        blinkBrightness = settings.brightness <= BRIGHTNESS_MIN? 0 : 100;
      }
      else if ((now - blinkToggled) >= period)
      {
        // end of a blink period, toggle blink brightness
        blinkToggled = now;
        if (blinkToggleCount%2 == 0)
        {
          // restore brightness
          blinkBrightness = settings.brightness;
        }
        else
        {
          // LED fully off or on
          blinkBrightness = settings.brightness <= BRIGHTNESS_MIN? 0 : 100;
        }
        --blinkToggleCount;
        if (blinkToggleCount == 0)
        {
          // block further brightness changes during current proximity
          setMode(MODE_BLINK_END);
          approach = now;
          initalApproach = false;
        }
      }
      dutyCycle = blinkBrightness;
    }

    if (blinkToggleCount <= 0)
    {
      // not blinking
      if (ntcTemperature < NTC_TEMPERATURE_MAX)
      {
        // regular operation
        dutyCycle = settings.brightness;
      }
      else
      {
        // over temperature, reduce brightness to min value
        dutyCycle = BRIGHTNESS_MIN;
      }
    }

    // update duty cylce of PWM timer 1
    #ifndef DEBUG
      sbi(DDRB, DDB1); // enable OC1A output
    #endif
    sbi(DDRB, DDB2); // enable OC1B output
    OCR1A = (100 - dutyCycle)*ICR1/100;
    OCR1B = OCR1A;
  }
  else
  {
    // disable outputs
    cbi(DDRB, DDB1); // disable OC1A output
    cbi(DDRB, DDB2); // disable OC1B output
  }

  /* extra debug
  printDebug(" up:");
  printDebug(dimUp);
  printDebug(" step:");
  printDebug(settings.dimBigSteps);
  printDebug(" bright:");
  printDebug(settings.brightness);
  printDebug(" blink:");
  printDebug(blinkToggleCount);
  printDebug(" duty:");
  printDebug(dutyCycle);
  printlnDebug();
  */

  // backup settings
  if (settingsModified && (now - settingsModified) >= BACKUP_DELAY)
  {
    // backup delay reached, save settings
    settings.crc = 0;
    settings.crc = calculateCRC16(settings);
    EEPROM.put(EEPROM_ADDRESS, settings);
    settingsModified = 0;
    printlnDebug("settings saved to EEPROM");
  }
}
