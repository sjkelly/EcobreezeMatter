#include <Matter.h>
#include <MatterFan.h>
#include <Wire.h>
#include <Adafruit_PWMServoDriver.h>

#define ESC_CHANNEL 0
// #define MIN_PULSE_WIDTH 1000 // Removed for solid voltage test
// #define MAX_PULSE_WIDTH 2000 // Removed for solid voltage test
#define SERVO_FREQ 50 // Analog servos and ESCs run at ~50 Hz

MatterFan matter_fan;
Adafruit_PWMServoDriver pwm = Adafruit_PWMServoDriver();

void setup()
{
  Serial.begin(115200);
  delay(2000); // Give time for Serial to connect
  Serial.println(" Booting EcobreezeMatter...");
  
  // Initialize Matter
  Matter.begin();
  matter_fan.begin();

  // Initialize PWM Driver
  Wire.begin();
  Serial.print("Checking for PWM driver at 0x40... ");
  Wire.beginTransmission(0x40);
  if (Wire.endTransmission() == 0) {
    Serial.println("Found!");
  } else {
    Serial.println("Not found. Check wiring.");
  }

  pwm.begin();
  pwm.setOscillatorFrequency(25000000);
  pwm.setPWMFreq(SERVO_FREQ);
  delay(10);

  // Arming removed for solid voltage test
  // Serial.println("Arming ESC (sending 1000us pulse)...");
  // ...

  pinMode(BTN_BUILTIN, INPUT_PULLUP);
  pinMode(LEDR, OUTPUT);
  digitalWrite(LEDR, HIGH);

  Serial.println("Matter fan with Solid 5V Test initialized");

  if (!Matter.isDeviceCommissioned()) {
    Serial.println("Matter device is not commissioned");
    Serial.println("Commission it to your Matter hub with the manual pairing code or QR code");
    Serial.printf("Manual pairing code: %s\n", Matter.getManualPairingCode().c_str());
    Serial.printf("QR code URL: %s\n", Matter.getOnboardingQRCodeUrl().c_str());
  }
  while (!Matter.isDeviceCommissioned()) {
    delay(200);
  }

  Serial.println("Waiting for Thread network...");
  while (!Matter.isDeviceThreadConnected()) {
    delay(200);
    decommission_handler();
  }
  Serial.println("Connected to Thread network");

  Serial.println("Waiting for Matter device discovery...");
  while (!matter_fan.is_online()) {
    delay(200);
    decommission_handler();
  }
  Serial.println("Matter device is now online");
}

void loop()
{
  decommission_handler();
  
  static uint8_t last_effective_speed = 255; // Force update on first loop
  static int manual_override = -1;
  static uint8_t last_matter_percent = 0;

  if (Serial.available() > 0) {
    int val = Serial.parseInt();
    // consume any trailing characters like newline
    while (Serial.available()) Serial.read();

    if (val >= 0 && val <= 100) {
      manual_override = val;
      Serial.print("Manual Override Set: ");
      Serial.println(manual_override);
    } else {
      manual_override = -1;
      Serial.println("Manual Override Disabled (Matter Control)");
    }
  }
  
  bool current_state = matter_fan.get_onoff();
  uint8_t current_percent = matter_fan.get_percent();

  if (current_percent != last_matter_percent) {
    last_matter_percent = current_percent;
    Serial.printf("Matter Percent Changed: %d%%\n", current_percent);
  }
  
  // If manual override is active (0-100), use it.
  // Otherwise, if the fan is logically OFF, speed is 0. Else use the percentage.
  uint8_t effective_speed;
  if (manual_override != -1) {
    effective_speed = (uint8_t)manual_override;
  } else {
    effective_speed = current_state ? current_percent : 0;
  }

  if (effective_speed != last_effective_speed) {
    last_effective_speed = effective_speed;
    
    Serial.print("Fan Speed Update: ");
    Serial.print(effective_speed);
    
    if (effective_speed > 0) {
        Serial.println("% -> SOLID 5V (ON)");
        // Set fully ON (4096, 0)
        pwm.setPWM(ESC_CHANNEL, 4096, 0); 
    } else {
        Serial.println("% -> SOLID 0V (OFF)");
        // Set fully OFF (0, 4096)
        pwm.setPWM(ESC_CHANNEL, 0, 4096);
    }
  }

  // Debug printing for state changes (optional)
  static bool fan_last_state = false;
  if (current_state != fan_last_state) {
    fan_last_state = current_state;
    if (current_state) {
      Serial.println("Matter Fan State: ON");
    }
    else {
      Serial.println("Matter Fan State: OFF");
    }
  }

  static uint32_t last_debug_print = 0;
  if (millis() - last_debug_print > 5000) {
    last_debug_print = millis();
    Serial.printf("Status: On/Off=%d, Level=%d, Thread=%s, Online=%s\n", 
      current_state, 
      current_percent, 
      Matter.isDeviceThreadConnected() ? "YES" : "NO", 
      matter_fan.is_online() ? "YES" : "NO"
    );
  }
}

void decommission_handler()
{
  // If the button is not pressed or the device is not commissioned - return
  if (digitalRead(BTN_BUILTIN) != LOW || !Matter.isDeviceCommissioned()) {
    return;
  }

  // Store the time when the button was first pressed
  uint32_t start_time = millis();
  // While the button is being pressed
  while (digitalRead(BTN_BUILTIN) == LOW) {
    // Calculate the elapsed time
    uint32_t elapsed_time = millis() - start_time;
    // If the button has been pressed for less than 10 seconds, continue
    if (elapsed_time < 10000u) {
      yield();
      continue;
    }

    // Blink the LED to indicate the start of the decommissioning process
    for (uint8_t i = 0u; i < 10u; i++) {
      digitalWrite(LED_BUILTIN, !(digitalRead(LED_BUILTIN)));
      delay(100);
    }

    Serial.println("Starting decommissioning process, device will reboot...");
    Serial.println();
    digitalWrite(LED_BUILTIN, LED_BUILTIN_INACTIVE);
    // This function will not return
    // The device will restart once decommissioning has finished
    Matter.decommission();
  }
}
