# Ecobreeze Matter

## Overview
This module documents the specifications and control interface for the McMillan Electric Company "Brushfree DC" motor (Model: F2012A4661). This unit is an Electronically Commutated (EC) motor custom-manufactured for Nature’s Cooling Solutions. It features an integrated H-bridge power drive, allowing for variable speed control directly from AC mains.

## 1. Technical Specifications

| Attribute | Rating / Value |
| :--- | :--- |
| **Manufacturer** | McMillan Electric Company (Woodville, WI, USA) |
| **Model Number** | F2012A4661 |
| **Input Voltage** | 120-230 V AC |
| **Frequency** | 60 Hz |
| **Power Consumption** | 28 W |
| **Operating Current** | 0.6A @ 120V / 0.4A @ 230V |
| **Speed Range** | 550 - 1200 RPM |
| **Insulation** | Class B (E62615) |
| **Thermal Protection** | E62862 |
| **Relevant Patents** | US 6,850,019; US 6,940,238 |

## 2. Control Interface
The motor is controlled via a SIG and GND interface.
- **Signal Type:** SIG + GND
- **Activation:** Applying 5V to the SIG line activates the motor.
- **Drive Logic:** Integrated H-bridge power drive.