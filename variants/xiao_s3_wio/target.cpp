#include <Arduino.h>
#include "target.h"

XiaoS3WIOBoard board;

#if defined(P_LORA_SCLK)
  static SPIClass spi;
  RADIO_CLASS radio = new Module(P_LORA_NSS, P_LORA_DIO_1, P_LORA_RESET, P_LORA_BUSY, spi);
#else
  RADIO_CLASS radio = new Module(P_LORA_NSS, P_LORA_DIO_1, P_LORA_RESET, P_LORA_BUSY);
#endif

WRAPPER_CLASS radio_driver(radio, board);

ESP32RTCClock fallback_clock;
AutoDiscoverRTCClock rtc_clock(fallback_clock);
EnvironmentSensorManager sensors;

#ifdef ENABLE_USER_POWER_BUTTON
  #ifndef PIN_USER_BTN
    #error "PIN_USER_BTN must be defined when ENABLE_USER_POWER_BUTTON is enabled"
  #endif
  #ifndef POWER_BUTTON_ACTIVE_LEVEL
    #define POWER_BUTTON_ACTIVE_LEVEL LOW
  #endif
  #if POWER_BUTTON_ACTIVE_LEVEL != LOW && POWER_BUTTON_ACTIVE_LEVEL != HIGH
    #error "POWER_BUTTON_ACTIVE_LEVEL must be LOW or HIGH"
  #endif
#endif

#ifdef DISPLAY_CLASS
  DISPLAY_CLASS display;
#endif

#ifdef PIN_USER_BTN
  // For the headless power-button environment, the fourth argument enables
  // the pull resistor matching POWER_BUTTON_ACTIVE_LEVEL.
#ifdef ENABLE_USER_POWER_BUTTON
  MomentaryButton user_btn(PIN_USER_BTN, 1000,
                           POWER_BUTTON_ACTIVE_LEVEL == LOW, true);
#else
  MomentaryButton user_btn(PIN_USER_BTN, 1000, true);
#endif
#endif

bool radio_init() {
  fallback_clock.begin();
  rtc_clock.begin(Wire);
  pinMode(PIN_STATUS_LED, OUTPUT);

#ifdef ENABLE_USER_POWER_BUTTON
  user_btn.begin();
#endif

  #if defined(P_LORA_SCLK)
  spi.begin(P_LORA_SCLK, P_LORA_MISO, P_LORA_MOSI);
  return radio.std_init(&spi);
#else
  return radio.std_init();
#endif
}

mesh::LocalIdentity radio_new_identity() {
  RadioNoiseListener rng(radio);
  return mesh::LocalIdentity(&rng);  // create new random identity
}

#ifdef ENABLE_USER_POWER_BUTTON
// Override powerOff only for the dedicated headless power-button environment.
void XiaoS3WIOBoard::powerOff() {
  // Power off the display if any
#ifdef DISPLAY_CLASS
  display.turnOff();
#endif

  // Power off LoRa
  radio_driver.powerOff();

  // Keep LoRa inactive during deepsleep
  digitalWrite(P_LORA_NSS, HIGH);
#if defined(CONFIG_IDF_TARGET_ESP32C3) || defined(CONFIG_IDF_TARGET_ESP32C6)
  gpio_hold_en((gpio_num_t)P_LORA_NSS);
#else
  rtc_gpio_hold_en((gpio_num_t)P_LORA_NSS);
#endif

  // Power off GPS if any
  if (sensors.getLocationProvider() != NULL) {
    sensors.getLocationProvider()->stop();
  }

  // Flush serial buffers
  Serial.flush();
  delay(100);

  // Clear stale wakeup sources to avoid ghost wakeup
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);

  // Do not let the press that requested power-off wake the device immediately.
  while (user_btn.isPressed()) {
    delay(10);
  }

  // Configure GPIO wakeup using the explicitly configured button polarity.
  esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);
  rtc_gpio_set_direction((gpio_num_t)PIN_USER_BTN, RTC_GPIO_MODE_INPUT_ONLY);
#if POWER_BUTTON_ACTIVE_LEVEL == LOW
  rtc_gpio_pullup_en((gpio_num_t)PIN_USER_BTN);
  rtc_gpio_pulldown_dis((gpio_num_t)PIN_USER_BTN);
#else
  rtc_gpio_pulldown_en((gpio_num_t)PIN_USER_BTN);
  rtc_gpio_pullup_dis((gpio_num_t)PIN_USER_BTN);
#endif
  esp_sleep_enable_ext0_wakeup((gpio_num_t)PIN_USER_BTN, POWER_BUTTON_ACTIVE_LEVEL);

  // Finally set ESP32 into deepsleep
  esp_deep_sleep_start();
}

// Long press powers the headless device off. Wake from deep sleep is handled
// by the ext0 GPIO configuration above.
void handleUserPowerButtonEvent() {
  // A wake press can remain held while the application boots. Require its
  // release before accepting a new long press as a power-off request.
  static bool is_armed = false;
  if (!is_armed) {
    user_btn.check();
    if (user_btn.isPressed()) {
      return;
    }
    is_armed = true;
  }

  if (user_btn.check() == BUTTON_EVENT_LONG_PRESS) {
    board.powerOff();
  }
}
#endif
