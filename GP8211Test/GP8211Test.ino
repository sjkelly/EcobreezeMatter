#include <DFRobot_GP8XXX.h>
#include <Wire.h>

/**
 * We use the parent class DFRobot_GP8XXX_IIC directly because it allows 
 * us to specify &Wire1 in the constructor. The GP8211S is a 15-bit DAC.
 */
DFRobot_GP8XXX_IIC dac(RESOLUTION_15_BIT, DFGP8XXX_I2C_DEVICEADDR, &Wire1);

void setup() {
  Serial.begin(115200);
  while (!Serial);
  delay(2000);

  Serial.println("\n=== GP8211 Wire1 Voltage Test ===");
  Serial.println("Pins: SDA -> D4, SCL -> D5");

  // Initialize the DAC on Wire1
  if (dac.begin() != 0) {
    Serial.println("DAC initialization failed! Check wiring to D4/D5 and 12V power.");
    while(1);
  }

  // Set to 0-10V range (requires 12V VCC to the GP8211)
  dac.setDACOutRange(dac.eOutputRange10V);
  
  // Start at 0V
  dac.setDACOutVoltage(0);
  
  Serial.println("DAC Initialized on Wire1 (0-10V Mode)");
  Serial.println("Enter a value 0-100 to set voltage percentage (e.g. 50 = 5V):");
}

void loop() {
  if (Serial.available()) {
    int percentage = Serial.parseInt();
    
    // Clear buffer
    while(Serial.available()) Serial.read();

    if (percentage >= 0 && percentage <= 100) {
      // GP8211S uses 15-bit resolution (0-32767)
      uint16_t dac_value = (uint32_t)percentage * 32767 / 100;
      
      dac.setDACOutVoltage(dac_value);
      
      float expected_v = (float)percentage / 10.0;
      Serial.printf("Setting Output: %d%% (~%.1fV) | DAC Value: %u\n", percentage, expected_v, dac_value);
    } else {
      Serial.println("Invalid input. Enter 0 to 100.");
    }
  }
}