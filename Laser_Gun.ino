#include "Display_ST7789.h"
#include "LVGL_Driver.h"
#include "ui.h"
#include <Adafruit_NeoPixel.h>
#include <math.h>

#define ENCODER_PIN_A 2
#define ENCODER_PIN_B 1
#define ENCODER_BUTTON 0
#define NEOPIXEL_PIN 8
#define THERMISTOR_PIN 4
#define STRING_PIN_1 9
#define FAN_PIN 18
#define TRIGGER_PIN 20
#define BATTERY_PIN 5

#define NUMPIXELS 1
Adafruit_NeoPixel strip(NUMPIXELS, NEOPIXEL_PIN, NEO_GRB + NEO_KHZ800);

#define THERMISTOR_NOMINAL 8400  // Otpor pri 25°C
#define TEMPERATURE_NOMINAL 22
#define BETA_COEFFICIENT 1250     // Prilagođeno
#define SERIES_RESISTOR 10000     // 10 kΩ otpornik
#define TEMP_OFFSET 0.0           // Bez offseta za sada

#define LASER_PWM_FREQ 10000
#define FAN_PWM_FREQ 25000
#define PWM_RESOLUTION 8

volatile int encoder_pos = 0;
int current_digit = 0;
volatile int last_a = HIGH;
volatile int last_b = HIGH;
bool button_pressed = false;
char serial_code[7];
int selected_mode = -1;
int active_mode = -1;
bool on_main_screen = false;
float temperature = 0.0;
int fan_pwm = 0;
unsigned long on_time_start = 0;
unsigned long last_on_time_ms = 0;
bool laser_on = false;
bool safety_mode = false;
bool led_blinking = false;
unsigned long last_blink_time = 0;
bool blink_state = false;
bool mode_locked = false;
bool rapid_running = false;
bool last_trigger_state = HIGH;

extern objects_t objects;

void IRAM_ATTR encoderISR();
void updateUI();
void updateMainScreenUI();
void updateTemperatureAndFan();
void handleButton();
void checkPassword();
void resetDigits();
void handleSerialInput();
void setLaserPower(int percentage);
void runRapidFireMode();
void selectMode();
int getLaserPowerForMode(int mode);
float readBattery();

void setup() {
  Serial.begin(115200);
  while (!Serial) delay(10);
  printf("System initialized. Enter 6-digit code via Serial (e.g., 511988).\n");

  LCD_Init();
  Lvgl_Init();
  ui_init();
  Set_Backlight(12);

  pinMode(ENCODER_PIN_A, INPUT_PULLUP);
  pinMode(ENCODER_PIN_B, INPUT_PULLUP);
  pinMode(ENCODER_BUTTON, INPUT_PULLUP);
  pinMode(TRIGGER_PIN, INPUT_PULLUP);
  pinMode(BATTERY_PIN, INPUT);
  pinMode(THERMISTOR_PIN, INPUT);

  attachInterrupt(digitalPinToInterrupt(ENCODER_PIN_A), encoderISR, CHANGE);

  strip.begin();
  strip.setBrightness(250);
  strip.setPixelColor(0, strip.Color(0, 0, 0));
  strip.show();

  analogWriteFrequency(STRING_PIN_1, LASER_PWM_FREQ);
  analogWriteFrequency(FAN_PIN, FAN_PWM_FREQ);
  analogWriteResolution(STRING_PIN_1, PWM_RESOLUTION);
  analogWriteResolution(FAN_PIN, PWM_RESOLUTION);
  analogWrite(FAN_PIN, 0);
  analogWrite(STRING_PIN_1, 0);

  lv_arc_set_range(objects.arc_encoder, 0, 359);
  lv_arc_set_value(objects.arc_encoder, 0);
  lv_label_set_text(objects.selected_number, "0");
  lv_label_set_text(objects.status_message, "Enter Code");
  lv_label_set_text(objects.digit1, "-");
  lv_label_set_text(objects.digit2, "-");
  lv_label_set_text(objects.digit3, "-");
  lv_label_set_text(objects.digit4, "-");
  lv_label_set_text(objects.digit5, "-");
  lv_label_set_text(objects.digit6, "-");
  lv_arc_set_range(objects.arc_temp, 0, 100);
  lv_arc_set_range(objects.arc_fan, 0, 100);
  lv_bar_set_range(objects.bat_status, 0, 100);

  printf("Setup complete, free heap: %u bytes\n", ESP.getFreeHeap());
}

void IRAM_ATTR encoderISR() {
  static unsigned long last_interrupt_time = 0;
  unsigned long current_time = micros();
  const int debounce_delay = 5000;

  if (current_time - last_interrupt_time > debounce_delay) {
    int a = digitalRead(ENCODER_PIN_A);
    int b = digitalRead(ENCODER_PIN_B);

    if (last_a == LOW && a == HIGH) {
      if (b == HIGH) encoder_pos--;
      else if (b == LOW) encoder_pos++;

      if (on_main_screen) {
        if (encoder_pos > 4) encoder_pos = 0;
        if (encoder_pos < 0) encoder_pos = 4;
      } else {
        if (encoder_pos > 9) encoder_pos = 0;
        if (encoder_pos < 0) encoder_pos = 9;
      }
    }
    last_a = a;
    last_b = b;
    last_interrupt_time = current_time;
  }
}

void updateUI() {
  static int last_encoder_pos = -1;
  if (encoder_pos == last_encoder_pos) return;
  last_encoder_pos = encoder_pos;

  if (!on_main_screen) {
    lv_arc_set_value(objects.arc_encoder, encoder_pos * 36);
    char num_str[2];
    sprintf(num_str, "%d", encoder_pos);
    lv_label_set_text(objects.selected_number, num_str);
  } else {
    updateMainScreenUI();
  }
}

void updateMainScreenUI() {
  static int last_selected_mode = -1;
  int mode_mapping[] = {2, 4, 1, 0, 3};
  selected_mode = mode_mapping[encoder_pos];

  if (selected_mode == last_selected_mode) return;
  last_selected_mode = selected_mode;

  lv_obj_clear_state(objects.eco, LV_STATE_FOCUSED);
  lv_obj_clear_state(objects.normal, LV_STATE_FOCUSED);
  lv_obj_clear_state(objects.ultra, LV_STATE_FOCUSED);
  lv_obj_clear_state(objects.demo, LV_STATE_FOCUSED);
  lv_obj_clear_state(objects.sport, LV_STATE_FOCUSED);

  switch (selected_mode) {
    case 0: lv_obj_add_state(objects.eco, LV_STATE_FOCUSED); break;
    case 1: lv_obj_add_state(objects.normal, LV_STATE_FOCUSED); break;
    case 2: lv_obj_add_state(objects.ultra, LV_STATE_FOCUSED); break;
    case 3: lv_obj_add_state(objects.demo, LV_STATE_FOCUSED); break;
    case 4: lv_obj_add_state(objects.sport, LV_STATE_FOCUSED); break;
  }
}

void updateTemperatureAndFan() {
  static unsigned long last_temp_update = 0;
  static unsigned long last_pwm_update = 0;
  const unsigned long temp_update_interval = 200;
  const unsigned long pwm_update_interval = 500;
  static float temp_sum = 0.0;
  static int sample_count = 0;
  const int num_samples = 10;

  unsigned long current_time = millis();

  if (current_time - last_temp_update >= temp_update_interval) {
    int adc_value = analogRead(THERMISTOR_PIN);
    float voltage = (adc_value / 4095.0) * 3.3;
    float resistance = SERIES_RESISTOR * (3.3 - voltage) / voltage;
    float steinhart = resistance / THERMISTOR_NOMINAL;
    steinhart = log(steinhart) / BETA_COEFFICIENT + 1.0 / (TEMPERATURE_NOMINAL + 273.15);
    steinhart = 1.0 / steinhart;
    float temp = steinhart - 273.15;



    temp_sum += temp;
    sample_count++;

    if (sample_count >= num_samples) {
      temperature = (temp_sum / num_samples) + TEMP_OFFSET;
      temp_sum = 0.0;
      sample_count = 0;

      if (temperature < -100 || temperature > 200) {
        printf("Temperature out of range: %.1fC\n", temperature);
      }

      lv_arc_set_value(objects.arc_temp, (int)temperature);
      char temp_str[6];
      sprintf(temp_str, "%dC", (int)temperature);
      lv_label_set_text(objects.temp_label, temp_str);
    }
    last_temp_update = current_time;
  }

  if (current_time - last_pwm_update >= pwm_update_interval) {
    int minFanSpeed = 0;
    int fanSpeed = 0;

    switch (active_mode) {
      case 0: minFanSpeed = 30; break;
      case 1: minFanSpeed = 45; break;
      case 2: minFanSpeed = 70; break;
      case 3: minFanSpeed = 70; break;
      case 4: minFanSpeed = 55; break;
      default: minFanSpeed = 0; break;
    }

    if (digitalRead(TRIGGER_PIN) == LOW && active_mode != 3) {
      if (temperature <= 35) fanSpeed = 70;
      else if (temperature >= 50) fanSpeed = 100;
      else fanSpeed = 70 + (temperature - 35) * (100 - 70) / (50 - 35);
    } else {
      fanSpeed = minFanSpeed;
    }

    if (temperature >= 60) {
      active_mode = 0;
      mode_locked = true;
      fanSpeed = 100;
      led_blinking = true;
      strip.setPixelColor(0, strip.Color(255, 0, 0));
    } else if (temperature < 50 && mode_locked) {
      mode_locked = false;
      led_blinking = false;
      strip.setPixelColor(0, strip.Color(0, 0, 0));
    }

    if (mode_locked) fanSpeed = 100;

    if (led_blinking && !mode_locked && current_time - last_blink_time >= 500) {
      blink_state = !blink_state;
      strip.setPixelColor(0, blink_state ? strip.Color(255, 0, 0) : strip.Color(0, 0, 0));
      last_blink_time = current_time;
    }

    fan_pwm = map(fanSpeed, 0, 100, 0, 255);
    analogWrite(FAN_PIN, fan_pwm);

    lv_arc_set_value(objects.arc_fan, fanSpeed);
    char fan_str[4];
    sprintf(fan_str, "%d%%", fanSpeed);
    lv_label_set_text(objects.fan_label, fan_str);

    strip.show();
    last_pwm_update = current_time;
  }

  if (laser_on) {
    last_on_time_ms = current_time - on_time_start;
    int seconds = last_on_time_ms / 1000;
    int hundredths = (last_on_time_ms % 1000) / 10;
    char time_str[10];
    sprintf(time_str, "On: %02d,%02ds", seconds, hundredths);
    lv_label_set_text(objects.on_time, time_str);
  } else if (last_on_time_ms > 0) {
    int seconds = last_on_time_ms / 1000;
    int hundredths = (last_on_time_ms % 1000) / 10;
    char time_str[10];
    sprintf(time_str, "On: %02d,%02ds", seconds, hundredths);
    lv_label_set_text(objects.on_time, time_str);
  } else {
    lv_label_set_text(objects.on_time, "On: 00,00s");
  }
}

void handleButton() {
  static unsigned long last_button_time = 0;
  const unsigned long button_debounce_delay = 200;

  unsigned long current_time = millis();
  bool button_state = digitalRead(ENCODER_BUTTON) == LOW;

  if (button_state && !button_pressed && (current_time - last_button_time > button_debounce_delay)) {
    button_pressed = true;
    last_button_time = current_time;

    if (!on_main_screen) {
      if (current_digit < 6) {
        char num_str[2];
        sprintf(num_str, "%d", encoder_pos);
        switch (current_digit) {
          case 0: lv_label_set_text(objects.digit1, num_str); break;
          case 1: lv_label_set_text(objects.digit2, num_str); break;
          case 2: lv_label_set_text(objects.digit3, num_str); break;
          case 3: lv_label_set_text(objects.digit4, num_str); break;
          case 4: lv_label_set_text(objects.digit5, num_str); break;
          case 5: lv_label_set_text(objects.digit6, num_str); break;
        }
        current_digit++;
        if (current_digit == 6) checkPassword();
      }
    } else {
      selectMode();
      updateMainScreenUI();
    }
  } else if (!button_state && button_pressed && (current_time - last_button_time > button_debounce_delay)) {
    button_pressed = false;
    last_button_time = current_time;
  }
}

void checkPassword() {
  const char* entered_code[6] = {
    lv_label_get_text(objects.digit1), lv_label_get_text(objects.digit2),
    lv_label_get_text(objects.digit3), lv_label_get_text(objects.digit4),
    lv_label_get_text(objects.digit5), lv_label_get_text(objects.digit6)
  };

  if (strcmp(entered_code[0], "5") == 0 && strcmp(entered_code[1], "1") == 0 &&
      strcmp(entered_code[2], "1") == 0 && strcmp(entered_code[3], "9") == 0 &&
      strcmp(entered_code[4], "8") == 0 && strcmp(entered_code[5], "8") == 0) {
    lv_label_set_text(objects.status_message, "Laser Ready");
    strip.setPixelColor(0, strip.Color(255, 0, 0));
    strip.show();
    unsigned long start_time = millis();
    while (millis() - start_time < 2000) {
      Timer_Loop();
    }
    strip.setPixelColor(0, strip.Color(0, 0, 0));
    strip.show();
    lv_scr_load(objects.main);
    lv_task_handler();
    on_main_screen = true;
    encoder_pos = 0;
    selected_mode = -1;
    active_mode = -1;
    led_blinking = false;
    printf("Password accepted, switched to main screen\n");
  } else {
    lv_label_set_text(objects.status_message, "Wrong Code");
    strip.setPixelColor(0, strip.Color(0, 255, 0));
    strip.show();
    unsigned long start_time = millis();
    while (millis() - start_time < 2000) {
      Timer_Loop();
    }
    lv_label_set_text(objects.status_message, "Enter Code");
    resetDigits();
    printf("Incorrect password entered\n");
  }
}

void resetDigits() {
  lv_label_set_text(objects.digit1, "-");
  lv_label_set_text(objects.digit2, "-");
  lv_label_set_text(objects.digit3, "-");
  lv_label_set_text(objects.digit4, "-");
  lv_label_set_text(objects.digit5, "-");
  lv_label_set_text(objects.digit6, "-");
  current_digit = 0;
  strip.setPixelColor(0, strip.Color(0, 0, 0));
  strip.show();
}

void handleSerialInput() {
  if (Serial.available() >= 6) {
    String input = Serial.readStringUntil('\n');
    if (input.length() >= 6) {
      strncpy(serial_code, input.c_str(), 6);
      serial_code[6] = '\0';
      if (strcmp(serial_code, "511988") == 0) {
        lv_label_set_text(objects.status_message, "Laser Ready");
        strip.setPixelColor(0, strip.Color(255, 0, 0));
        strip.show();
        unsigned long start_time = millis();
        while (millis() - start_time < 2000) {
          Timer_Loop();
        }
        strip.setPixelColor(0, strip.Color(0, 0, 0));
        strip.show();
        lv_scr_load(objects.main);
        lv_task_handler();
        on_main_screen = true;
        encoder_pos = 0;
        selected_mode = -1;
        active_mode = -1;
        led_blinking = false;
        printf("Serial password accepted, switched to main screen\n");
      } else {
        lv_label_set_text(objects.status_message, "Wrong Code");
        strip.setPixelColor(0, strip.Color(0, 255, 0));
        strip.show();
        unsigned long start_time = millis();
        while (millis() - start_time < 2000) {
          Timer_Loop();
        }
        lv_label_set_text(objects.status_message, "Enter Code");
        resetDigits();
        printf("Incorrect serial password entered\n");
      }
    }
    while (Serial.available()) Serial.read();
  }
}

void setLaserPower(int percentage) {
  int pwm_value = (percentage * 255) / 100;
  analogWrite(STRING_PIN_1, pwm_value);
}

void runRapidFireMode() {
  rapid_running = true;
  setLaserPower(100);
  unsigned long start_time = millis();
  while (millis() - start_time < 100) {
    Timer_Loop();
  }
  setLaserPower(0);
  start_time = millis();
  while (millis() - start_time < 50) {
    Timer_Loop();
  }
  rapid_running = false;
}

void selectMode() {
  if (mode_locked) return;

  active_mode = selected_mode;
  switch (active_mode) {
    case 0: strip.setPixelColor(0, strip.Color(255, 0, 0)); break;
    case 1: strip.setPixelColor(0, strip.Color(124, 250, 124)); break;
    case 2: strip.setPixelColor(0, strip.Color(0, 255, 0)); break;
    case 3: strip.setPixelColor(0, strip.Color(255, 255, 0)); break;
    case 4: strip.setPixelColor(0, strip.Color(0, 0, 255)); break;
  }
  strip.show();
  printf("Mode %d selected\n", active_mode);
}

int getLaserPowerForMode(int mode) {
  switch (mode) {
    case 0: return 40;  // Eco
    case 1: return 60;  // Normal
    case 2: return 100; // Ultra
    case 4: return 80;  // Sport
    default: return 0;
  }
}

float readBattery() {
  int adc_value = analogRead(BATTERY_PIN);
  float measured_voltage = (adc_value / 4095.0) * 3.3;
  float actual_voltage = measured_voltage * ((56.0 + 4.7) / 4.7); // Voltage divider scaling
  return actual_voltage;
}

void loop() {
  Timer_Loop();

  static unsigned long last_status_update = 0;
  const unsigned long status_interval = 5000;

  static int last_encoder_pos = -1;
  if (encoder_pos != last_encoder_pos) {
    last_encoder_pos = encoder_pos;
    updateUI();
  }
  handleButton();
  handleSerialInput();
  updateTemperatureAndFan();

  unsigned long current_time = millis();
  if (current_time - last_status_update >= status_interval) {
    float battery_voltage = readBattery();
    int battery_percent = constrain(map(battery_voltage, 24, 34, 0, 100), 0, 100); // 36V-41.2V range
    lv_bar_set_value(objects.bat_status, battery_percent, LV_ANIM_OFF);
    char bat_str[6];
    sprintf(bat_str, "%d%%", battery_percent);
    lv_label_set_text(objects.bat_percent, bat_str);

    int fanSpeed = map(fan_pwm, 0, 255, 0, 100);
    printf("Battery: %.2fV, Temp: %.1fC, Fan: %d%%\n", battery_voltage, temperature, fanSpeed);
    last_status_update = current_time;
  }

  bool current_trigger_state = digitalRead(TRIGGER_PIN);
  if (active_mode == 3 && current_trigger_state == LOW && !rapid_running) {
    runRapidFireMode();
  } else if (current_trigger_state == LOW && !mode_locked && active_mode != 3) {
    if (!laser_on && last_trigger_state == HIGH) {
      laser_on = true;
      on_time_start = current_time;
      last_on_time_ms = 0;
    }
    setLaserPower(getLaserPowerForMode(active_mode));
  } else if (laser_on) {
    last_on_time_ms = current_time - on_time_start;
    laser_on = false;
    setLaserPower(0);
  }

  last_trigger_state = current_trigger_state;
}