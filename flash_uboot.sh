openocd \
		-f board/stm32h7x3i_eval.cfg \
        -c "program buildroot/output/images/u-boot.bin 0x08000000" \
		-c "reset run" -c "shutdown"
 