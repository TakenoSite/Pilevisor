#ifndef MVMM_MEMMAP_H
#define MVMM_MEMMAP_H

#define UARTBASE    0x09000000
#define RTCBASE     0x09010000
#define GPIOBASE    0x09030000
#define GICDBASE    0x08000000
#define GITSBASE    0x08080000
#define GICRBASE    0x080a0000
#define VIRTIO0     0x0a000000

#define PCIE_MMIO_BASE       0x10000000
#define PCIE_HIGH_MMIO_BASE  0x8000000000ULL
#define PCIE_ECAM_BASE       0x4010000000ULL

#define VMMBASE     0x40000000

#define PHYSIZE     (256*1024*1024)     /* 256 MB */
#define PHYEND      (VMMBASE+PHYSIZE)

#endif
