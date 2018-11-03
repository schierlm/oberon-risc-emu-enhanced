#ifndef DISK_H
#define DISK_H

#include "risc-io.h"

struct RISC_SPI *disk_new(const char *filename);

struct RISC_HostFS *host_fs_new(const char *directory);

#endif  // DISK_H
