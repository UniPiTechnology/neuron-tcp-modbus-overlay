# TCP Modbus server for UniPi Neuron series:

Installation:
Get the latest release source code, eg:

 ```wget https://github.com/UniPiTechnology/neuron_tcp_modbus_overlay/archive/v1.0.0.zip```
 
Unzip it

 ```unzip v1.0.0.zip```

And run the installation scrip as root (requires make tools and libmodbus)

 ```bash neuron_tcp_modbus_overlay-1.0.0/install.sh```

## Neuron Modbus TCP Server
This daemon provides standard TCP Modbus interface for all controllers from the [UniPi Neuron series].
Handles low level communication on SPI with all of the CPUs. It also handles creation of PTYs(pseudoterminal or also virtual serial lines) in /dev/extcomm/x/y by the type of the product and is able to update firmware of each CPU.

!!Does provide access to 1-Wire!!!

See the [downloads.unipi.technology] for Modbus registers mapping and explanation.

[Evok] (the official UniPi API) uses this daemon and provides another webservices and also provides access to 1Wire sensors.

### Prerequisites:
* spi_overlay - spi overlay for the Neuron controllers to allow custom CS for SPI
* [libmodbus]

### Installation
Use the install (as noted above) script or run the Makefile 

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

Serial lines of all products can be acessed by two ways

## SPI Overlay
Creates another three SPI devices for communication with all CPUs using custom GPIOs for CS (Chip Select) pins.

### Installation
Use the install (as noted above) script or call `sh compile-dtc` to compile the overlay and copy it to /boot/overlays and add add line `dtoverlay=neuron-spi` to /boot/config.txt. Also make sure that the default SPI device is commented out in the config `#dtparam=spi=on`. 

# Todo list & known bugs
* Parity of serial communication has to be set via the Modbus uart_config register the rest (comm speed and others) can be set when opening PTY
* Currently only the standard Raspbian Jessie OS is supported
* Rewrite this into Kernel driver


[UniPi Neuron series]:http://unipi.technology
[libmodbus]:http://libmodbus.org/
[downloads.unipi.technology]:http://downloads.unipi.technology
[Evok]:https://github.com/UniPiTechnology/evok