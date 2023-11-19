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
 *
 * - provides most features of the original:
 *   - light on on power on
 *   - fade on/off
 *   - proximity < 600 ms: on/off
 *   - proximity > 800 ms: dim up/down
 *   - dim limit reached: 2 short blinks
 *   - dim hold > 5 s: toggle dim mode continuous/3 steps (3 longs blinks)
 *   - limit brightness to minimum when board temperature exceeds 85 °C
 *     (5 short blinks) and resume normal operation after 5°C cool down
 *
 * - additional features:
 *   - hold > 15 s at power up: toggle proximity sensor lock (3 longs blinks)
 *   - reduced standby and power on consumption
 *
 * ****************************************************************************
 *
 * Oligo Grace Board:
 *
 * - MCU: ATmega328P - 5V
 * - standby power consumption per pendant: 0.6 W (24 V, 25 mA, original firmware)
 * - power on current per pendant: ~0.8 A
 * - power on consumption per pendant: 1.5 W ... 15.5 W
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
 * Components required to upload firmware:
 *
 * - Molex Pico-EZmate connector to attach ISP to Oligo board, e.g. part no. 369200606
 * - ATmega ISP, e.g. Arduino as ISP incl. USB/TTL adapter
 *
 *
 * Portability
 *
 * The code of this project is specifically tailored for the ATmega328P MCU.
 * Using an Arduino PWM library and an Arduino timer library would have been possible
 * but this would not have made the code significantly more portable because
 * the available libraries are also hardware dependent.
 *
 *****************************************************************************
 *
 * possible additional features:
 * - fade off at power off
 * - change setting over air (WiFi/Bluetooth)
 * - sync setting of 2 pendants
 * - separate setting for top and bottom panel
 *
 *****************************************************************************/

#define VERSION "1.0.4.1" // 18.11.2023

// Arduino includes, LGPL license
#include <EEPROM.h>
#include <HardwareSerial.h>
#include <wiring_private.h>

// AVR includes, modified BSD license
#include <avr/io.h>
#include <avr/power.h>
#include <avr/sfr_defs.h>
#include <avr/sleep.h>
#include <avr/wdt.h>
#include <util/crc16.h>

//#define TEST  // @todo: comment in for testing without NTC
//#define DEBUG // @todo: comment in if serial debugging is needed, will also disable bottom panel

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

// main loop
#define LOOP_PERIOD 10 // [ms]

// proximity gesture timing
#define DURATION_MIN           100 // [ms] min. duration for proximity
#define DURATION_POWER_MAX     600 // [ms] max. duration for short (power) proximity
#define DURATION_DIM_MIN       800 // [ms] min. duration for long (dim) proximity
#define DURATION_TOGGLE_STEP  5000 // [ms] min. extended duration for dim mode change
#define DURATION_LOCK        15000 // [ms] min. startup duration for proximity lock toggle
#define DURATION_DIM_INC        40 // [ms] duration per continuous dim increment
#define DURATION_DIM_STEP    DURATION_DIM_MIN // [ms] duration per dim step

// brightness parameters
#define BRIGHTNESS_MIN      4 // [%]
#define BRIGHTNESS_STEP_1  16 // [%]
#define BRIGHTNESS_STEP_2  32 // [%]
#define BRIGHTNESS_MAX    100 // [%]
#ifdef DEBUG
  #define BRIGHTNESS_INC    5 // [%]
#else
  #define BRIGHTNESS_INC    1 // [%]
#endif

// timer parameters
#define PWM_FREQUENCY  32000 // [Hz] select frequency so that you can't hear the LEDs (8/16 MHz: 245 Hz ... 40 kHz)
#define FADE_DURATION      2 // [s]  number of seconds to change timer 1 from 0 % to 100 % duty cycle

// blink timing
#define BLINK_PERIOD_FAST 100 // [ms]
#define BLINK_PERIOD_SLOW 500 // [ms]

// board temperature parameters
#define NTC_TEMP_MAX             85 // [°C]
#define NTC_TEMP_HYSTERESIS       5 // [°C]
#define NTC_TEMP_INVALID     999.0f // [°C]
#define DIVIDER_RESISTANCE    47000 // [Ohm]
#define NTC_RESISTANCE     47000.0f // [Ohm]
#define NTC_TEMP_NOM          25.0f // [°C]
#define NTC_B_COEFFICIENT   4131.0f // estimated NTC B coefficient for 25-100°C
#define KELVIN_CELSIUS      273.15f // [°C]

// operating modes
enum OperationMode
{
  MODE_STANDBY,
  MODE_DEFAULT,
  MODE_BLINK_SETTINGS,
  MODE_BLINK_LIMIT,
  MODE_BLINK_WARNING,
  MODE_BLINK_END
};

// brightness level in big step mode
enum BrightnessLevel
{
  LEVEL_0 = 0,
  LEVEL_1 = BRIGHTNESS_MIN,
  LEVEL_2 = BRIGHTNESS_STEP_1,
  LEVEL_3 = BRIGHTNESS_STEP_2,
  LEVEL_4 = BRIGHTNESS_MAX
};

// persistent settings
struct Settings
{
  unsigned int magic = EEPROM_MAGIC;
  unsigned int brightness = BRIGHTNESS_STEP_2; // [%]
  bool dimSteps = true;
  volatile bool proximitySensorLocked = false;
  uint16_t crc = 0;
} settings;

// ISR variables
volatile bool near = false;
volatile bool initalApproach = false;
volatile bool outputEnabled = false;
volatile bool proximityAtStartup = false;
volatile int fadeOCR = -1;
volatile unsigned long approach = 0;
volatile OperationMode operationMode = MODE_DEFAULT;
volatile int blinkToggleCount = 0;
volatile unsigned int blinkPeriod = 0;
volatile unsigned long blinkToggled = 0;

bool dimUp = true;
bool overTemperature = true;
unsigned int blinkBrightness = 0;
unsigned int adcResolution = 0;
unsigned long settingsModified = 0;
unsigned long delayUntil = 0;


/**
 * set operation mode (regular, blinking)
 */
void setOperationMode(OperationMode mode)
{
  operationMode = mode;
  switch (mode)
  {
    case MODE_STANDBY:
      blinkToggleCount = 0;
      fadeOCR = 0;
      break;

    case MODE_DEFAULT:
      blinkToggleCount = 0;
      break;

    case MODE_BLINK_LIMIT:
      blinkToggleCount = 4;
      blinkPeriod = BLINK_PERIOD_FAST;
      blinkToggled = 0;
      break;

    case MODE_BLINK_SETTINGS:
      blinkToggleCount = 6;
      blinkPeriod = BLINK_PERIOD_SLOW;
      blinkToggled = 0;
      break;

    case MODE_BLINK_WARNING:
      blinkToggleCount = 10;
      blinkPeriod = BLINK_PERIOD_FAST;
      blinkToggled = 0;
      break;

    case MODE_BLINK_END:
      blinkToggleCount = -1;
      break;
  }
}

/**
 * @return next higher brightness level
 */
BrightnessLevel operator++(BrightnessLevel& level)
{
  switch (level)
  {
    case LEVEL_0:
      return LEVEL_1;
    case LEVEL_1:
      return LEVEL_2;
    case LEVEL_2:
      return LEVEL_3;
    default:
      return LEVEL_4;
  }
}

/**
 * @return next lower brightness level
 */
BrightnessLevel operator--(BrightnessLevel& level)
{
  switch (level)
  {
    case LEVEL_4:
      return LEVEL_3;
    case LEVEL_3:
      return LEVEL_2;
    case LEVEL_2:
      return LEVEL_1;
    default:
      return LEVEL_0;
  }
}

/**
 * @return brightness level of settings.brightness (floor)
 */
BrightnessLevel getBrightnessLevel()
{
  if (settings.brightness <= LEVEL_0)
    return LEVEL_0;
  else if (settings.brightness <= LEVEL_1)
    return LEVEL_1;
  else if (settings.brightness <= LEVEL_2)
    return LEVEL_2;
  else if (settings.brightness <= LEVEL_3)
    return LEVEL_3;
  else
    return LEVEL_4;
}

/**
 * @return CRC16 (for EEPROM backup)
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
 * @param brightness [%]
 * @return duty cycle value for timer 1 OCR register
 */
inline uint16_t brightnessToOCR(int brightness)
{
  return brightness>=0? brightness<=100? brightness*ICR1/100 : 100 : 0;
}

/**
 * get NTC temperature
 *
 * @return temperature [°C] or NTC_TEMP_INVALID on error
 */
int getNtcTemperature()
{
  // sample input 3 times
  size_t count = 3; // ~ 19 µs (1 ADC conversion takes less than 6.25 µs at 8 MHz)
  unsigned long sum = 0;
  for (size_t i=0; i<count; i++)
  {
    sum += analogRead(PIN_NTC);
    delayMicroseconds(20);
  }

  // calculate temperature
  float temperature = NTC_TEMP_INVALID;
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
  near = digitalRead(PIN_PROXIMITY);

  if (!settings.proximitySensorLocked)
  {
    unsigned int now = millis();
    if (near)
    {
      // proximity start
      approach = now;
      initalApproach = true;
      proximityAtStartup = false;
      if (outputEnabled && operationMode == MODE_BLINK_END)
      {
        // was blinking, set default mode
        setOperationMode(MODE_DEFAULT);
      }
    }
    else
    {
      // proximity end
      unsigned int proximityDuration = now - approach;
      if (initalApproach && proximityDuration >= DURATION_MIN && proximityDuration <= DURATION_POWER_MAX)
      {
        // short proximity pulse: toggle power
        outputEnabled = !outputEnabled;
        if (outputEnabled)
        {
          // was off, set default mode, fade on
          setOperationMode(MODE_DEFAULT);
          fadeOCR = brightnessToOCR(settings.brightness);
        }
        else
        {
          // was on, fade off
          setOperationMode(MODE_STANDBY);
        }
      }
    }
  }
}

/**
 * ISR for fader timer 2
 */
ISR(TIMER2_COMPA_vect)
{
  if (fadeOCR >= 0)
  {
    if (OCR1A < (uint16_t)fadeOCR)
    {
      // increase duty cycle to max.
      ++OCR1A;
      OCR1B = OCR1A;
    }
    else if (OCR1A > (uint16_t)fadeOCR)
    {
      // decrease duty cycle to zero
      --OCR1A;
      OCR1B = OCR1A;
    }

    if (OCR1A == (uint16_t)fadeOCR)
    {
      // fading completed
      fadeOCR = -1;
    }
  }
}

/**
 * Arduino setup
 */
void setup()
{
  // maybe change I/O clock to 4 MHz (8 MHz / 2), but it will also reduce PWM duty cycle resolution
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

  // permanent power saving
  noInterrupts();
#ifndef DEBUG
  power_usart0_disable();
#endif
  power_spi_disable();
  power_twi_disable();
  interrupts();

  // configure proximity sensor input
  pinMode(PIN_PROXIMITY, INPUT);
  noInterrupts();
  attachInterrupt(digitalPinToInterrupt(PIN_PROXIMITY), proximity, CHANGE);
  interrupts();

  // configure LED brightness control outputs (PWM via timer 1 on pins D9 and D10)
  TCCR1A = 0;
  TCCR1B = 0;
  TCNT1 = 0;
  ICR1 = F_CPU/PWM_FREQUENCY; // timer ticks per PWM period (0..65535)
  OCR1A = 0;
  OCR1B = 0;
  TCCR1A = _BV(COM1A1) | _BV(COM1A0) |_BV(COM1B1) |_BV(COM1B0) | _BV(WGM11); // set output on match (inverted)
  TCCR1B = _BV(WGM13) | _BV(WGM12) | _BV(CS10); // fast PWM with TOP=ICR1 (mode 14), clock not prescaled

  // configure LED brightness fader (via timer 2)
  TCCR2A = 0;
  TCCR2B = 0;
  TCNT2 = 0;
  OCR2A = F_CPU/1024L*FADE_DURATION/ICR1; // timer ticks for changing timer 1 to 100 % duty cycle within fade duration
  TCCR2A = _BV(WGM21); // CTC with TOP=OCR2A (mode 2)
  TCCR2B = _BV(CS22) | _BV(CS21) | _BV(CS20); // CPU clock prescaled by 1024 -> 7813 Hz
  sbi(TIMSK2, OCIE2A); // enable timer 2 interrupt

  // configure ADC
  analogReference(DEFAULT); // ATmega328: via AVCC pin
  adcResolution = 1023;

  // restore settings from EEPROM
  Settings restoredSettings;
  EEPROM.get(EEPROM_ADDRESS, restoredSettings);
  if (restoredSettings.magic == EEPROM_MAGIC)
  {
    // magic number matches
    uint16_t eepromCRC = restoredSettings.crc;
    restoredSettings.crc = 0;
    uint16_t expectedCRC = calculateCRC16(restoredSettings);
    if (eepromCRC == expectedCRC)
    {
      // CRC matches, restore settings
      settings = restoredSettings;
      printlnDebug("settings restored from EEPROM");

      // check settings
      if (settings.brightness < BRIGHTNESS_MIN)
      {
        settings.brightness = BRIGHTNESS_MIN;
      }
      else if (settings.brightness > BRIGHTNESS_MAX)
      {
        settings.brightness = BRIGHTNESS_MAX;
      }
    }
  }

  // turn output on at power up
  outputEnabled = true;
  fadeOCR = brightnessToOCR(settings.brightness);
  dimUp = settings.brightness == BRIGHTNESS_MIN? false : true; // prefer initial dim up, inverted setting required
  setOperationMode(MODE_DEFAULT);
  initalApproach = false;

  // get initial proximity sensor state
  near = digitalRead(PIN_PROXIMITY);
  if (near)
  {
    approach = millis();
    proximityAtStartup = true;
  }

  // enable watchdog
  wdt_enable(WDTO_2S);

  printlnDebug("init done");
}

/**
 * Arduino main loop (~10 ms)
 */
void loop()
{
  #ifdef DEBUG
    delay(100);
  #endif

  // feed watchdog
  wdt_reset();

  // check proximity to dim brightness and to toggle dim direction
  int brightness = 0;
  unsigned long now = millis();
  unsigned int proximityDuration = now - approach;
  bool delaying = delayUntil && now < delayUntil && (delayUntil - now) <= LOOP_PERIOD;
  if (!delaying)
  {
    if (outputEnabled && near)
    {
      if (proximityDuration >= DURATION_DIM_MIN)
      {
        if (proximityAtStartup)
        {
          if (proximityDuration >= DURATION_LOCK)
          {
            // startup proximity, toggle proximity sensor lock
            settings.proximitySensorLocked = !settings.proximitySensorLocked;
            settingsModified = now;
            proximityAtStartup = false;
            setOperationMode(MODE_BLINK_SETTINGS);
          }
        }
        else if (initalApproach)
        {
          // inital proximity, change dimmer direction
          dimUp = !dimUp;
          initalApproach = false;
        }
        else
        {
          // dimmer step size selection
          if (operationMode == MODE_BLINK_END && blinkPeriod == BLINK_PERIOD_FAST
              && ((!dimUp && settings.brightness == BRIGHTNESS_MIN) || (dimUp && settings.brightness == BRIGHTNESS_MAX)))
          {
            // continued proximity after blinking at top/bottom
            if (proximityDuration >= DURATION_TOGGLE_STEP)
            {
              // toggle step size and signal mode change (3 slow blinks)
              settings.dimSteps = !settings.dimSteps;
              settingsModified = now;
              setOperationMode(MODE_BLINK_SETTINGS);
            }
          }
        }

        // change brightness
        bool changeBrightness = operationMode == MODE_DEFAULT;
        if (changeBrightness)
        {
          // init dim behaviour
          int dimDelay = settings.dimSteps? DURATION_DIM_STEP : DURATION_DIM_INC;
          BrightnessLevel brightnessLevel = getBrightnessLevel();
          if (dimUp)
          {
            // dim up
            if (settings.brightness < BRIGHTNESS_MAX)
            {
              if (settings.dimSteps)
              {
                settings.brightness = ++brightnessLevel;
              }
              else
              {
                settings.brightness = min(settings.brightness + BRIGHTNESS_INC, BRIGHTNESS_MAX);
              }
              settingsModified = now;

              if (settings.brightness >= BRIGHTNESS_MAX)
              {
                // new at limit, signal
                setOperationMode(MODE_BLINK_LIMIT);
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
              if (settings.dimSteps)
              {
                settings.brightness = --brightnessLevel;
              }
              else
              {
                settings.brightness = max(settings.brightness - BRIGHTNESS_INC, BRIGHTNESS_MIN);
              }
              settingsModified = now;

              if (settings.brightness <= BRIGHTNESS_MIN)
              {
                // new at limit, signal
                setOperationMode(MODE_BLINK_LIMIT);
              }
            }
            else
            {
              // limit value
              settings.brightness = BRIGHTNESS_MIN;
            }
          }
          printlnDebug(settings.brightness);

          // modify approach start time for periodic repeat
          approach += dimDelay;
          initalApproach = false;
        }
      }
    }

    // control output (on, off, brightness)
    if (outputEnabled)
    {
      if (blinkToggleCount > 0)
      {
        // blinking
        unsigned int period = blinkPeriod;
        if (!blinkToggled || (now - blinkToggled) >= period)
        {
          // toggle blink brightness
          --blinkToggleCount;
          blinkToggled = now;
          if (blinkToggleCount%2 == 0)
          {
            // end of blink: restore brightness
            blinkBrightness = settings.brightness;
          }
          else
          {
            // new blink: preferably with lowered brightness
            BrightnessLevel brightnessLevel = getBrightnessLevel();
            blinkBrightness = brightnessLevel > LEVEL_0? --brightnessLevel : ++brightnessLevel;
          }
          if (blinkToggleCount == 0)
          {
            // blinking completed, block further brightness changes during current proximity
            setOperationMode(MODE_BLINK_END);
            approach = now;
            initalApproach = false;
          }
        }
        brightness = blinkBrightness;
      }

      // check board temperature
      int ntcTemperature;
      power_adc_enable();
      #ifndef TEST
        ntcTemperature = getNtcTemperature();
        printDebug("temp:");
        printDebug(ntcTemperature);
        printDebug(" ");
      #else
        getNtcTemperature();
        ntcTemperature = 30;
      #endif
      if (!overTemperature && ntcTemperature >= NTC_TEMP_MAX)
      {
        overTemperature = true;
        setOperationMode(MODE_BLINK_WARNING);
      }
      else if (overTemperature && ntcTemperature <= NTC_TEMP_MAX - NTC_TEMP_HYSTERESIS)
      {
        overTemperature = false;
      }

      if (blinkToggleCount <= 0)
      {
        // not blinking
        if (overTemperature)
        {
          // over temperature, reduce brightness to min value
          brightness = BRIGHTNESS_MIN;
        }
        else
        {
          // regular operation
          brightness = settings.brightness;
        }
      }
    }

    if (brightness > 0)
    {
      // update duty cycle of PWM timer 1
      sbi(DDRB, DDB1); // enable OC1A output
      sbi(DDRB, DDB2); // enable OC1B output
      if (fadeOCR < 0)
      {
        OCR1A = brightnessToOCR(brightness);
        OCR1B = OCR1A;
      }
    }
    else if (fadeOCR < 0)
    {
      // disable outputs
      cbi(DDRB, DDB1); // disable OC1A output
      cbi(DDRB, DDB2); // disable OC1B output
      OCR1A = 0;
      OCR1B = OCR1A;
    }

    // debug
    printDebug("near:");
    printDebug(near);
    printDebug(" initial:");
    printDebug(initalApproach);
    printDebug(" up:");
    printDebug(dimUp);
    printDebug(" step:");
    printDebug(settings.dimSteps);
    printDebug(" bright:");
    printDebug(settings.brightness);
    printDebug(" blink:");
    printDebug(blinkToggleCount);
    printDebug(" duty:");
    printDebug(brightness);
    printDebug(" startup proximity:");
    printDebug(proximityAtStartup);
    printDebug(" on:");
    printDebug(outputEnabled);
    printDebug(" fade:");
    printDebug(fadeOCR);
    printlnDebug();

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

  // dynamic power saving
  if (delaying)
  {
    // delaying: stay idle until next interrupt (timer, proximity)
    set_sleep_mode(SLEEP_MODE_IDLE);
    sleep_mode();
  }
  else if (operationMode == MODE_STANDBY && fadeOCR < 0 && !(initalApproach && proximityDuration <= DURATION_POWER_MAX))
  {
    // LED is off, disable ADC and watchdog and power down until next interrupt (proximity)
    delayUntil = 0;
    set_sleep_mode(SLEEP_MODE_PWR_DOWN);
    wdt_disable();
    power_adc_disable();
    sleep_mode();
    wdt_enable(WDTO_2S);
  }
  else
  {
    // was active, start passive delay: stay idle until next interrupt (timer, proximity)
    delayUntil = now + LOOP_PERIOD;
    set_sleep_mode(SLEEP_MODE_IDLE);
    sleep_mode();
  }
}
