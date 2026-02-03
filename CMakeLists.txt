cmake_minimum_required(VERSION 4.1)
project(phoenix C)

set(CMAKE_C_STANDARD 11)

include_directories(.)
include_directories(drivers)
include_directories(kernel)
include_directories(net)
include_directories(wimp)

add_executable(phoenix
        drivers/bluetooth/bluetooth.c
        drivers/nvme/nvme.c
        drivers/usb/usb_storage.c
        drivers/gpu.c
        kernel/boot.c
        kernel/irq.c
        kernel/mmu.c
        kernel/sched.c
        kernel/select.c
        kernel/signal.c
        kernel/task.c
        net/wimp.c
        wimp/wimp.c
        nvme.c)
