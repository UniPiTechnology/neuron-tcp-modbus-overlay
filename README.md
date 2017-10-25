# TCP Modbus server for the Neuron series of UniPi devices

Installation:
Get the latest release source code, e.g. from:

 ```wget https://github.com/UniPiTechnology/neuron_tcp_modbus_overlay/archive/v1.0.1.zip```
 
Unzip it:

 ```unzip v1.0.1.zip```

And run the installation script as root (requires make tools and libmodbus):

 ```cd neuron_tcp_modbus_overlay-1.0.1 ```
 
  ```bash install.sh ```

## Neuron Modbus TCP Server
This daemon provides standard TCP Modbus interface for all controllers from the [UniPi Neuron series].
Handles low level communication on SPI with all of the embedded CPUs. Also handles creation of PTYs (pseudo-terminal or also virtual serial lines) in /dev/extcomm/x/y by the type of the product and is able to update the firmware of every CPU.

See the [downloads.unipi.technology] for mapping and explanation of Modbus registers.

[Evok] (the official UniPi API) uses this daemon and provides other webservices as well as access to 1Wire sensors.

### Prerequisites:
* spi_overlay - spi overlay for the Neuron controllers to allow custom CS for SPI
* [libmodbus]

### Installation:
Use the install script (as described above) or run the provided Makefile 

### Usage:
./neuron_tcp_server [-v[v]] [-d] [-l listen_address] [-p port] [-s dev1[,dev2[,dev3]]] [-i gpio1[,gpio2[,gpio3]]] [-b [baud1,..] [-f firmwaredir] [-c]
  --verbose	 
  --daemon	 
  --listen=...	 
  --port=...	 
  --spidev=...	 
  --interrupts=...	 
  --bauds=...	 
  --fwdir=...	 
  --check-firmware

Or see the attached neurontcp.service for systemd init system.

Serial lines of all products can be accessed in two ways:

## SPI Overlay
Creates another three SPI devices for communication with all CPUs using custom GPIOs for CS (Chip Select) pins.

### Installation
Use the install script (as described above) or call `sh compile-dtc` to compile the overlay and copy it to /boot/overlays, then add line `dtoverlay=neuron-spi` to /boot/config.txt. Also make sure that the default SPI device is commented out in the config `#dtparam=spi=on`. 

# To-do list & known bugs
* Parity of serial communication has to be set via the Modbus uart_config register; the rest of serial configuration (comm speed etc.) can be set when opening the PTY
* Currently only the standard Raspbian Jessie OS is supported
* Rewrite this into Kernel driver


[UniPi Neuron series]:http://unipi.technology
[libmodbus]:http://libmodbus.org/
[downloads.unipi.technology]:http://downloads.unipi.technology
[Evok]:https://github.com/UniPiTechnology/evok
