.SUFFIXES:

CC := gcc
LD := ld
QEMU := qemu-system-x86_64
HOST_CC := cc

BUILD := build
ISO_ROOT := $(BUILD)/iso_root
KERNEL := $(BUILD)/vostok.elf
IMAGE := $(BUILD)/vostok.iso

LIMINE_VERSION := 12.6.0
LIMINE_ARCHIVE := $(BUILD)/limine-binary.tar.xz
LIMINE_DIR := $(BUILD)/limine
LIMINE_URL := https://github.com/Limine-Bootloader/Limine/releases/download/v$(LIMINE_VERSION)/limine-binary.tar.xz
LIMINE_SHA256 := 44fb480294463c15823bba5378c955b46d15d08fc7b5a74de49fef2953d1878d
LIMINE_PROTOCOL_COMMIT := 93b5284ee279c817f3ad7a640472a38485c113c3
LIMINE_PROTOCOL_DIR := $(BUILD)/limine-protocol
LIMINE_HEADER := $(LIMINE_PROTOCOL_DIR)/include/limine.h

CFLAGS := -std=gnu11 -O2 -g -Wall -Wextra -Werror \
	-MMD -MP \
	-ffreestanding -fno-builtin -fno-stack-protector -fno-stack-check -fno-pic \
	-fno-asynchronous-unwind-tables -fno-unwind-tables -m64 -march=x86-64 \
	-mabi=sysv -mno-red-zone -mno-mmx -mno-sse -mno-sse2 -mcmodel=kernel
CPPFLAGS := -Isrc -I$(LIMINE_PROTOCOL_DIR)/include
LDFLAGS := -m elf_x86_64 -nostdlib -static -z max-page-size=0x1000 \
	--gc-sections -T linker.ld
USER_CFLAGS := -std=gnu11 -O2 -g -Wall -Wextra -Werror -ffreestanding \
	-fno-builtin -fno-stack-protector -fno-pic -fno-pie -m64 \
	-mno-red-zone -mcmodel=small -Isrc -Iuser
USER_LDFLAGS := -m elf_x86_64 -nostdlib -static -z max-page-size=0x1000 \
	--gc-sections -T user/linker.ld
USER_PROGRAMS := $(BUILD)/user/receiver.elf $(BUILD)/user/sender.elf \
	$(BUILD)/user/auditor.elf $(BUILD)/user/faulty.elf \
	$(BUILD)/user/spinner_a.elf $(BUILD)/user/spinner_b.elf \
	$(BUILD)/user/console.elf $(BUILD)/user/shell.elf \
	$(BUILD)/user/init.elf $(BUILD)/user/hello.elf \
	$(BUILD)/user/iskra_vm.elf \
	$(BUILD)/user/iskrac_native.elf
ISKRA_COMPILER := iskra/build/iskrac

COMMON_OBJECTS := \
	$(BUILD)/start.o \
	$(BUILD)/memory.o \
	$(BUILD)/serial.o \
	$(BUILD)/print.o \
	$(BUILD)/panic.o \
	$(BUILD)/boot/limine_requests.o \
	$(BUILD)/mm/page_bitmap.o \
	$(BUILD)/mm/physical.o \
	$(BUILD)/mm/address_space.o \
	$(BUILD)/kernel/capability.o \
	$(BUILD)/kernel/elf.o \
	$(BUILD)/kernel/ipc.o \
	$(BUILD)/kernel/ramfs.o \
	$(BUILD)/kernel/process.o \
	$(BUILD)/kernel/scheduler.o \
	$(BUILD)/x86/gdt.o \
	$(BUILD)/x86/gdt_load.o \
	$(BUILD)/x86/idt.o \
	$(BUILD)/x86/exceptions.o \
	$(BUILD)/x86/exception_stubs.o \
	$(BUILD)/x86/syscall_entry.o \
	$(BUILD)/x86/timer.o \
	$(BUILD)/x86/timer_entry.o \
	$(BUILD)/x86/serial_entry.o
OBJECTS := $(COMMON_OBJECTS) $(BUILD)/main.o
EXCEPTION_TEST_OBJECTS := $(COMMON_OBJECTS) $(BUILD)/exception_test.o
EXCEPTION_TEST_KERNEL := $(BUILD)/vostok-exception-test.elf
EXCEPTION_TEST_IMAGE := $(BUILD)/vostok-exception-test.iso
EXCEPTION_ISO_ROOT := $(BUILD)/exception_iso_root
HOST_PMM_TEST := $(BUILD)/tests/page_bitmap_test

-include $(OBJECTS:.o=.d) $(EXCEPTION_TEST_OBJECTS:.o=.d)

.PHONY: all run test metrics clean distclean

all: $(IMAGE)

$(BUILD):
	mkdir -p $@

$(BUILD)/%.o: src/%.c | $(BUILD) $(LIMINE_HEADER)
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

$(BUILD)/%.o: src/%.S | $(BUILD) $(LIMINE_HEADER)
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

$(ISKRA_COMPILER): $(wildcard iskra/src/*.c iskra/src/*.h) iskra/Makefile
	$(MAKE) -C iskra all

$(BUILD)/user/%.S: user/%.isk $(ISKRA_COMPILER) | $(BUILD)
	mkdir -p $(dir $@)
	$(ISKRA_COMPILER) --emit-asm $< >$@

$(BUILD)/user/%.o: $(BUILD)/user/%.S | $(BUILD)
	$(CC) $(USER_CFLAGS) -c $< -o $@

$(BUILD)/user/runtime.o: user/runtime.S | $(BUILD)
	mkdir -p $(dir $@)
	$(CC) $(USER_CFLAGS) -c $< -o $@

$(BUILD)/user/%.elf: $(BUILD)/user/%.o $(BUILD)/user/runtime.o user/linker.ld
	$(LD) $(USER_LDFLAGS) $(BUILD)/user/runtime.o $< -o $@

$(KERNEL): $(OBJECTS) linker.ld
	$(LD) $(LDFLAGS) $(OBJECTS) -o $@

$(EXCEPTION_TEST_KERNEL): $(EXCEPTION_TEST_OBJECTS) linker.ld
	$(LD) $(LDFLAGS) $(EXCEPTION_TEST_OBJECTS) -o $@

$(LIMINE_DIR)/limine: | $(BUILD)
	curl -L --fail --output $(LIMINE_ARCHIVE) $(LIMINE_URL)
	echo "$(LIMINE_SHA256)  $(LIMINE_ARCHIVE)" | sha256sum --check -
	rm -rf $(LIMINE_DIR)
	mkdir -p $(LIMINE_DIR)
	tar -xJf $(LIMINE_ARCHIVE) -C $(LIMINE_DIR) --strip-components=1
	$(MAKE) -C $(LIMINE_DIR)

$(LIMINE_HEADER): | $(BUILD)
	rm -rf $(LIMINE_PROTOCOL_DIR)
	git clone --quiet --filter=blob:none --no-checkout \
		https://github.com/Limine-Bootloader/limine-protocol.git \
		$(LIMINE_PROTOCOL_DIR)
	git -C $(LIMINE_PROTOCOL_DIR) fetch --quiet --depth 1 origin \
		$(LIMINE_PROTOCOL_COMMIT)
	git -C $(LIMINE_PROTOCOL_DIR) checkout --quiet --detach FETCH_HEAD

$(HOST_PMM_TEST): tests/page_bitmap_test.c src/mm/page_bitmap.c \
		src/mm/page_bitmap.h | $(BUILD)
	mkdir -p $(dir $@)
	$(HOST_CC) -std=c11 -O2 -g -Wall -Wextra -Wpedantic -Werror -Isrc \
		tests/page_bitmap_test.c src/mm/page_bitmap.c -o $@

$(IMAGE): $(KERNEL) $(USER_PROGRAMS) $(LIMINE_DIR)/limine limine.conf
	rm -rf $(ISO_ROOT)
	mkdir -p $(ISO_ROOT)/boot/limine $(ISO_ROOT)/EFI/BOOT
	cp $(KERNEL) $(ISO_ROOT)/boot/vostok.elf
	cp $(USER_PROGRAMS) $(ISO_ROOT)/boot/
	cp limine.conf $(LIMINE_DIR)/limine-bios.sys \
		$(LIMINE_DIR)/limine-bios-cd.bin $(LIMINE_DIR)/limine-uefi-cd.bin \
		$(ISO_ROOT)/boot/limine/
	cp $(LIMINE_DIR)/BOOTX64.EFI $(LIMINE_DIR)/BOOTIA32.EFI \
		$(ISO_ROOT)/EFI/BOOT/
	xorriso -as mkisofs -R -r -J \
		-b boot/limine/limine-bios-cd.bin -no-emul-boot \
		-boot-load-size 4 -boot-info-table -hfsplus -apm-block-size 2048 \
		--efi-boot boot/limine/limine-uefi-cd.bin \
		-efi-boot-part --efi-boot-image --protective-msdos-label \
		$(ISO_ROOT) -o $(IMAGE)
	$(LIMINE_DIR)/limine bios-install $(IMAGE)

$(EXCEPTION_TEST_IMAGE): $(EXCEPTION_TEST_KERNEL) $(USER_PROGRAMS) $(LIMINE_DIR)/limine limine.conf
	rm -rf $(EXCEPTION_ISO_ROOT)
	mkdir -p $(EXCEPTION_ISO_ROOT)/boot/limine $(EXCEPTION_ISO_ROOT)/EFI/BOOT
	cp $(EXCEPTION_TEST_KERNEL) $(EXCEPTION_ISO_ROOT)/boot/vostok.elf
	cp $(USER_PROGRAMS) $(EXCEPTION_ISO_ROOT)/boot/
	cp limine.conf $(LIMINE_DIR)/limine-bios.sys \
		$(LIMINE_DIR)/limine-bios-cd.bin $(LIMINE_DIR)/limine-uefi-cd.bin \
		$(EXCEPTION_ISO_ROOT)/boot/limine/
	cp $(LIMINE_DIR)/BOOTX64.EFI $(LIMINE_DIR)/BOOTIA32.EFI \
		$(EXCEPTION_ISO_ROOT)/EFI/BOOT/
	xorriso -as mkisofs -R -r -J \
		-b boot/limine/limine-bios-cd.bin -no-emul-boot \
		-boot-load-size 4 -boot-info-table -hfsplus -apm-block-size 2048 \
		--efi-boot boot/limine/limine-uefi-cd.bin \
		-efi-boot-part --efi-boot-image --protective-msdos-label \
		$(EXCEPTION_ISO_ROOT) -o $(EXCEPTION_TEST_IMAGE)
	$(LIMINE_DIR)/limine bios-install $(EXCEPTION_TEST_IMAGE)

run: $(IMAGE)
	$(QEMU) -M q35 -m 128M -cdrom $(IMAGE) -boot d \
		-display none -serial stdio -monitor none -no-reboot

test: $(IMAGE) $(EXCEPTION_TEST_IMAGE) $(HOST_PMM_TEST)
	$(HOST_PMM_TEST)
	QEMU_INPUT_BASE64='aGVscFxycHNccmhlbGxvXHJsc1xyY2F0IC9ob21lL21haW4uaXNrXHJydW4gaGVsbG9ccmVkaXQgL2hvbWUvdGVzdC5pc2tccmZuIG1haW4oKSAtPiBpMzIgeyBwcmludCgiTmF0aXZlIElza3JhIGZyb20gVm9zdG9rIVxcbiIpOyByZXR1cm4gMDsgfVxyXDAwNGJ1aWxkIC9ob21lL3Rlc3QuaXNrXHJydW4gL2hvbWUvYS5vdXRccm1ldHJpY3NccnNodXRkb3duXHI=' \
	QEMU_TIMEOUT=12 \
	sh scripts/qemu-smoke.sh $(IMAGE) "physical allocator:" \
		"address-space test passed" "capability check rejected receive" \
		"capability check rejected console write" \
		"IPC main: sender -> receiver, word=42" \
		"IPC audit: receiver -> auditor, word=99" \
		"terminated by exception 6" "Vostok 0.3" \
		"Hello from Iskra userspace." "PID  NAME" "preemptions" \
		"UART metrics:" "/home/main.isk" "saved" \
		"Hello from dynamically loaded ELF." "Native Iskra from Vostok!" \
		"native x86-64 ELF written to /home/a.out" \
		"entry=0x0000000000400078" \
		"run: finished successfully" \
		"all user processes completed successfully"
	sh scripts/qemu-smoke.sh $(EXCEPTION_TEST_IMAGE) \
		"exception: page fault (#PF)" "cr2: 0x00000deadbeef000"
	$(MAKE) -C iskra test

metrics: $(IMAGE) $(ISKRA_COMPILER)
	sh scripts/metrics.sh $(IMAGE) $(ISKRA_COMPILER)

clean:
	rm -rf $(ISO_ROOT) $(EXCEPTION_ISO_ROOT) $(BUILD)/*.o $(BUILD)/boot \
		$(BUILD)/kernel $(BUILD)/mm $(BUILD)/tests $(BUILD)/user $(BUILD)/x86 \
		$(KERNEL) $(IMAGE) $(EXCEPTION_TEST_KERNEL) $(EXCEPTION_TEST_IMAGE)
	$(MAKE) -C iskra clean

distclean: clean
	rm -rf $(LIMINE_DIR) $(LIMINE_ARCHIVE) $(LIMINE_PROTOCOL_DIR)
