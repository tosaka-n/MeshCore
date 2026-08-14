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

// Button is always available for deep sleep wakeup and display control
#ifdef PIN_USER_BTN
  MomentaryButton user_btn(PIN_USER_BTN, 1000, true);
#endif

#ifdef DISPLAY_CLASS
  DISPLAY_CLASS display;
#endif

bool radio_init() {
  fallback_clock.begin();
  rtc_clock.begin(Wire);
#ifdef PIN_USER_BTN
  pinMode(PIN_USER_BTN, INPUT);
#endif
  pinMode(PIN_STATUS_LED, OUTPUT);

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

// Override powerOff to add GPIO wakeup support
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

  // Configure GPIO wakeup for button recovery
#ifdef PIN_USER_BTN
  esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);
  rtc_gpio_set_direction((gpio_num_t)PIN_USER_BTN, RTC_GPIO_MODE_INPUT_ONLY);
  rtc_gpio_pullup_en((gpio_num_t)PIN_USER_BTN);

  // Use ext0 for the button so we can honor the detected active level and keep
  // LoRa packet wakeups on the usual ext1 path.
  esp_sleep_enable_ext0_wakeup((gpio_num_t)PIN_USER_BTN, user_btn_wakeup_level);
  esp_sleep_enable_ext1_wakeup((1ULL << P_LORA_DIO_1), ESP_EXT1_WAKEUP_ANY_HIGH);
#endif

  // Finally set ESP32 into deepsleep
  esp_deep_sleep_start();
}

// Handle user button events for headless board operation
// - Long press: enters deep sleep for power conservation
// - Button press during normal operation: wake is handled by ext0 wakeup hardware
// Called periodically from main loop; declared as weak to allow board-specific implementations
void handleUserButtonEvent() __attribute__((weak)) {
#ifdef PIN_USER_BTN
  // ← ブート後 3秒間は長押しを無視（復帰時のボタン長押し回避）
  static unsigned long boot_time = 0;
  if (boot_time == 0) {
    boot_time = millis();
  }
  
  uint8_t button_event = user_btn.check();
  
  // 長押し時間は **1000ms = 1秒**
  if (button_event == BUTTON_EVENT_LONG_PRESS && (millis() - boot_time) > 3000) {
    board.powerOff();
  }
#endif
}
