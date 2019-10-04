# AirU V2 Repository

# Workspace Setup
To setup all the tools and workspace required, just follow the [Espressif Getting Started Guide.](https://docs.espressif.com/projects/esp-idf/en/latest/get-started/) 

## Notes for installing on Windows
First of all, I do not recommend working with the ESP32 in a Windows environment. You need to install and use MingW32, and it is extremely slow. It is much better to run a virtual machine (VirtualBox, or VMWare Workstation through the UofU). When the ESP32 is connected to your computer, go to Device Manager and locate the device. If it is showing up as an Unkown Device, have Windows try to automatically install the driver, it should be able to do so. Check which COM port the ESP32 is connected to by looking in Device Manager. In the `Serial Flash Config` page of `make menuconfig`, the port is just the device COM port, like `COM3`, whereas in Linux its something like `/dev/ttyS2`, and in MacOS it looks like `/dev/cu.usb-serial1440`. 

# Project Setup
Several files and settings are excluded using the `.gitignore` file and will have to be added manually until we have `git secret` set up. Make a `cert` directory and add the MQTT broker certificate (PEM format) inside it. Open menuconfig and update the following:
- go to `~/esp/esp-idf/components/mqtt/mqtt_client.c` and replace line 885 with this line:

`xEventGroupWaitBits(client->status_bits, STOPPED_BIT, false, true, 1000 / portTICK_PERIOD_MS /* portMAX_DELAY */);`

meaning just replace `portMAX_DELAY` with `1000 / portTICK_PERIOD_MS`. There's currently an error with the mqtt client that will hang here if you attempt to connect and there was a problem, then you try to destroy (stop) the client. 
- Serial Flash Config
    - Update the default serial port if nececessary (`/dev/cu.usbserial-1410` on OSX)
    - Flash Size (4 MB)
    - After flashing (Stay in bootloader)
- AirU Configuration -> Update all appropriate
- Partition Table -> Partition Table (Factory app, two OTA definitions)
- Component Config -> FAT Filesystem support -> Long filename support (Long filename buffer in heap)

- Application Make menuconfig setup

Make menuconfig > AirU Configuration

CONFIG_MQTT_HOST="air.eng.utah.edu"

CONFIG_MQTT_USERNAME="<private>"

CONFIG_MQTT_PASSWORD="<private>"

CONFIG_INFLUX_MEASUREMENT_NAME="<private>"

CONFIG_MQTT_ROOT_TOPIC="<private>"

CONFIG_MQTT_DATA_PUB_TOPIC="<private>"

CONFIG_MQTT_SUB_ALL_TOPIC="<private>"

CONFIG_DATA_UPLOAD_PERIOD=60

CONFIG_USE_SD=y

CONFIG_SD_DATA_STORE=y

CONFIG_SD_CARD_DEBUG=y




