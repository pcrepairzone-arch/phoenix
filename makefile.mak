# Makefile for RISC OS Phoenix
# Simple and reliable build for Windows + MSYS2

CC = aarch64-none-elf-gcc
AS = aarch64-none-elf-as
LD = aarch64-none-elf-ld
OBJCOPY = aarch64-none-elf-objcopy

CFLAGS = -Wall -O2 -ffreestanding -mcpu=cortex-a72 -mgeneral-regs-only \
         -nostdlib -fno-builtin -Ikernel -I. -Idrivers -Inet -Iwimp
ASFLAGS = -mcpu=cortex-a72
LDFLAGS = -T kernel/linker.ld -nostdlib -static

OBJS = \
    kernel/boot.o \
    kernel/kernel.o \
    kernel/sched.o \
    kernel/task.o \
    kernel/signal.o \
    kernel/mmu.o \
    kernel/pipe.o \
    kernel/select.o \
    kernel/irq.o \
    kernel/timer.o \
    kernel/pci.o \
    kernel/vfs.o \
    kernel/filecore.o \
    kernel/dl.o \
    kernel/blockdriver.o \
    kernel/spinlock.o \
    drivers/nvme/nvme.o \
    drivers/usb/usb_storage.o \
    drivers/bluetooth/bluetooth.o \
    drivers/gpu/gpu.o \
    drivers/mmc/mmc.o \
    net/tcpip.o \
    net/socket.o \
    net/ipv4.o \
    net/ipv6.o \
    net/tcp.o \
    net/udp.o \
    net/arp.o \
    wimp/wimp.o \
    wimp/window.o \
    wimp/event.o \
    wimp/menu.o \
    apps/paint.o \
    apps/netsurf.o

TARGET = phoenix64.img

all: $(TARGET)

$(TARGET): kernel.elf
    $(OBJCOPY) -O binary kernel.elf $(TARGET)

kernel.elf: $(OBJS)
    $(LD) $(LDFLAGS) $(OBJS) -o kernel.elf

%.o: %.c
    $(CC) $(CFLAGS) -c $< -o $@

%.o: %.S
    $(AS) $(ASFLAGS) $< -o $@

clean:
    rm -f *.o */*.o */*/*.o kernel.elf $(TARGET)

.PHONY: all clean