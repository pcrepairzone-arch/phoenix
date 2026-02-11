cmake_minimum_required(VERSION 3.16)
project(phoenix C ASM)

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

set(CMAKE_C_COMPILER aarch64-none-elf-gcc)
set(CMAKE_ASM_COMPILER aarch64-none-elf-as)
set(CMAKE_LINKER aarch64-none-elf-ld)
set(CMAKE_OBJCOPY aarch64-none-elf-objcopy)

set(CMAKE_C_FLAGS "-Wall -O2 -ffreestanding -mcpu=cortex-a72 -mgeneral-regs-only -nostdlib -fno-builtin -Ikernel -I.")
set(CMAKE_ASM_FLAGS "-mcpu=cortex-a72")

# All source files
set(SOURCES
    kernel/boot.c
    kernel/kernel.c
    kernel/sched.c
    kernel/task.c
    kernel/signal.c
    kernel/mmu.c
    kernel/pipe.c
    kernel/select.c
    kernel/irq.c
    kernel/timer.c
    kernel/pci.c
    kernel/vfs.c
    kernel/filecore.c
    kernel/dl.c

    drivers/nvme/nvme.c
    drivers/usb/usb_storage.c
    drivers/bluetooth/bluetooth.c
    drivers/gpu/gpu.c
    drivers/mmc/mmc.c

    net/tcpip.c
    net/socket.c
    net/ipv4.c
    net/ipv6.c
    net/tcp.c
    net/udp.c
    net/arp.c

    wimp/wimp.c
    wimp/window.c
    wimp/event.c
    wimp/menu.c

    apps/paint.c
    apps/netsurf.c
)

add_executable(phoenix ${SOURCES})

# Linker script
set_target_properties(phoenix PROPERTIES
    LINK_FLAGS "-T ${CMAKE_SOURCE_DIR}/kernel/linker.ld -nostdlib -static"
)

# Generate final .img
add_custom_command(TARGET phoenix POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O binary phoenix phoenix64.img
    COMMENT "Creating bootable image: phoenix64.img"
)