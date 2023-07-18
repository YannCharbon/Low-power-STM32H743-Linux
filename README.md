# Repository content
This repository contains software and hardware designs of a low-power Linux board based on a STM32H743 microcontroller.

# Software design
The Linux environment is deployed through __buildroot__. The `custom_board_def` folder contains all the custom definitions, packages, configs and patches for the board.

There are three base configs for Linux that can be used

- Linux runs in RAM with a volatile Initramfs rootfs
- Linux runs in RAM with a non-volatile JFFS2 rootfs
- Linux runs in XIP mode from SPI-flash with a read-only CRAMFS rootfs

## Installation, configuration
First of all, run the `init_tftp.sh` to install, configure and create the TFTP server and directory. The TFTP folder for the project is `/srv/tftp/stm32h743`.

To generate Linux distribution using Buildroot, run `make BR2_EXTERNAL=<path_to_this_git_repo>/custom_board_def`.

Once Buildroot has finished generating the distribution, simply set the desired configuration by running one of the following script :

- `set_mode_initramfs_rootfs.sh`
- `set_mode_jffs2_rootfs.sh`
- `set_mode_xip_rootfs.sh`

After this, run `make` again inside the buildroot repository.

The output files can be found under `buildroot/output/images`.

To communicate with the board, a serial terminal such as `minicom` must be used. The board will show up under `/dev/ttyACMx`.

## Deployment

The `flash_uboot.sh` script can be used to flash the bootloader (U-Boot) to the STM32H743 internal flash over __JTAG__.

Once U-Boot is started, the following commands can be used :

- `run netboot` : directly download Linux from TFTP and boot it inside RAM (initramfs required)
- `run updatespiram` : update Linux with JFFS2 rootfs from TFTP inside SPI-flash
- `run spibootram` : launch Linux with JFFS2 rootfs from SPI-flash
- `run updatespixip` : update Linux with CRAMFS rootfs from TFTP inside SPI-flash
- `run spibootxip` : launch Linux with CRAMFS rootfs from SPI-flash in XIP mode

To show all the available commands type `pri` inside U-Boot.

# Hardware design

The hardware design is split in two different boards. The `UCL-CPUM-H7-x` board, which is the CPU module (MCU, RAM, flash) and the `UCL-MB-802154RT-x` board, which is the motherboard (USB, Ethernet, power-supply, IEEE 802.15.4 interface). The `-x` corresponds to the version of the design.

# Applications

The respository also contains a cross-compilation environment to create user-space applications for the system. This can be found in the `Application folder`.

# Pre-built images

The `pre-built_images` folder contains ready-to-use images for U-Boot, Linux and the rootfs.
