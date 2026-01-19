#include <Matter.h>
#include <MatterFan.h>
#include <DFRobot_GP8XXX.h>
#include <Wire.h>
#include <openthread/instance.h>
#include <openthread/platform/radio.h>

extern "C" {
  extern otInstance *sInstance;
}

// --- Configuration & Constants ---
constexpr uint16_t DAC_RESOLUTION = 32767; // 15-bit
constexpr uint16_t DAC_MIN_VAL    = 9830;  // ~3V (30% of 10V) for 0% speed
constexpr uint16_t DAC_RANGE      = DAC_RESOLUTION - DAC_MIN_VAL;

constexpr uint32_t STATUS_REPORT_INTERVAL_MS = 5000;

// --- Global Objects ---
MatterFan matter_fan;

// Connect Sensor to SDA -> D4, SCL -> D5
// Using the parent class DFRobot_GP8XXX_IIC directly to allow specifying &Wire1.
DFRobot_GP8XXX_IIC GP8211S(RESOLUTION_15_BIT, DFGP8XXX_I2C_DEVICEADDR, &Wire1);

// --- State Management ---
struct FanState {
  bool on = false;
  uint8_t percent = 0;
  DeviceFan::fan_mode_t mode = DeviceFan::fan_mode_t::Off;
  String source = "BOOT";
};

FanState currentState;
FanState lastHardwareState;

// --- Helper Functions ---

const char* getModeString(DeviceFan::fan_mode_t mode) {
  switch (mode) {
    case DeviceFan::fan_mode_t::Off: return "Off";
    case DeviceFan::fan_mode_t::Low: return "Low";
    case DeviceFan::fan_mode_t::Med: return "Med";
    case DeviceFan::fan_mode_t::High: return "High";
    case DeviceFan::fan_mode_t::On: return "On";
    case DeviceFan::fan_mode_t::Auto: return "Auto";
    case DeviceFan::fan_mode_t::Smart: return "Smart";
    default: return "Unknown";
  }
}

void setThreadTxPower(int8_t powerDbm) {
  if (!sInstance) return;
  otError error = otPlatRadioSetTransmitPower(sInstance, powerDbm);
  if (error == OT_ERROR_NONE) {
    Serial.printf("Thread TX Power set to: %d dBm\n", powerDbm);
  } else {
    Serial.printf("Failed to set Thread TX Power to %d dBm, error: %d\n", powerDbm, error);
  }
}

// Updates the physical DAC output based on state
void updateFanHardware(bool on, uint8_t percent) {
  if (!on) {
    GP8211S.setDACOutVoltage(0);
    return;
  }

  // Map 0-100% speed to 3V-10V range (DAC_MIN_VAL to DAC_RESOLUTION)
  // 0% speed = 3V to ensure motor startup/movement if ON
  uint16_t dac_value = DAC_MIN_VAL + ((uint32_t)percent * DAC_RANGE / 100);
  GP8211S.setDACOutVoltage(dac_value);
}

void decommission_handler() {
  if (digitalRead(BTN_BUILTIN) != LOW || !Matter.isDeviceCommissioned()) {
    return;
  }

  uint32_t start_time = millis();
  while (digitalRead(BTN_BUILTIN) == LOW) {
    if (millis() - start_time >= 10000u) {
      // Blink LED to indicate action
      for (uint8_t i = 0; i < 10; i++) {
        digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
        delay(100);
      }
      Serial.println("Decommissioning device...");
      digitalWrite(LED_BUILTIN, LED_BUILTIN_INACTIVE);
      Matter.decommission(); // Will reboot
    }
    yield();
  }
}

void setup() {
  Serial.begin(115200);
  
  // Non-blocking wait for Serial (optional, just for catching early logs)
  uint32_t start = millis();
  while (!Serial && millis() - start < 2000) { delay(10); }

  Serial.println("\n--- Booting EcobreezeMatter ---");

  // Init Hardware
  pinMode(BTN_BUILTIN, INPUT_PULLUP);
  pinMode(LEDR, OUTPUT);
  digitalWrite(LEDR, HIGH);

  Wire1.begin(); // D4/D5
  
  if (GP8211S.begin() != 0) {
    Serial.println("ERROR: GP8211S DAC initialization failed! Check wiring (D4/D5).");
  } else {
    GP8211S.setDACOutRange(DFRobot_GP8XXX::eOutputRange10V);
    GP8211S.setDACOutVoltage(0);
    Serial.println("GP8211S initialized (0-10V).");
  }

  // Init Matter
  Matter.begin();
  matter_fan.begin();
  Serial.println("Matter initialized.");

  // Configure Thread Radio
  //setThreadTxPower(20);

  // Print pairing info if needed
  if (!Matter.isDeviceCommissioned()) {
    Serial.println("Device NOT Commissioned.");
    Serial.printf("Pairing Code: %s\n", Matter.getManualPairingCode().c_str());
    Serial.printf("QR URL: %s\n", Matter.getOnboardingQRCodeUrl().c_str());
  } else {
    Serial.println("Device is Commissioned.");
  }
  
  Serial.println("System Ready. Send 0-100 to update Fan State.");
}

void loop() {
  decommission_handler();

  // --- 1. Handle Serial Input (Update Matter State) ---
  if (Serial.available()) {
    int val = Serial.parseInt();
    while (Serial.available()) Serial.read(); // Flush remainder

    if (val >= 0 && val <= 100) {
      Serial.printf("CMD: Set to %d%%\n", val);
      if (val == 0) {
        matter_fan.set_onoff(false);
      } else {
        matter_fan.set_onoff(true);
        matter_fan.set_percent((uint8_t)val);
        // Switch to 'On' mode to avoid 'Low'/'Med'/'High' presets overriding the custom percent
        matter_fan.set_mode(DeviceFan::fan_mode_t::On); 
      }
    } else {
      Serial.println("Invalid input. Use 0-100.");
    }
  }

  // --- 2. Determine Target State ---
  FanState targetState;

  // Use static variables to track the *previous* read from Matter, 
  // allowing us to detect WHICH attribute changed (Mode vs Percent).
  static DeviceFan::fan_mode_t last_matter_mode = DeviceFan::fan_mode_t::Off;
  static uint8_t last_matter_percent = 0;
  static bool first_run = true;

  // Initialize tracking on first run
  if (first_run && Matter.isDeviceCommissioned() && matter_fan.is_online()) {
    last_matter_mode = matter_fan.get_mode();
    last_matter_percent = matter_fan.get_percent();
    first_run = false;
  }

  // Always sync from Matter (Local changes above updated Matter already)
  if (Matter.isDeviceCommissioned() && matter_fan.is_online()) {
    targetState.source = "MATTER";
    
    DeviceFan::fan_mode_t current_mode = matter_fan.get_mode();
    uint8_t current_percent = matter_fan.get_percent();

    // Check for changes
    bool mode_changed = (current_mode != last_matter_mode);
    bool percent_changed = (current_percent != last_matter_percent);

    if (mode_changed) {
      Serial.printf("EVENT: Mode changed %s -> %s\n", getModeString(last_matter_mode), getModeString(current_mode));
      
      // Mode takes priority. Enforce the preset speed.
      if (current_mode == DeviceFan::fan_mode_t::Low) current_percent = 10;
      else if (current_mode == DeviceFan::fan_mode_t::Med) current_percent = 50;
      else if (current_mode == DeviceFan::fan_mode_t::High) current_percent = 100;
      else if (current_mode == DeviceFan::fan_mode_t::Off) current_percent = 0;
      
      // Sync the percent back to Matter so the slider updates in the App
      if (current_percent != matter_fan.get_percent()) {
        matter_fan.set_percent(current_percent); 
      }
    } 
    else if (percent_changed) {
       Serial.printf("EVENT: Percent changed %d -> %d\n", last_matter_percent, current_percent);
       
       // Percent takes priority. If we are in a fixed mode but speed changed, switch to 'On'.
       if (current_mode == DeviceFan::fan_mode_t::Low || 
           current_mode == DeviceFan::fan_mode_t::Med || 
           current_mode == DeviceFan::fan_mode_t::High) {
         
         // Only switch mode if the new percent doesn't match the current mode's definition
         // (Allows small jitter? No, strict equality is safer for now)
         uint8_t expected = 0;
         if (current_mode == DeviceFan::fan_mode_t::Low) expected = 10;
         if (current_mode == DeviceFan::fan_mode_t::Med) expected = 50;
         if (current_mode == DeviceFan::fan_mode_t::High) expected = 100;

         if (current_percent != expected) {
            Serial.println("ACTION: Speed override detected. Switching Mode to ON.");
            current_mode = DeviceFan::fan_mode_t::On;
            matter_fan.set_mode(current_mode);
         }
       }
       
       // Handle 0% = Off
       if (current_percent == 0 && current_mode != DeviceFan::fan_mode_t::Off) {
          current_mode = DeviceFan::fan_mode_t::Off;
          matter_fan.set_mode(current_mode);
       }
       // Handle >0% but Off -> Turn On
       if (current_percent > 0 && current_mode == DeviceFan::fan_mode_t::Off) {
          current_mode = DeviceFan::fan_mode_t::On;
          matter_fan.set_mode(current_mode);
       }
    }
    
    // Update tracking
    last_matter_mode = current_mode;
    last_matter_percent = current_percent;

    // Final state construction
    targetState.mode = current_mode;
    
    // Sanity check raw percent for display/DAC
    if (current_percent > 100) {
        current_percent = map(current_percent, 0, 254, 0, 100); // just in case
        if (current_percent > 100) current_percent = 100;
    }
    targetState.percent = current_percent;
    targetState.on = (targetState.mode != DeviceFan::fan_mode_t::Off) && (targetState.percent > 0);

  } else {
    // Not online/commissioned
    targetState.source = "OFFLINE";
    targetState.on = false;
    targetState.percent = 0;
    targetState.mode = DeviceFan::fan_mode_t::Off;
  }

  currentState = targetState;

  // --- 3. Update Hardware if Changed ---
  if (currentState.on != lastHardwareState.on || 
      currentState.percent != lastHardwareState.percent ||
      currentState.source != lastHardwareState.source) {
      
    Serial.printf("Fan Update [%s]: State=%s, Speed=%d%%, Mode=%s\n", 
                  currentState.source.c_str(), 
                  currentState.on ? "ON" : "OFF", 
                  currentState.percent, 
                  getModeString(currentState.mode));

    updateFanHardware(currentState.on, currentState.percent);
    lastHardwareState = currentState;
  }

  // --- 4. Periodic Status Logging ---
  static uint32_t last_log = 0;
  if (millis() - last_log > STATUS_REPORT_INTERVAL_MS) {
    last_log = millis();
    bool is_online = Matter.isDeviceCommissioned() && matter_fan.is_online();
    Serial.printf("STATUS: [%s] State=%d Speed=%d%% Mode=%s | Matter Online: %s\n", 
      currentState.source.c_str(),
      currentState.on, 
      currentState.percent,
      getModeString(currentState.mode),
      is_online ? "YES" : "NO"
    );
  }
}
