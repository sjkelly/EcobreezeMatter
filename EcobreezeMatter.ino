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

int manual_percent = -1; // -1 indicates Matter control, 0-100 indicates Manual override

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
  
  Serial.println("System Ready. Send 0-100 for Manual Control, -1 for Matter Control.");
}

void loop() {
  decommission_handler();

  // --- 1. Handle Serial Input (Manual Override) ---
  if (Serial.available()) {
    int val = Serial.parseInt();
    while (Serial.available()) Serial.read(); // Flush remainder

    if (val >= 0 && val <= 100) {
      manual_percent = val;
      Serial.printf("MANUAL OVERRIDE: Set to %d%%\n", manual_percent);
    } else if (val == -1) {
      manual_percent = -1;
      Serial.println("MODE SWITCH: Resuming Matter control");
    } else {
      Serial.println("Invalid input. Use 0-100 or -1.");
    }
  }

  // --- 2. Determine Target State ---
  FanState targetState;

  if (manual_percent >= 0) {
    // Manual Mode
    targetState.source = "MANUAL";
    targetState.on = (manual_percent > 0);
    targetState.percent = (uint8_t)manual_percent;
    
    // Map percent to approx mode for display
    if (!targetState.on) targetState.mode = DeviceFan::fan_mode_t::Off;
    else if (targetState.percent <= 10) targetState.mode = DeviceFan::fan_mode_t::Low;
    else if (targetState.percent <= 50) targetState.mode = DeviceFan::fan_mode_t::Med;
    else targetState.mode = DeviceFan::fan_mode_t::High;

  } else {
    // Matter Mode
    targetState.source = "MATTER";
    if (Matter.isDeviceCommissioned() && matter_fan.is_online()) {
      targetState.on = matter_fan.get_onoff();
      targetState.mode = matter_fan.get_mode();
      
      // Determine percent from mode or raw value
      uint8_t raw_percent = matter_fan.get_percent();
      
      if (targetState.mode == DeviceFan::fan_mode_t::Low) targetState.percent = 10;
      else if (targetState.mode == DeviceFan::fan_mode_t::Med) targetState.percent = 50;
      else if (targetState.mode == DeviceFan::fan_mode_t::High) targetState.percent = 100;
      else if (targetState.mode == DeviceFan::fan_mode_t::Off) targetState.percent = 0;
      else {
        // Use raw percent (clamped)
        // Handle potential 0-254 scaling if value > 100
        if (raw_percent > 100) raw_percent = map(raw_percent, 0, 254, 0, 100);
        targetState.percent = (raw_percent > 100) ? 100 : raw_percent;
      }

      // Explicit OFF override
      if (!targetState.on) targetState.percent = 0;

    } else {
      // Not online/commissioned
      targetState.source = "OFFLINE";
      targetState.on = false;
      targetState.percent = 0;
      targetState.mode = DeviceFan::fan_mode_t::Off;
    }
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
