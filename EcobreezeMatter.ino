#include <Matter.h>
#include <MatterFan.h>

#include <openthread/instance.h>
#include <openthread/platform/radio.h>

extern "C" {
  extern otInstance *sInstance;
}

MatterFan matter_fan;

void setup()
{
  Serial.begin(115200);
  delay(2000); // Give time for Serial to connect
  Serial.println(" Booting EcobreezeMatter...");
  
  // Initialize Matter
  Matter.begin();
  matter_fan.begin();

  if (sInstance) {
    int8_t power = 0;
    otError error = otPlatRadioGetTransmitPower(sInstance, &power);
    if (error == OT_ERROR_NONE) {
      Serial.printf("Current Thread TX Power: %d dBm\n", power);
    } else {
      Serial.printf("Failed to get Thread TX Power, error: %d\n", error);
    }
    
    // Try to set to 20 dBm (hardware might cap it)
    error = otPlatRadioSetTransmitPower(sInstance, 20); 
    if (error == OT_ERROR_NONE) {
       Serial.println("Requested Thread TX Power: 20 dBm");
       
       // Verify
       otPlatRadioGetTransmitPower(sInstance, &power);
       Serial.printf("New Thread TX Power: %d dBm\n", power);
    } else {
       Serial.printf("Failed to set Thread TX Power, error: %d\n", error);
    }
  } else {
    Serial.println("OpenThread Instance (sInstance) is NULL!");
  }

  // Initialize Fan Pin
  pinMode(D8, OUTPUT);
  digitalWrite(D8, LOW); // Start with fan off

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
  bool current_state = matter_fan.get_onoff();
  uint8_t current_percent = matter_fan.get_percent();

  // Debug printing for state changes (optional)
  static bool fan_last_state = false;
  if (current_state != fan_last_state) {
    fan_last_state = current_state;
    if (current_state) {
      Serial.println("Matter Fan State: ON");
      digitalWrite(D8, HIGH);
    }
    else {
      Serial.println("Matter Fan State: OFF");
      digitalWrite(D8, LOW);
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
