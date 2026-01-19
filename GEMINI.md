# EcobreezeMatter Fan Controller

## Hardware Details
- **Board:** Silicon Labs Nano Matter (`SiliconLabs:silabs:nano_matter`)
- **DAC:** DFRobot GP8211S (0-10V output range)
- **Motor:** McMillan H-bridge BLDC motor (Model F2012A4661)
  - Control Signal: 0-10V DC analog.
  - Signal Logic: SIG+GND. 5V on SIG activates motor (PWM/Analog control).
  - Voltage Scaling: In software, 0% speed maps to 3V (30% DAC) and 100% speed maps to 10V (100% DAC).

## Matter Library Locations (Silicon Labs Arduino Core)
- **MatterFan.h:** `/home/sjkelly/.arduino15/packages/SiliconLabs/hardware/silabs/3.0.0/libraries/Matter/src/MatterFan.h`
- **DeviceFan.h:** `/home/sjkelly/.arduino15/packages/SiliconLabs/hardware/silabs/3.0.0/libraries/Matter/src/devices/DeviceFan.h`
- **Implementation:** `/home/sjkelly/.arduino15/packages/SiliconLabs/hardware/silabs/3.0.0/libraries/Matter/src/devices/DeviceFan.cpp`

## Fan Mode Mappings
The following presets are mapped to percentage values for the DAC output:
- **Low:** 33% (approx. 5.3V)
- **Medium:** 66% (approx. 7.6V)
- **High:** 100% (10.0V)
- **Off:** 0% (0V)

## OTA Firmware Update Status
- **Support:** Enabled by default (Requestor Cluster is active in `nano_matter` variant).
- **Process:** 
  1. Build firmware to get `.hex`.
  2. Convert to `.gbl` using Simplicity Commander.
  3. Wrap in `.ota` using Matter SDK tools.

## Development Notes
- Use `./bin/arduino-cli` for compilation and uploading.
- The project uses `Wire1` (D4/D5) for I2C communication with the DAC.
- Manual test mode can be entered by sending any key over Serial within 5 seconds of boot.