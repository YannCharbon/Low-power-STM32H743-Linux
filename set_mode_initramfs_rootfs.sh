cp -f custom_board_def/configs/ucl-mb-802154rt-ram-rootfs-initramfs_defconfig buildroot/.config
cp -f custom_board_def/board/yanncharbon/ucl-mb-802154rt-1/linux-rootfs-initramfs.config buildroot/output/build/linux-5.4.150/.config
echo "Configs set to initramfs mode. Please run 'make' inside buildroot."
