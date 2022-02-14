cp -f custom_board_def/configs/ucl-mb-802154rt-xip-rootfs-cramfs_defconfig buildroot/.config
cp -f custom_board_def/board/yanncharbon/ucl-mb-802154rt-1/linux-xip-rootfs-cramfs.config buildroot/output/build/linux-5.4.150/.config
echo "Configs set to XIP mode. Please run 'make' inside buildroot. Please manually disable 'qspi' flash node inside device-tree."
