#include "Display_ST7789.h"
#include "LVGL_Driver.h"
#include "ui.h"
#include <Adafruit_NeoPixel.h>
#include <math.h>

// Pinovi
#define ENCODER_PIN_A  2   // CLK
#define ENCODER_PIN_B  1   // DT
#define ENCODER_BUTTON 0   // SW
#define NEOPIXEL_PIN   8   // NeoPixel LED
#define THERMISTOR_PIN 3   // Termistor na GPIO 3 (ADC)
#define STRING_PIN_1   9   // Laser string 1
#define STRING_PIN_2   18  // Laser string 2
#define STRING_PIN_3   19  // Laser string 3
#define STRING_PIN_4   20  // Laser string 4
#define FAN_PIN        13  // PWM za ventilatore
#define BUZZER_PIN     12  // Piezo zvučnik na GPIO 12
#define TRIGGER_PIN    5   // Trigger pin (aktivan na LOW)

// NeoPixel postavke
#define NUMPIXELS      1
Adafruit_NeoPixel strip(NUMPIXELS, NEOPIXEL_PIN, NEO_GRB + NEO_KHZ800);

// Termistor postavke
#define THERMISTOR_NOMINAL 10000  // Otpor termistora na 25°C (10kΩ)
#define TEMPERATURE_NOMINAL 25    // Temperatura za nominalni otpor (25°C)
#define BETA_COEFFICIENT 3950     // Beta koeficijent termistora
#define SERIES_RESISTOR 10000     // Otpornik u naponskom djelitelju (10kΩ)

// PWM postavke
#define PWM_FREQ       10000
#define PWM_RESOLUTION 8

// Zvučne frekvencije za modove
#define ECO_FREQ       500   // Niska frekvencija za Eco
#define NORMAL_FREQ    1000  // Srednja frekvencija za Normal
#define SPORT_FREQ     1500  // Viša frekvencija za Sport
#define ULTRA_FREQ     2000  // Najviša frekvencija za Ultra
#define WARNING_FREQ   3000  // Frekvencija za upozorenje

// Globalne varijable
int encoder_pos = 0;
int current_digit = 0;
volatile int last_a = HIGH;
volatile int last_b = HIGH;
bool button_pressed = false;
char serial_code[7];
unsigned long last_encoder_time = 0;
int selected_mode = -1;
int active_mode = -1;
bool on_main_screen = false;
float temperature = 0.0;
int fan_pwm = 0;
unsigned long on_time_start = 0;
unsigned long last_on_time_ms = 0; // Pohrana posljednjeg vremena
bool laser_on = false;
bool safety_mode = false;
bool led_blinking = false;
unsigned long last_blink_time = 0;
bool blink_state = false;
bool modeLocked = false;
bool demo_running = false;
bool last_trigger_state = HIGH; // Praćenje prethodnog stanja trigger pina

extern objects_t objects;

void IRAM_ATTR encoderISR();

void setup() {
  Serial.begin(115200);
  while (!Serial) delay(10);
  Serial.println("System initialized. Enter 6-digit code via Serial (e.g., 511988).");

  LCD_Init();
  Lvgl_Init();
  ui_init();
  Set_Backlight(12);

  pinMode(ENCODER_PIN_A, INPUT_PULLUP);
  pinMode(ENCODER_PIN_B, INPUT_PULLUP);
  pinMode(ENCODER_BUTTON, INPUT_PULLUP);
  pinMode(TRIGGER_PIN, INPUT_PULLUP); // PULLUP jer je aktivan na LOW

  attachInterrupt(digitalPinToInterrupt(ENCODER_PIN_A), encoderISR, CHANGE);

  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);

  strip.begin();
  strip.setBrightness(250);
  strip.setPixelColor(0, strip.Color(0, 0, 0));
  strip.show();

  analogWriteFrequency(STRING_PIN_1, PWM_FREQ);
  analogWriteFrequency(STRING_PIN_2, PWM_FREQ);
  analogWriteFrequency(STRING_PIN_3, PWM_FREQ);
  analogWriteFrequency(STRING_PIN_4, PWM_FREQ);
  analogWriteFrequency(FAN_PIN, PWM_FREQ);

  analogWriteResolution(STRING_PIN_1, PWM_RESOLUTION);
  analogWriteResolution(STRING_PIN_2, PWM_RESOLUTION);
  analogWriteResolution(STRING_PIN_3, PWM_RESOLUTION);
  analogWriteResolution(STRING_PIN_4, PWM_RESOLUTION);
  analogWriteResolution(FAN_PIN, PWM_RESOLUTION);

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
}

void IRAM_ATTR encoderISR() {
  static unsigned long last_interrupt_time = 0;
  unsigned long current_time = micros();
  const int debounce_delay = 1300; // 1 ms debounce interval

  if (current_time - last_interrupt_time > debounce_delay) {
    int a = digitalRead(ENCODER_PIN_A);
    int b = digitalRead(ENCODER_PIN_B);

    // Detektiraj samo prelazak A iz LOW u HIGH (jedna tranzicija po kliku)
    if (last_a == LOW && a == HIGH) {
      if (b == HIGH) {
        encoder_pos--; // Smjer lijevo
      } else if (b == LOW) {
        encoder_pos++; // Smjer desno
      }

      // Ograničenje vrijednosti ovisno o ekranu
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

void playSound(int frequency, int duration) {
  tone(BUZZER_PIN, frequency, duration);
  delay(duration);
  noTone(BUZZER_PIN);
}

void updateUI() {
  if (!on_main_screen) {
    int arc_value = encoder_pos * 36;
    lv_arc_set_value(objects.arc_encoder, arc_value);
    char num_str[2];
    sprintf(num_str, "%d", encoder_pos);
    lv_label_set_text(objects.selected_number, num_str);
  } else {
    updateMainScreenUI();
  }
}

void updateMainScreenUI() {
  int mode_mapping[] = {2, 4, 1, 0, 3};
  selected_mode = mode_mapping[encoder_pos];

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

  if (active_mode != -1 && !led_blinking) {
    switch (active_mode) {
      case 0: strip.setPixelColor(0, strip.Color(255, 0, 0)); break; // Eco - Crvena
      case 1: strip.setPixelColor(0, strip.Color(124, 250, 124)); break; // Normal - Svijetlo zelena
      case 2: strip.setPixelColor(0, strip.Color(0, 255, 0)); break; // Ultra - Zelena
      case 3: strip.setPixelColor(0, strip.Color(255, 255, 0)); break; // Demo - Žuta
      case 4: strip.setPixelColor(0, strip.Color(0, 0, 255)); break; // Sport - Plava
    }
    strip.show();
  }

  updateTemperatureAndFan();
}

void updateTemperatureAndFan() {
  int adc_value = analogRead(THERMISTOR_PIN);
  float voltage = (adc_value / 4095.0) * 3.3;
  float resistance = SERIES_RESISTOR * (3.3 / voltage - 1);
  float steinhart;
  steinhart = resistance / THERMISTOR_NOMINAL;
  steinhart = log(steinhart);
  steinhart /= BETA_COEFFICIENT;
  steinhart += 1.0 / (TEMPERATURE_NOMINAL + 273.15);
  steinhart = 1.0 / steinhart;
  temperature = steinhart - 273.15;

  if (temperature < -50 || temperature > 150) {
    Serial.println("Temperature out of range!");
    temperature = 0.0;
  }

  int minFanSpeed = 0;
  int fanSpeed = 0;

  // Definiraj minimalne obrtaje prema modu
  switch (active_mode) {
    case 0: minFanSpeed = 30; break; // Eco
    case 1: minFanSpeed = 45; break; // Normal
    case 2: minFanSpeed = 70; break; // Ultra
    case 3: minFanSpeed = 30; break; // Demo (minimalno dok čeka trigger)
    case 4: minFanSpeed = 55; break; // Sport
    default: minFanSpeed = 0; break;
  }

  // Ako je trigger LOW (aktivan) i mod nije Demo, prilagodi brzinu prema temperaturi
  if (digitalRead(TRIGGER_PIN) == LOW && active_mode != 3 && !demo_running) {
    if (temperature <= 35) {
      fanSpeed = 70;
    } else if (temperature >= 50) {
      fanSpeed = 100;
    } else {
      fanSpeed = 70 + (temperature - 35) * (100 - 70) / (50 - 35);
    }
  } else {
    fanSpeed = minFanSpeed; // Minimalni obrtaji kada laser nije aktivan
  }

  // Sigurnosni mehanizmi
  if (temperature >= 60) {
    active_mode = 0; // Prebaci u Eco mod
    modeLocked = true;
    fanSpeed = 100;
    strip.setPixelColor(0, strip.Color(255, 0, 0)); // Crvena za upozorenje
    strip.show();
    tone(BUZZER_PIN, WARNING_FREQ);
  } else if (temperature >= 55) {
    if (!led_blinking) {
      led_blinking = true;
      last_blink_time = millis();
    }
    tone(BUZZER_PIN, WARNING_FREQ);
  } else if (temperature < 50 && modeLocked) {
    modeLocked = false;
    noTone(BUZZER_PIN);
    led_blinking = false;
    strip.setPixelColor(0, strip.Color(0, 0, 0));
    strip.show();
  }

  if (led_blinking) {
    unsigned long current_time = millis();
    if (current_time - last_blink_time >= 500) {
      blink_state = !blink_state;
      strip.setPixelColor(0, blink_state ? strip.Color(0, 255, 0) : strip.Color(0, 0, 0));
      strip.show();
      last_blink_time = current_time;
    }
  }

  fan_pwm = map(fanSpeed, 0, 100, 0, 255);
  analogWrite(FAN_PIN, fan_pwm);

  lv_arc_set_value(objects.arc_temp, (int)temperature);
  lv_arc_set_value(objects.arc_fan, fanSpeed); // Uvijek prikazuje trenutnu brzinu ventilatora

  if (temperature < 30) {
    lv_obj_set_style_arc_color(objects.arc_temp, lv_color_hex(0x00FF00), LV_PART_INDICATOR | LV_STATE_DEFAULT);
  } else if (temperature >= 30 && temperature <= 50) {
    lv_obj_set_style_arc_color(objects.arc_temp, lv_color_hex(0xFFFF00), LV_PART_INDICATOR | LV_STATE_DEFAULT);
  } else {
    lv_obj_set_style_arc_color(objects.arc_temp, lv_color_hex(0xFF0000), LV_PART_INDICATOR | LV_STATE_DEFAULT);
  }

  char temp_str[6];
  sprintf(temp_str, "%.1fC", temperature);
  lv_label_set_text(objects.temp_label, temp_str);

  char fan_str[4];
  sprintf(fan_str, "%d%%", fanSpeed);
  lv_label_set_text(objects.fan_label, fan_str);

  if (laser_on) {
    last_on_time_ms = millis() - on_time_start;
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
  if (digitalRead(ENCODER_BUTTON) == LOW && !button_pressed) {
    button_pressed = true;
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
        if (current_digit == 6) {
          checkPassword();
        }
      }
    } else {
      selectMode();
    }
  } else if (digitalRead(ENCODER_BUTTON) == HIGH) {
    button_pressed = false;
  }
}

void checkPassword() {
  const char* entered_code[6];
  entered_code[0] = lv_label_get_text(objects.digit1);
  entered_code[1] = lv_label_get_text(objects.digit2);
  entered_code[2] = lv_label_get_text(objects.digit3);
  entered_code[3] = lv_label_get_text(objects.digit4);
  entered_code[4] = lv_label_get_text(objects.digit5);
  entered_code[5] = lv_label_get_text(objects.digit6);

  if (strcmp(entered_code[0], "1") == 0 &&
      strcmp(entered_code[1], "2") == 0 &&
      strcmp(entered_code[2], "2") == 0 &&
      strcmp(entered_code[3], "3") == 0 &&
      strcmp(entered_code[4], "3") == 0 &&
      strcmp(entered_code[5], "3") == 0) {
    lv_label_set_text(objects.status_message, "Laser Ready");
    strip.setPixelColor(0, strip.Color(255, 0, 0));
    strip.show();
    delay(2000);
    strip.setPixelColor(0, strip.Color(0, 0, 0));
    strip.show();
    lv_scr_load(objects.main);
    on_main_screen = true;
    encoder_pos = 0;
    selected_mode = -1;
    active_mode = -1;
    led_blinking = false;
  } else {
    lv_label_set_text(objects.status_message, "0"); // "Access Denied" zamijenjen s "0"
    strip.setPixelColor(0, strip.Color(0, 255, 0));
    strip.show();
    delay(2000);
    lv_label_set_text(objects.status_message, "Enter Code");
    resetDigits();
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
    Serial.readBytes(serial_code, 6);
    serial_code[6] = '\0';
    Serial.print("Entered code: ");
    Serial.println(serial_code);
    if (strcmp(serial_code, "122333") == 0) {
      lv_label_set_text(objects.status_message, "Laser Ready");
      strip.setPixelColor(0, strip.Color(255, 0, 0));
      strip.show();
      delay(2000);
      strip.setPixelColor(0, strip.Color(0, 0, 0));
      strip.show();
      lv_scr_load(objects.main);
      on_main_screen = true;
      encoder_pos = 0;
      selected_mode = -1;
      active_mode = -1;
      led_blinking = false;
    } else {
      lv_label_set_text(objects.status_message, "0"); // "Access Denied" zamijenjen s "0"
      strip.setPixelColor(0, strip.Color(0, 255, 0));
      strip.show();
      delay(2000);
      lv_label_set_text(objects.status_message, "Enter Code");
      resetDigits();
    }
    while (Serial.available()) Serial.read();
  }
}

void setLaserPower(int percentage) {
  int pwm_value = (percentage * 255) / 100;
  analogWrite(STRING_PIN_1, pwm_value);
  analogWrite(STRING_PIN_2, pwm_value);
  analogWrite(STRING_PIN_3, pwm_value);
  analogWrite(STRING_PIN_4, pwm_value);
}

void setSingleStringPower(int string_pin, int percentage) {
  int pwm_value = (percentage * 255) / 100;
  analogWrite(string_pin, pwm_value);
}

void runDemoMode() {
  demo_running = true;
  for (int i = 0; i < 3; i++) {
    playSound(WARNING_FREQ, 200);
    strip.setPixelColor(0, strip.Color(0, 255, 0)); // Zelena
    strip.show();
    delay(200);
    strip.setPixelColor(0, strip.Color(255, 255, 0)); // Žuta
    strip.show();
    delay(200);
  }

  setLaserPower(0);
  setSingleStringPower(STRING_PIN_1, 15); // 0-30ms 15%
  delay(30);
  setSingleStringPower(STRING_PIN_1, 0); // 30-50ms 0%
  delay(20);
  setSingleStringPower(STRING_PIN_1, 25); // 50-80ms 25%
  delay(30);
  setSingleStringPower(STRING_PIN_1, 0); // 80-100ms 0%
  delay(20);
  for (int i = 25; i <= 50; i++) { // 100-200ms 25-50%
    setSingleStringPower(STRING_PIN_1, i);
    delay(2);
  }
  setSingleStringPower(STRING_PIN_2, 50); // 200-250ms drugi string na 50%
  delay(50);
  setLaserPower(0); // 250-280ms 0%
  delay(30);
  setSingleStringPower(STRING_PIN_1, 50); // 280-350ms tri stringa na 50%
  setSingleStringPower(STRING_PIN_2, 50);
  setSingleStringPower(STRING_PIN_3, 50);
  delay(70);
  setLaserPower(50); // 350-500ms sva 4 stringa na 50%
  delay(150);
  setLaserPower(0);
  demo_running = false;
  selected_mode = 2; // Postavi Ultra mod
  active_mode = 2;
  encoder_pos = 0; // Postavi poziciju enkodera na Ultra mod
  selectMode(); // Ažuriraj mod
  updateMainScreenUI(); // Ažuriraj UI za Ultra mod
}

void selectMode() {
  if (modeLocked) return;

  active_mode = selected_mode;
  switch (active_mode) {
    case 0: // Eco
      Serial.println("Eco mode selected");
      strip.setPixelColor(0, strip.Color(255, 0, 0));
      strip.show();
      playSound(ECO_FREQ, 200);
      break;
    case 1: // Normal
      Serial.println("Normal mode selected");
      strip.setPixelColor(0, strip.Color(124, 250, 124));
      strip.show();
      playSound(NORMAL_FREQ, 200);
      break;
    case 2: // Ultra
      Serial.println("Ultra mode selected");
      strip.setPixelColor(0, strip.Color(0, 255, 0));
      strip.show();
      playSound(ULTRA_FREQ, 200);
      break;
    case 3: // Demo
      Serial.println("Demo mode selected");
      strip.setPixelColor(0, strip.Color(255, 255, 0));
      strip.show();
      demo_running = false; // Resetiraj demo status
      break;
    case 4: // Sport
      Serial.println("Sport mode selected");
      strip.setPixelColor(0, strip.Color(0, 0, 255));
      strip.show();
      playSound(SPORT_FREQ, 200);
      break;
  }
}

int getLaserPowerForMode(int mode) {
  switch (mode) {
    case 0: return 40; // Eco
    case 1: return 60; // Normal
    case 2: return 100; // Ultra
    case 4: return 80; // Sport
    default: return 0;
  }
}

void loop() {
  Timer_Loop();
  updateUI();
  handleButton();
  handleSerialInput();

  bool current_trigger_state = digitalRead(TRIGGER_PIN);

  if (active_mode == 3 && current_trigger_state == LOW && !demo_running) {
    runDemoMode();
  } else if (current_trigger_state == LOW && !modeLocked && active_mode != 3) {
    if (!laser_on && last_trigger_state == HIGH) { // Aktivacija samo na prelasku s HIGH na LOW
      laser_on = true;
      on_time_start = millis();
      last_on_time_ms = 0; // Resetiraj posljednje vrijeme pri novoj aktivaciji
    }
    setLaserPower(getLaserPowerForMode(active_mode));
  } else {
    if (laser_on) {
      last_on_time_ms = millis() - on_time_start; // Pohrani vrijeme kada se laser isključi
      laser_on = false;
    }
    setLaserPower(0);
  }

  last_trigger_state = current_trigger_state; // Ažuriraj prethodno stanje triggera

  delay(5);
}