run-out: kernel.elf
	qemu-system-aarch64 -M raspi4b -nographic -kernel kernel.elf -serial stdio -monitor none -drive file=sdcard.img,if=sd,format=raw,cache=directsync

run-monitor: kernel.elf
	qemu-system-aarch64 -M raspi4b -nographic -kernel kernel.elf -serial none -monitor stdio -drive file=sdcard.img,if=sd,format=raw,cache=directsync

run-debug: kernel.elf
	qemu-system-aarch64 -M raspi4b -nographic -kernel kernel.elf -serial none -monitor stdio -drive file=sdcard.img,if=sd,format=raw,cache=directsync -s -S

truncate-sd: 
	truncate -s 0M sdcard.img && qemu-img create -f raw sdcard.img 2G

check-sd: truncate-sd
	xxd -g 1 -l 10 sdcard.img

check-sd-hex:
	xxd -g 1 -l 10 sdcard.img

disasm: kernel.elf
	llvm-objdump -d --arch-name=aarch64 kernel.elf

clean:
	rm -f *.o *.elf kernel.img *.map

PHONY: all run-out run-monitor run-debug disasm truncate-sd clean