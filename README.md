# AirU V2 PoE Firmware

# IMPORTANT NOTE!!!!!
The AirU PoE board connects GPIO12 to a pull-up resistor, which automatically sets the flash voltage to 1.8v. In order to use this firmware, the VDD_SDIO efuse of the ESP32 must be set using the following command:
```
espefuse.py set_flash_voltage 3.3V
```
Read more about efuses here: https://github.com/espressif/esptool/wiki/espefuse


# Workspace Setup
To setup all the tools and workspace required, just follow the [Espressif Getting Started Guide.](https://docs.espressif.com/projects/esp-idf/en/latest/get-started/) 

## Notes for installing on Windows
First of all, I do not recommend working with the ESP32 in a Windows environment. You need to install and use MingW32, and it is extremely slow. It is much better to run a virtual machine (VirtualBox, or VMWare Workstation through the UofU). When the ESP32 is connected to your computer, go to Device Manager and locate the device. If it is showing up as an Unkown Device, have Windows try to automatically install the driver, it should be able to do so. Check which COM port the ESP32 is connected to by looking in Device Manager. In the `Serial Flash Config` page of `make menuconfig`, the port is just the device COM port, like `COM3`, whereas in Linux its something like `/dev/ttyS2`, and in MacOS it looks like `/dev/cu.usb1440`. 

# Purpose
This firmware should be run on the AirU PoE board. For more information about this board, go here: https://github.com/aqandu/airu-board/tree/poe_v2

# Database:
This project sends data to the google cloud database using MQTT commands. To change the google cloud database that your board is uploading to, see lines 54-64 of the mqtt_if.c file.


