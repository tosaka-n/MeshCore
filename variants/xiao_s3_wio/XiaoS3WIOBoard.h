#pragma once

#include <Arduino.h>
#include <helpers/ESP32Board.h>

class XiaoS3WIOBoard : public ESP32Board {
public:
private:
  uint8_t user_btn_wakeup_level = 0;

  void detectUserButtonWakeupPolarity() {
#ifdef PIN_USER_BTN
    pinMode(PIN_USER_BTN, INPUT_PULLUP);
    delay(5);
    user_btn_wakeup_level = (digitalRead(PIN_USER_BTN) == LOW) ? 1 : 0;
#endif
  }

public:
  XiaoS3WIOBoard() { }
  void begin() {
    ESP32Board::begin();
    detectUserButtonWakeupPolarity();
  }
  void powerOff() override;

  const char* getManufacturerName() const override {
    return "Xiao S3 WIO";
  }
};
