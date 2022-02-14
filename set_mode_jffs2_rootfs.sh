cp -f custom_board_def/configs/ucl-mb-802154rt-ram-rootfs-jffs2_defconfig buildroot/.config
cp -f custom_board_def/board/yanncharbon/ucl-mb-802154rt-1/linux-rootfs-jffs2.config buildroot/output/build/linux-5.4.150/.config
echo "Configs set to jffs2 mode. Please run 'make' inside buildroot. Please manually enable 'qspi' flash node inside device-tree."
