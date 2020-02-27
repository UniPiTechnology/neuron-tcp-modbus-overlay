[![deprecated](http://badges.github.io/stability-badges/dist/deprecated.svg)](http://github.com/badges/stability-badges)
# DEPRECATED
This project has been replaced by UniPi kernel module. Please check the UniPi [kb] and/or the [Deb repository] wher the module can be found.

# Legacy TCP Modbus server for the Neuron series of UniPi devices

*IMPORTANT NOTE: See our [Evok] repository for up-to-date installation instructions; this repository is maintained solely to provide legacy support*

Installation:

Check out the source code for the latest release, e.g. by calling:

 ```wget https://github.com/UniPiTechnology/neuron-tcp-modbus-overlay/archive/v.1.0.3.zip```
 
Unzip it:

 ```unzip v.1.0.3.zip```

And run the installation script as root (requires make and libmodbus):

 ```cd neuron-tcp-modbus-overlay-1.0.3 ```
 
  ```bash install.sh ```

## Components

### Neuron Modbus TCP Server
This daemon provides a userspace implementation of a standard TCP Modbus interface for all controllers from the [UniPi Neuron series].
It can handle low level communication on SPI with all embedded boards with all embedded Neuron CPIs. It also handles the creation of PTYs (pseudo-terminal (also known as virtual) serial lines) in /dev/extcomm/x/y and is able to update our firmware.

See the [downloads.unipi.technology] for mapping and explanation of Modbus registers.

[Evok] (the official UniPi API) uses this daemon and provides other webservices as well as access to 1Wire sensors.

#### Prerequisites:
* spi_overlay - spi overlay for the Neuron controllers to allow custom CS for SPI
* [libmodbus]

#### Installation:
Use the install script (as described above) or run the provided Makefile 

#### Usage:
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

### SPI Overlay
Creates another three SPI devices for communication with all CPUs using custom GPIOs for CS (Chip Select) pins.

#### Installation
Use the install script (as described above) or call `sh compile-dtc` to compile the overlay and copy it to /boot/overlays, then add line `dtoverlay=neuron-spi` to /boot/config.txt. Also make sure that the default SPI device is commented out in the config `#dtparam=spi=on`. 

## Other notes & known issues
* Parity of serial communication has to be set via the Modbus uart_config register; the rest of serial configuration (comm speed etc.) can be set when opening the PTY
* Have a look instead at our [kernel driver source code] if you wish to try to integrate our more in-depth interface into your image.

[UniPi Neuron series]:https://unipi.technology
[libmodbus]:http://libmodbus.org/
[downloads.unipi.technology]:https://kb.unipi.technology
[Evok]:https://github.com/UniPiTechnology/evok
[kernel driver source code]:https://git.unipi.technology/UniPi/unipi-kernel
[kb]:https://kb.unipi.technology
[Deb repository]:https://repo.unipi.technology/debian/
