#!/bin/sh
# Deploy image and dtb to tftp

dir_publish="/srv/tftp/stm32f769/"

cp ${BUILD_DIR}/uboot-2021.10-rc4/u-boot.bin ${BINARIES_DIR}
cp ${BUILD_DIR}/uboot-2021.10-rc4/spl/u-boot-spl.bin ${BINARIES_DIR}

echo "Copy zImage to tftp"
cp ${BINARIES_DIR}/zImage ${dir_publish}
echo "Generate and Copy uxipImage to tftp"
dd if=/dev/zero ibs=1k count=64 | tr "\000" "\377" > padding0xff
touch tmpxipfile
dd if=padding0xff of=tmpxipfile conv=notrunc
dd obs=1 seek=64 if=${BINARIES_DIR}/xipImage of=tmpxipfile conv=notrunc
cp ${BINARIES_DIR}/xipImage ${BINARIES_DIR}/xipImage.nopad64
rm -f ${BINARIES_DIR}/xipImage
cp tmpxipfile ${BINARIES_DIR}/xipImage
rm -f padding0xff
rm -f tmpxipfile
mkimage -A arm -O linux -C none -T kernel -a 0x90010040 -e 0x90010040 -n 'Linux-5.12-XIP' -x -d ${BINARIES_DIR}/xipImage ${BINARIES_DIR}/uxipImage
cp ${BINARIES_DIR}/uxipImage ${dir_publish}
echo "Copy dtb to tftp"
cp ${BUILD_DIR}/linux-5.12.11/arch/arm/boot/dts/stm32f769-disco.dtb ${BINARIES_DIR}
cp ${BUILD_DIR}/linux-5.12.11/arch/arm/boot/dts/stm32f769-disco.dtb ${dir_publish}
echo "Copy rootfs to tftp"
cp ${BINARIES_DIR}/rootfs.jffs2 ${dir_publish}
