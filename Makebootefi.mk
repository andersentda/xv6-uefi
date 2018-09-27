ARCH            = ia32
#$(shell uname -m | sed s,i[3456789]86,ia32,)

# Try to infer the correct QEMU
ifndef QEMU
QEMU = $(shell if which qemu > /dev/null; \
	then echo qemu; exit; \
	elif which qemu-system-i386 > /dev/null; \
	then echo qemu-system-i386; exit; \
	else \
	qemu=/Applications/Q.app/Contents/MacOS/i386-softmmu.app/Contents/MacOS/i386-softmmu; \
	if test -x $$qemu; then echo $$qemu; exit; fi; fi; \
	echo "***" 1>&2; \
	echo "*** Error: Couldn't find a working QEMU executable." 1>&2; \
	echo "*** Is the directory containing the qemu binary in your PATH" 1>&2; \
	echo "*** or have you tried setting the QEMU variable in Makefile?" 1>&2; \
	echo "***" 1>&2; exit 1)
endif


EFIOBJS            = bootefi.o 

EFIINC          = /usr/include/efi
EFIINCS         = -I$(EFIINC) -I$(EFIINC)/$(ARCH) -I$(EFIINC)/protocol
LIB             = /usr/lib32
EFILIB          = /usr/lib32
EFI_CRT_OBJS    = $(EFILIB)/crt0-efi-$(ARCH).o
EFI_LDS         = $(EFILIB)/elf_$(ARCH)_efi.lds

QEMUOPTS = -drive file=fs.img,index=1,media=disk,format=raw -bios OVMF.fd -drive file=hdiimage.bin,index=0,media=disk,format=raw  -smp $(CPUS) -m 512 $(QEMUEXTRA)

efi: bootefi.efi kernel
	cp bootefi.efi BOOTIA32.EFI
	dd if=/dev/zero of=xv6.img bs=1k count=1440
	mformat -i xv6.img -f 1440 ::
	mmd -i xv6.img ::/EFI
	mmd -i xv6.img ::/EFI/BOOT
	mcopy -i xv6.img BOOTIA32.EFI ::/EFI/BOOT
	mcopy -i xv6.img kernel ::/
	mkgpt -o hdiimage.bin --image-size 4096 --part xv6.img --type system

efimemfs: bootefi.efi kernelmemfs
	cp bootefi.efi BOOTIA32.EFI
	dd if=/dev/zero of=xv6memfs.img bs=1k count=1440
	mformat -i xv6memfs.img -f 1440 ::
	mmd -i xv6memfs.img ::/EFI
	mmd -i xv6memfs.img ::/EFI/BOOT
	mcopy -i xv6memfs.img BOOTIA32.EFI ::/EFI/BOOT
	mcopy -i xv6memfs.img kernelmemfs ::/kernel
	mkgpt -o hdiimage.bin --image-size 4096 --part xv6memfs.img --type system



qemu-efi: efi fs.img
	$(QEMU) $(QEMUOPTS) -nographic -serial mon:stdio

qemu-efi-gdb: efi fs.img .gdbinit
	$(QEMU) $(QEMUOPTS) -nographic -serial mon:stdio -S $(QEMUGDB) 

qemu-efimemfs: efimemfs 
	$(QEMU) -bios OVMF.fd -drive file=hdiimage.bin,index=0,media=disk,format=raw  -smp $(CPUS) -m 512 $(QEMUEXTRA) -nographic -serial mon:stdio

qemu-efimemfs-gdb: efimemfs .gdbinit
	$(QEMU) -bios OVMF.fd -drive file=hdiimage.bin,index=0,media=disk,format=raw  -smp $(CPUS) -m 512 $(QEMUEXTRA) -nographic -serial mon:stdio -S $(QEMUGDB) 




EFI_CFLAGS          = $(EFIINCS) -g -fno-stack-protector -fpic \
		  -fshort-wchar -mno-red-zone -Wall
ifeq ($(ARCH),x86_64)
  EFI_CFLAGS += -DEFI_FUNCTION_WRAPPER
else
  EFI_CFLAGS += -m32
endif

EFI_LDFLAGS         = -g -nostdlib -znocombreloc -T $(EFI_LDS) -shared \
		  -Bsymbolic -L $(EFILIB) -L $(LIB) $(EFI_CRT_OBJS) 

bootefi.o: bootefi.c
	$(CC) $(EFI_CFLAGS) -o bootefi.o -c bootefi.c

bootefi.so: $(EFIOBJS)
	ld $(EFI_LDFLAGS) $(EFIOBJS) -o $@ -lefi -lgnuefi
#	$(OBJDUMP) -S bootefi.so > bootefi.asm
#	$(OBJDUMP) -t bootefi.so | sed '1,/SYMBOL TABLE/d; s/ .* / /; /^$$/d' > bootefi.sym

%.efi: %.so
	objcopy -j .text -j .sdata -j .data -j .dynamic \
		-j .dynsym  -j .rel -j .rela -j .reloc \
		--target=efi-app-$(ARCH) $^ $@

