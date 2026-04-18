#include <FastLED.h>
#include "esp_wifi.h"
#include "esp_bt.h"

// =========================================================================
// Defines: Pins
// =========================================================================

#define SWITCH_IN_PIN GPIO_NUM_2

#define BATTERY_READ_EN_PIN GPIO_NUM_23
#define BATTERY_READ_IN_PIN GPIO_NUM_0  // A0
#define LED_DATA_PIN GPIO_NUM_19
#define BUZZER_OUT_PIN GPIO_NUM_18
#define LED_POWER_EN_PIN GPIO_NUM_22


// =========================================================================
// Defines: Constants
// =========================================================================

#define RING_COUNT 9

#define ANIM_INIT_START_DELAY_MILLIS 200
#define ANIM_INIT_END_DELAY_MILLIS 30
#define ANIM_INIT_STEP_DELAY_FRAC 0.75
#define ANIM_FIRE_DELAY_MILLIS 30
#define BTN_DEBOUNCE_MILLIS 100
#define BTN_LONG_PRESS_MILLIS 2000
#define BATTERY_SAMPLE_MILLIS 5000

// Settle time for ADC to accurately read battery
// settle_t = (divider_resistance * adc_capacitance) * 9 = (50kOhm * 20pF) * 9 = 9usec
// (+1 for good measure)
#define BATTERY_SETTLE_MICROS 10

#define SOUND_FREQ_START_HZ 3000
#define SOUND_FREQ_DEC_HZ 200

#define LED_TYPE WS2812B
#define LED_COLOR_ORDER GRB
// (10 per ring) * (9 rings) + 1 rear + 2 front
#define LED_COUNT 93
#define LED_BRIGHTNESS 10

#define LEDS_PER_RING 10
#define LEDS_ALL_RINGS_COUNT (LEDS_PER_RING * RING_COUNT)

#define LED_IDX_POWER 0
#define LED_IDX_RINGS 1
#define LED_IDX_MUZZLE 91

// sample_val = (((voltage / 2) / 3.3) * 4095)
// Min voltage: 3.2
// Max voltage: 4.2
#define BATTERY_SAMPLE_VAL_MIN 1985
#define BATTERY_SAMPLE_VAL_MAX 2606

// Define bitmask for RTC setup for deep sleep
#define RTC_SLEEP_EN_PIN_BITMASK (1ULL << SWITCH_IN_PIN)


// =========================================================================
// Variables
// =========================================================================

unsigned long buttonDebounceStartTime;
uint8_t switchIsDebouncing = 0;

unsigned long buttonDepressedStartTime;
uint8_t switchIsDepressed = 0;

unsigned long batterySampleLastTime;

CRGB leds[LED_COUNT];

CRGB ringColor = 0x0000ff;
CRGB muzzleFlashColor = 0xffffff;

uint8_t batteryPercent;


// =========================================================================
// Utilities
// =========================================================================

uint8_t detectSwitch(uint8_t desiredState) {
  if (digitalRead(SWITCH_IN_PIN) == desiredState) {
    if (switchIsDebouncing) {
      if ((millis() - buttonDebounceStartTime) > BTN_DEBOUNCE_MILLIS) {
        switchIsDebouncing = 0;
        return 1;
      }
    } else {
      buttonDebounceStartTime = millis();
      switchIsDebouncing = 1;
    }
  }
  return 0;
}

void readBatteryLevel () {
  digitalWrite(BATTERY_READ_EN_PIN, HIGH);
  delayMicroseconds(BATTERY_SETTLE_MICROS);
  
  int batteryReading = analogRead(BATTERY_READ_IN_PIN);
  batteryPercent = map(batteryReading, BATTERY_SAMPLE_VAL_MIN, BATTERY_SAMPLE_VAL_MAX, 0, 100);
  
  digitalWrite(BATTERY_READ_EN_PIN, LOW);
}

void startSleep () {
  // Setup deep sleep wakeup source
  esp_sleep_enable_ext1_wakeup(RTC_SLEEP_EN_PIN_BITMASK, ESP_EXT1_WAKEUP_ANY_LOW);
  
  
  // Enter deep sleep
  esp_deep_sleep_start();
}

void writeRingColor (int index, CRGB color) {
  int startIdx = LED_IDX_RINGS + (index * LEDS_PER_RING);
  fill_solid(leds + startIdx, LEDS_PER_RING, color);
}

void fillRings (CRGB color) {
  fill_solid(leds, LEDS_ALL_RINGS_COUNT, color);
}

void writeMuzzleColor (CRGB color) {
  fill_solid(leds + LED_IDX_MUZZLE, 2, color);
}

void displayBatteryLevel () {
  // Map battery percent to hue from red to green (0-120);
  uint8_t hue = map(batteryPercent, 0, 100, 0, 120);
  leds[LED_IDX_POWER] = CHSV(hue, 255, 255);
  FastLED.show();
}

void updateBatteryLevel () {
  readBatteryLevel();
  displayBatteryLevel();
}

void enableLedStrip () {
  // Remove hold for deep sleep (if set)
  gpio_hold_dis((gpio_num_t)LED_POWER_EN_PIN);
  digitalWrite(LED_POWER_EN_PIN, HIGH);
}

void disableLedStrip () {
  // Drive data pin low first to prevent ghosting
  digitalWrite(LED_DATA_PIN, LOW);
  
  digitalWrite(LED_POWER_EN_PIN, LOW);
  // Set disable to hold while asleep
  gpio_hold_en((gpio_num_t)LED_POWER_EN_PIN);
}

// =========================================================================
// Animation Sequences
// =========================================================================

// Run blaster firing sequence
void fireBlaster () {
  int fireTone = SOUND_FREQ_START_HZ;
  for (int i = 0; i < RING_COUNT; i++) {
    tone(BUZZER_OUT_PIN, fireTone);
    // Disable targer ring
    writeRingColor(i, 0);
    // Fill in behind
    if (i > 0) {
      writeRingColor(i - 1, ringColor);
    }
    delay(ANIM_FIRE_DELAY_MILLIS);
  }
  writeRingColor(RING_COUNT-1, ringColor);

  writeMuzzleColor(muzzleFlashColor);
  delay(ANIM_FIRE_DELAY_MILLIS);
  writeMuzzleColor(0);
}

// Blaster powering up sequence
void powerUp () {
  FastLED.clear();
  FastLED.show();
  // Read and show battery first
  updateBatteryLevel();

  int frameDelay = ANIM_INIT_START_DELAY_MILLIS;
  while (frameDelay > ANIM_INIT_END_DELAY_MILLIS) {
    for (int i = 0; i < RING_COUNT; i++) {
      // Turn on target ring
      writeRingColor(i, ringColor);
      // Clear last one
      if (i > 0) {
        writeRingColor(i-1, ringColor);
      } else {
        writeRingColor(RING_COUNT-1, ringColor);
      }
      FastLED.show();
    }
    frameDelay = frameDelay * ANIM_INIT_STEP_DELAY_FRAC;
  }

  fillRings(ringColor);
}

// Blaster powering down sequence
void powerDown () {
  FastLED.clear();
  FastLED.show();
  
  disableLedStrip();
  startSleep();
}


// =========================================================================
// Event Routines
// =========================================================================

// Pollable routine handling switch input for blaster firing and deep sleep
void handleSwitch () {
  if (switchIsDepressed) {
    // If switch still depressed, wait for "turn-off" delay
    if (digitalRead(SWITCH_IN_PIN) == LOW) {
      if ((millis() - buttonDepressedStartTime) > BTN_LONG_PRESS_MILLIS) {
        // Long press detected, power down
        powerDown();
      }
    } else {
      // Short press detected, fire blaster
      fireBlaster();
      switchIsDepressed = 0;
    }
  } else {
    if (detectSwitch(LOW)) {
      buttonDepressedStartTime = millis();
      switchIsDepressed = 1;
    }
  }
}

// Pollable routine handling regular battery level reading
void handleBattery () {
  if ((millis() - batterySampleLastTime) > BATTERY_SAMPLE_MILLIS) {
    batterySampleLastTime = millis();
    updateBatteryLevel();
    Serial.print("Battery Level: ");
    Serial.print(batteryPercent);
    Serial.println("%");
  }
}


// =========================================================================
// Main
// =========================================================================

void setup() {
  esp_wifi_stop();   // Power down wifi
  esp_wifi_deinit(); // Release wifi memory

  // Disable BLE/Zigbee
  esp_bt_controller_disable();
  esp_bt_controller_deinit();
  
  // External pullup used so deep sleep remains stable
  pinMode(SWITCH_IN_PIN, INPUT);
  
  pinMode(BATTERY_READ_EN_PIN, OUTPUT);
  pinMode(LED_DATA_PIN, OUTPUT);
  pinMode(BUZZER_OUT_PIN, OUTPUT);
  pinMode(LED_POWER_EN_PIN, OUTPUT);

  digitalWrite(BATTERY_READ_EN_PIN, LOW);
  
  enableLedStrip();
  // Short delay allowing leds to start up
  delay(2);

  FastLED.addLeds<LED_TYPE,LED_DATA_PIN,LED_COLOR_ORDER>(leds, LED_COUNT)
    .setCorrection(TypicalLEDStrip)
    .setDither(LED_BRIGHTNESS < 255);
  FastLED.setBrightness(LED_BRIGHTNESS);


  Serial.begin(115200);
  Serial.println("Ready! (=^-^=)");

  powerUp();
}

void loop() {
  handleSwitch();
  handleBattery();
}

