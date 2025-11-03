# HydroControl-GE: Universal Industrial Firmware

This repository contains the universal firmware for the HydroControl-GE project, a smart management system for the hydraulic network of the Miloud Hadfi Olympic Stadium in Oran. The firmware is designed to be compiled and flashed onto identical ESP32-based hardware, which can then be configured into one of three distinct roles.

## Project Overview

The system architecture is a star network consisting of:
- **AquaReservPro:** A sensor module to monitor water levels in reservoirs.
- **WellguardPro:** An actuator module to control well pumps.
- **HydroControl-GE (Central Unit):** A central gateway that manages all peripherals, implements the intelligent filling logic, and provides a unified web interface for monitoring and control.

Communication between modules is handled by a secure, encrypted LoRa network. Each module also hosts a web interface for local configuration and monitoring.

## Features

- **Universal Firmware:** A single codebase for all three device roles.
- **First-Time Configuration:** Easy setup via a Wi-Fi Access Point and web portal.
- **Secure LoRa Communication:** All LoRa messages are encrypted using AES-128.
- **Automatic Discovery:** Peripheral modules are automatically discovered and managed by the central unit.
- **Intelligent Filling Logic:** The central unit automatically controls pumps based on the levels of their assigned reservoirs.
- **Real-Time Web Interfaces:** Modern, responsive web UIs for each role, with live updates via WebSockets.
- **ThingsBoard Integration:** The central unit can act as a gateway to forward all telemetry data to a ThingsBoard instance.
- **Robust & Multi-threaded:** Built on FreeRTOS to ensure a non-blocking, responsive, and stable system suitable for industrial environments.

## Hardware Requirements

- **Microcontroller:** ESP32-DevKitC (or equivalent)
- **LoRa Module:** SX1278 (433MHz)
- **Peripherals:** Relays, buttons, sensors, and LEDs as specified in the `include/config.h` file.

## Software Requirements

- **Visual Studio Code** with the **PlatformIO IDE** extension.

## Compilation and Flashing

1.  **Clone the repository:**
    ```bash
    git clone https://github.com/Neo-Rak/HydroControl-GE-Universal-Industeriel.git
    ```
2.  **Open in PlatformIO:**
    Open the cloned folder in Visual Studio Code. PlatformIO should automatically recognize it as a project.
3.  **Install Libraries:**
    PlatformIO will automatically detect the required libraries in `platformio.ini` and prompt you to install them.
4.  **Compile and Upload:**
    Connect your ESP32 board via USB, select the correct COM port in PlatformIO, and click the "Upload" button (the arrow icon in the status bar).

## First-Time Configuration

This step is mandatory for any new or factory-reset device.

1.  **Connect to the Setup Wi-Fi:**
    On its first boot, the device will create a Wi-Fi network with an SSID similar to `HydroControl-Setup-XXYY`. Connect to this network.
2.  **Access the Configuration Portal:**
    Open a web browser and navigate to `http://192.168.4.1`.
3.  **Configure the Device:**
    -   **Select a Role:** Choose `AquaReservPro`, `WellguardPro`, or `HydroControl-GE`.
    -   **Assign a Name:** Give the device a unique, descriptive name (e.g., "Reservoir_Piscine", "Puit_Est").
    -   **Enter Wi-Fi Credentials:** Provide the SSID and password for your local Wi-Fi network.
4.  **Save and Reboot:**
    Click "Save and Reboot". The device will save the configuration to non-volatile memory and restart in its new operational role.

## Using the Web Interfaces

Once configured, each device will connect to your local Wi-Fi. You can find its IP address from your router's client list or via the serial monitor. Accessing this IP address in a web browser will open the role-specific interface.

-   **AquaReservPro:** Shows the current water level and allows manual fill requests.
-   **WellguardPro:** Shows the pump status and allows for manual pump control for testing.
-   **HydroControl-GE:** Provides a full dashboard of all connected modules, allows for the assignment of reservoirs to wells, and offers manual control over all pumps and configuration of the ThingsBoard gateway.
