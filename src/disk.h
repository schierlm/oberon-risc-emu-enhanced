#ifndef DISK_H
#define DISK_H

#include "risc-io.h"

struct RISC_SPI *disk_new(const char *filename, bool paravirtual);

struct RISC_HostFS *host_fs_new(const char *directory);

struct RISC_HostTransfer *host_transfer_new();

#endif  // DISK_H
