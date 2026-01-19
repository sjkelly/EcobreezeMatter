# Matter OTA Firmware Update Guide for EcobreezeMatter

This guide outlines the process for creating and deploying Over-The-Air (OTA) firmware updates for the EcobreezeMatter project running on the Silicon Labs Nano Matter board.

## 1. Overview

The `nano_matter` Arduino board definition automatically enables the **Matter OTA Software Update Requestor** cluster. This means your device is already capable of:
1.  Querying an OTA Provider for updates.
2.  Downloading the update image.
3.  Applying the update and rebooting.

Your responsibility is to **build** and **package** the firmware correctly so the OTA Provider can serve it.

## 2. Prerequisites

You will need the following tools installed on your development machine:

1.  **Arduino CLI** (already in `./bin/arduino-cli`): To build the project.
2.  **Simplicity Commander**: A Silicon Labs tool to create Gecko Bootloader (`.gbl`) files.
    *   [Download from Silicon Labs](https://www.silabs.com/developers/simplicity-studio#commander)
    *   Ensure `commander` is in your system PATH.
3.  **Matter `ota-image-tool.py`**: A python script from the Connected Home IP (Matter) SDK to wrap the image with Matter-specific headers.
    *   You can typically find this in the `src/app/ota_image_tool.py` of the [Matter SDK repo](https://github.com/project-chip/connectedhomeip).
    *   Requires Python 3 and dependencies (often `pip install ecdsa`).

## 3. The Build & Package Workflow

### Step 1: Compile the Firmware
Compile your sketch as usual to produce the standard raw binary/hex file.

```bash
# Example build command
./bin/arduino-cli compile --fqbn SiliconLabs:silabs:nano_matter --output-dir build/ .
```

This will output `build/EcobreezeMatter.ino.hex`.

### Step 2: Convert to Gecko Bootloader Image (.gbl)

The Silicon Labs chip requires a specific container format called `.gbl` for bootloader updates.

```bash
commander gbl create build/EcobreezeMatter.gbl --app build/EcobreezeMatter.ino.hex
```

### Step 3: Wrap in Matter OTA Container (.ota)

The Matter protocol requires the `.gbl` file to be wrapped with a header containing Vendor ID (VID), Product ID (PID), and Version info.

**Crucial:** The `SoftwareVersion` (integer) in the command below **must be higher** than the version currently running on the device, or the update will be ignored.

```bash
# Example: Creating an update for Version 2 (SoftwareVersion 2, SoftwareVersionString "2.0")
# VID: 0xDEAD (Default Test VID, check your config)
# PID: 0xBEEF (Default Test PID, check your config)

python3 ota-image-tool.py create \
    -v 0xDEAD \
    -p 0xBEEF \
    -vn 2 \
    -vs "2.0" \
    -da sha256 \
    build/EcobreezeMatter.gbl \
    build/EcobreezeMatter.ota
```

## 4. Serving the Update

Once you have the `.ota` file, you need a **Matter OTA Provider**. This is usually a feature of your Matter Controller or Hub.

### Option A: chip-tool (Linux/Mac)
If you are using the reference `chip-tool` for testing:

1.  **Launch the OTA Provider app:**
    ```bash
    ./chip-ota-provider-app -f build/EcobreezeMatter.ota
    ```
2.  **Commission the Provider:**
    In a separate terminal, commission the provider app (Node ID 2) to your controller.
    ```bash
    ./chip-tool pairing onnetwork 2 20202021
    ```
3.  **Announce the Update:**
    Tell the device (EcobreezeMatter, Node ID 1) about the provider (Node ID 2).
    ```bash
    ./chip-tool otasoftwareupdaterequestor announce-otaprovider 1 2 0 0 0 2
    ```

### Option B: Home Assistant / Apple Home / Google Home
Commercial hubs handle OTA differently.
*   **Home Assistant:** Currently requires the Matter Server add-on. You often need to upload the `.ota` file to a specific directory that the Matter Server scans.
*   **Google/Apple:** Often require uploading the firmware to the Connectivity Standards Alliance (CSA) Distributed Compliance Ledger (DCL) or a manufacturer-specific cloud portal (for certified devices). For development, you likely need to stick to `chip-tool` or specific developer tools provided by the ecosystem.

## 5. Troubleshooting
*   **Version Mismatch:** Ensure the `-vn` (integer version) is strictly greater than the one defined in your `EcobreezeMatter.ino` (standard Matter default is often 0 or 1).
*   **VID/PID Mismatch:** The Vendor and Product IDs in the `ota-image-tool.py` command must match exactly what is defined in your `boards.txt` or overridden in your code.
