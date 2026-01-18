#include <Matter.h>
#include <MatterFan.h>
#include <DFRobot_GP8XXX.h>
#include <Wire.h>

#include <openthread/instance.h>
#include <openthread/platform/radio.h>

extern "C" {
  extern otInstance *sInstance;
}

MatterFan matter_fan;

// Use the parent class DFRobot_GP8XXX_IIC directly to allow specifying &Wire1.
// Connect Sensor to SDA -> D4, SCL -> D5
DFRobot_GP8XXX_IIC GP8211S(RESOLUTION_15_BIT, DFGP8XXX_I2C_DEVICEADDR, &Wire1);

void decommission_handler();

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

  // I2C Scanner on Wire1 (D4/D5)
  Serial.println("Scanning I2C bus on Wire1 (D4/D5)...");
  Wire1.begin();

  int nDevices = 0;
  for (byte address = 1; address < 127; address++) {
    Wire1.beginTransmission(address);
    byte error = Wire1.endTransmission();
    if (error == 0) {
      Serial.print("I2C device found at address 0x");
      if (address < 16) Serial.print("0");
      Serial.print(address, HEX);
      Serial.println("  !");
      nDevices++;
    } else if (error == 4) {
      Serial.print("Unknown error at address 0x");
      if (address < 16) Serial.print("0");
      Serial.println(address, HEX);
    }
  }
  if (nDevices == 0)
    Serial.println("No I2C devices found on Wire1\n");
  else
    Serial.println("done\n");

  // Initialize GP8211S DAC
  if (GP8211S.begin() != 0) {
    Serial.println("GP8211S initialization failed! Check wiring (D4/D5).");
  } else {
    GP8211S.setDACOutRange(DFRobot_GP8XXX::eOutputRange10V);
    GP8211S.setDACOutVoltage(0);
    Serial.println("GP8211S initialized (0-10V range) on Wire1");
  }

  delay(10);

  pinMode(BTN_BUILTIN, INPUT_PULLUP);
  pinMode(LEDR, OUTPUT);
  digitalWrite(LEDR, HIGH);

  Serial.println("Matter fan initialized");

  // Check for Manual Mode entry
  Serial.println("Press any key within 5 seconds to enter MANUAL TEST MODE (skips Matter wait)...");
  uint32_t wait_start = millis();
  bool manual_mode = false;
  while (millis() - wait_start < 5000) {
    if (Serial.available()) {
      while(Serial.available()) Serial.read(); // Clear buffer
      manual_mode = true;
      Serial.println("!!! MANUAL TEST MODE ACTIVATED !!!");
      Serial.println("Send 0-100 to set voltage.");
      break;
    }
    delay(10);
  }

  if (!manual_mode) {
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
}

void loop()
{
  decommission_handler();
  
  static int manual_percent = -1;
  static bool last_online_state = false;
  
  // Check for Matter online state changes
  bool current_online_state = matter_fan.is_online();
  if (current_online_state != last_online_state) {
    last_online_state = current_online_state;
    Serial.printf("Matter Fan is now %s\n", current_online_state ? "ONLINE" : "OFFLINE");
  }

  // Serial Input Handler for Manual Test
  if (Serial.available()) {
    int val = Serial.parseInt();
    // consume potential leftovers like newline
    while(Serial.available()) Serial.read();
    
    if (val >= 0 && val <= 100) {
        manual_percent = val;
        Serial.printf("MANUAL MODE: Set to %d%%\n", manual_percent);
    } else if (val == -1) {
        manual_percent = -1;
        Serial.println("MATTER MODE: Resuming Matter control");
    } else {
        Serial.println("Invalid input. Enter 0-100 for Manual, or -1 for Matter.");
    }
  }

  bool current_state;
  uint8_t current_percent;
  DeviceFan::fan_mode_t current_mode = DeviceFan::fan_mode_t::Off;

  if (manual_percent >= 0) {
      current_state = (manual_percent > 0);
      current_percent = (uint8_t)manual_percent;
      if (current_percent == 0) current_mode = DeviceFan::fan_mode_t::Off;
      else if (current_percent <= 33) current_mode = DeviceFan::fan_mode_t::Low;
      else if (current_percent <= 66) current_mode = DeviceFan::fan_mode_t::Med;
      else current_mode = DeviceFan::fan_mode_t::High;
  } else {
      current_state = matter_fan.get_onoff();
      current_percent = matter_fan.get_percent();
      current_mode = matter_fan.get_mode();
      
      // If mode is specific preset, we might want to prioritize it or map it
      if (current_mode == DeviceFan::fan_mode_t::Low) current_percent = 33;
      else if (current_mode == DeviceFan::fan_mode_t::Med) current_percent = 66;
      else if (current_mode == DeviceFan::fan_mode_t::High) current_percent = 100;
      else if (current_mode == DeviceFan::fan_mode_t::Off) current_percent = 0;

      // Matter Level Control usually uses 0-254. If we see a value > 100, 
      // we assume it's the raw 0-254 scale and map it to 0-100 to prevent DAC overflow.
      if (current_percent > 100) {
         current_percent = map(current_percent, 0, 254, 0, 100);
      }
  }
  
  // Safety clamp
  if (current_percent > 100) current_percent = 100;

  static bool fan_last_state = false;
  static uint8_t fan_last_percent = 0;
  static DeviceFan::fan_mode_t fan_last_mode = DeviceFan::fan_mode_t::Off;
  static String last_source = "";

  String current_source = (manual_percent >= 0) ? "MANUAL" : "MATTER";

  if (current_state != fan_last_state || current_percent != fan_last_percent || current_mode != fan_last_mode || current_source != last_source) {
    fan_last_state = current_state;
    fan_last_percent = current_percent;
    fan_last_mode = current_mode;
    last_source = current_source;

    const char* mode_names[] = {"Off", "Low", "Med", "High", "On", "Auto", "Smart"};
    const char* mode_str = (current_mode >= 0 && current_mode <= 6) ? mode_names[current_mode] : "Unknown";

    if (current_state) {
      Serial.printf("Fan Update: ON, Speed: %d%%, Mode: %s (Source: %s)\n", current_percent, mode_str, current_source.c_str());
      // Normalize: 0% speed = 3V (30% of 10V), 100% speed = 10V (100% of 10V)
      // 15-bit resolution for GP8211S (0-32767)
      uint32_t dac_min = 9830; // 30% of 32767 is ~9830 (3V)
      uint32_t dac_range = 32767 - dac_min;
      uint16_t dac_value = dac_min + ((uint32_t)current_percent * dac_range / 100);
      
      GP8211S.setDACOutVoltage(dac_value);
    }
    else {
      Serial.printf("Fan Update: OFF, Mode: %s (Source: %s)\n", mode_str, current_source.c_str());
      GP8211S.setDACOutVoltage(0);
    }
  }

  static uint32_t last_debug_print = 0;
  if (millis() - last_debug_print > 10000) {
    last_debug_print = millis();
    const char* mode_names[] = {"Off", "Low", "Med", "High", "On", "Auto", "Smart"};
    const char* mode_str = (current_mode >= 0 && current_mode <= 6) ? mode_names[current_mode] : "Unknown";
    Serial.printf("Status: %s | On/Off=%d | Speed=%d%% | Mode=%s | Online=%d\n", 
      current_source.c_str(),
      current_state, 
      current_percent,
      mode_str,
      current_online_state
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