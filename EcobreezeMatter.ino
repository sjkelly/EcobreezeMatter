#include <Matter.h>
#include <MatterFan.h>
#include <Wire.h>
#include <Adafruit_PWMServoDriver.h>

#define ESC_CHANNEL 4
#define MIN_PULSE_WIDTH 1000 // 1000us
#define MAX_PULSE_WIDTH 2000 // 2000us
#define SERVO_FREQ 50 // Analog servos and ESCs run at ~50 Hz

MatterFan matter_fan;
Adafruit_PWMServoDriver pwm = Adafruit_PWMServoDriver();

void setup()
{
  Serial.begin(115200);
  
  // Initialize Matter
  Matter.begin();
  matter_fan.begin();

  // Initialize PWM Driver
  pwm.begin();
  pwm.setOscillatorFrequency(27000000);
  pwm.setPWMFreq(SERVO_FREQ);
  delay(10);

  pinMode(BTN_BUILTIN, INPUT_PULLUP);
  pinMode(LEDR, OUTPUT);
  digitalWrite(LEDR, HIGH);

  Serial.println("Matter fan with ESC");

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
    Serial.println("%\n");
    
    // Update legacy pin D4
    int speed_analog = map(effective_speed, 0, 100, 0, 255);
    analogWrite(fan_pin, speed_analog);
    
    // Update ESC on I2C PWM FeatherWing
    // Map 0-100% to pulse width in microseconds
    long pulse_us = map(effective_speed, 0, 100, MIN_PULSE_WIDTH, MAX_PULSE_WIDTH);
    
    // Convert microseconds to 12-bit counts (0-4095)
    // counts = (pulse_us * 4096) / (1000000 / 50) 
    //        = (pulse_us * 4096 * 50) / 1000000
    //        = pulse_us * 0.2048
    uint16_t pulse_counts = (uint16_t)(pulse_us * 0.2048);
    
    pwm.setPWM(ESC_CHANNEL, 0, pulse_counts);
  }

  // Debug printing for state changes (optional, keeping close to original logic)
  static bool fan_last_state = false;
  if (current_state != fan_last_state) {
    fan_last_state = current_state;
    if (current_state) {
      Serial.println("Fan ON");
    } else {
      Serial.println("Fan OFF");
    }
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