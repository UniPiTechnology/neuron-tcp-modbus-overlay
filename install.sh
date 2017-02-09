#!/bin/bash

ask() {
    # http://djm.me/ask
    while true; do

        if [ "${2:-}" = "Y" ]; then
            prompt="Y/n"
            default=Y
        elif [ "${2:-}" = "N" ]; then
            prompt="y/N"
            default=N
        else
            prompt="y/n"
            default=
        fi

        # Ask the question
        read -p "$1 [$prompt] " REPLY

        # Default?
        if [ -z "$REPLY" ]; then
            REPLY=$default
        fi

        # Check if the reply is valid
        case "$REPLY" in
            Y*|y*) return 0 ;;
            N*|n*) return 1 ;;
        esac

    done
}

install_overlay() {
    echo "Enabling SPI overlay"
    cd spi_overlay
    sh compile-dtc
    if ! grep -q 'dtoverlay=neuron-spi' /boot/config.txt ;then
        echo -e "$(cat /boot/config.txt) \n\n#Enable UniPi Neuron SPI overlay\ndtoverlay=neuron-spi" > /boot/config.txt
    fi
    if ! grep -q '#dtparam=spi=on' /boot/config.txt ;then
            sed -i '/dtparam=spi=on/s/^/#/g' /etc/modprobe.d/raspi-blacklist.conf
        fi
    cd ..
}

install_neuron_tcp_server() {
    mkdir /opt/neurontcp
    cd neuron_tcp_server
    echo "Compiling neurontcp server..."
    make
    echo "Installing Neuron TCP server into /opt/neurontcp/"
    make install
    #cp neuron_tcp_server /opt/neurontcp/
    #chmod +x /opt/neurontcp/neuron_tcp_server
    echo "Enabling neurontcp service for systemd"
    cp neurontcp.service /lib/systemd/system/
    cd ..
    systemctl daemon-reload
    systemctl enable neurontcp
}

install_prerequisites() {
    echo "Installing libmodbus"
    apt-get update
    apt-get install -y libmodbus-dev
}

if [ "$EUID" -ne 0 ]; then
    echo "Please run this script as root user"
    exit
fi

install_prerequisites
install_overlay
install_neuron_tcp_server

if ask "Reboot is required. Is it OK to reboot now?"; then
    reboot
else
    echo 'Remember to reboot at earliest convenience'
fi
echo ' '
