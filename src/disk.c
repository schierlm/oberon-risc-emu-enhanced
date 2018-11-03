#define _POSIX_C_SOURCE 200809L
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <unistd.h>
#include "disk.h"

enum DiskState {
  diskCommand,
  diskRead,
  diskWrite,
  diskWriting,
};

struct Disk {
  struct RISC_SPI spi;

  enum DiskState state;
  FILE *file;
  uint32_t offset;

  uint32_t rx_buf[128];
  int rx_idx;

  uint32_t tx_buf[128+2];
  int tx_cnt;
  int tx_idx;
};


static uint32_t disk_read(const struct RISC_SPI *spi);
static void disk_write(const struct RISC_SPI *spi, uint32_t value);
static void disk_run_command(struct Disk *disk);
static void seek_sector(FILE *f, uint32_t secnum);
static void read_sector(FILE *f, uint32_t buf[static 128]);
static void write_sector(FILE *f, uint32_t buf[static 128]);


struct RISC_SPI *disk_new(const char *filename) {
  struct Disk *disk = calloc(1, sizeof(*disk));
  disk->spi = (struct RISC_SPI) {
    .read_data = disk_read,
    .write_data = disk_write
  };

  disk->state = diskCommand;

  if (filename) {
    disk->file = fopen(filename, "rb+");
    if (disk->file == 0) {
      fprintf(stderr, "Can't open file \"%s\": %s\n", filename, strerror(errno));
      exit(1);
    }

    // Check for filesystem-only image, starting directly at sector 1 (DiskAdr 29)
    read_sector(disk->file, &disk->tx_buf[0]);
    disk->offset = (disk->tx_buf[0] == 0x9B1EA38D) ? 0x80002 : 0;
  }

  return &disk->spi;
}

static void disk_write(const struct RISC_SPI *spi, uint32_t value) {
  struct Disk *disk = (struct Disk *)spi;
  disk->tx_idx++;
  switch (disk->state) {
    case diskCommand: {
      if ((uint8_t)value != 0xFF || disk->rx_idx != 0) {
        disk->rx_buf[disk->rx_idx] = value;
        disk->rx_idx++;
        if (disk->rx_idx == 6) {
          disk_run_command(disk);
          disk->rx_idx = 0;
        }
      }
      break;
    }
    case diskRead: {
      if (disk->tx_idx == disk->tx_cnt) {
        disk->state = diskCommand;
        disk->tx_cnt = 0;
        disk->tx_idx = 0;
      }
      break;
    }
    case diskWrite: {
      if (value == 254) {
        disk->state = diskWriting;
      }
      break;
    }
    case diskWriting: {
      if (disk->rx_idx < 128) {
        disk->rx_buf[disk->rx_idx] = value;
      }
      disk->rx_idx++;
      if (disk->rx_idx == 128) {
        write_sector(disk->file, &disk->rx_buf[0]);
      }
      if (disk->rx_idx == 130) {
        disk->tx_buf[0] = 5;
        disk->tx_cnt = 1;
        disk->tx_idx = -1;
        disk->rx_idx = 0;
        disk->state = diskCommand;
      }
      break;
    }
  }
}

static uint32_t disk_read(const struct RISC_SPI *spi) {
  struct Disk *disk = (struct Disk *)spi;
  uint32_t result;
  if (disk->tx_idx >= 0 && disk->tx_idx < disk->tx_cnt) {
    result = disk->tx_buf[disk->tx_idx];
  } else {
    result = 255;
  }
  return result;
}

static void disk_run_command(struct Disk *disk) {
  uint32_t cmd = disk->rx_buf[0];
  uint32_t arg = (disk->rx_buf[1] << 24)
    | (disk->rx_buf[2] << 16)
    | (disk->rx_buf[3] << 8)
    | disk->rx_buf[4];

  switch (cmd) {
    case 81: {
      disk->state = diskRead;
      disk->tx_buf[0] = 0;
      disk->tx_buf[1] = 254;
      seek_sector(disk->file, arg - disk->offset);
      read_sector(disk->file, &disk->tx_buf[2]);
      disk->tx_cnt = 2 + 128;
      break;
    }
    case 88: {
      disk->state = diskWrite;
      seek_sector(disk->file, arg - disk->offset);
      disk->tx_buf[0] = 0;
      disk->tx_cnt = 1;
      break;
    }
    default: {
      disk->tx_buf[0] = 0;
      disk->tx_cnt = 1;
      break;
    }
  }
  disk->tx_idx = -1;
}

static void seek_sector(FILE *f, uint32_t secnum) {
  if (f) {
    fseek(f, secnum * 512, SEEK_SET);
  }
}

static void read_sector(FILE *f, uint32_t buf[static 128]) {
  uint8_t bytes[512] = { 0 };
  if (f) {
    fread(bytes, 512, 1, f);
  }
  for (int i = 0; i < 128; i++) {
    buf[i] = (uint32_t)bytes[i*4+0]
      | ((uint32_t)bytes[i*4+1] << 8)
      | ((uint32_t)bytes[i*4+2] << 16)
      | ((uint32_t)bytes[i*4+3] << 24);
  }
}

static void write_sector(FILE *f, uint32_t buf[static 128]) {
  if (f) {
    uint8_t bytes[512];
    for (int i = 0; i < 128; i++) {
      bytes[i*4+0] = (uint8_t)(buf[i]      );
      bytes[i*4+1] = (uint8_t)(buf[i] >>  8);
      bytes[i*4+2] = (uint8_t)(buf[i] >> 16);
      bytes[i*4+3] = (uint8_t)(buf[i] >> 24);
    }
    fwrite(bytes, 512, 1, f);
  }
}


#define MAX_HOSTFS_FILES 4096
#define HOSTFS_SECTOR_MAGIC 290000000

struct HostFS {
  struct RISC_HostFS hostfs;
  const char* dirname;
  DIR *directory;
  char* allocated_names[MAX_HOSTFS_FILES];
  char* allocated_full_names[MAX_HOSTFS_FILES];
  uint32_t allocated_names_size;
  char current_prefix[33];
};


static void hostfs_write(const struct RISC_HostFS *hostfs_hostfs, uint32_t value, uint32_t *ram);
static uint32_t hostfs_search_file(struct HostFS *hostfs, char *filename);

struct RISC_HostFS *host_fs_new(const char *directory) {
  struct HostFS *hostfs = calloc(1, sizeof(*hostfs));
  hostfs->hostfs = (struct RISC_HostFS) {
    .write = hostfs_write
  };

  hostfs->dirname = directory;
  hostfs->directory = opendir(directory);
  if (hostfs->directory == 0) {
    fprintf(stderr, "Can't open directory \"%s\": %s\n", directory, strerror(errno));
    exit(1);
  }

  return &hostfs->hostfs;
}

static void hostfs_write(const struct RISC_HostFS *hostfs_hostfs, uint32_t value, uint32_t *ram) {
  struct HostFS *hostfs = (struct HostFS *)hostfs_hostfs;
  uint32_t offset = value / 4;
  switch(ram[offset]) {
    case 0: { // FileDir.Search
      ram[offset+1] = hostfs_search_file(hostfs, (char*) (ram+offset+2));
      break;
    }
    case 1: { // FileDir.Enumerate Start
      strncpy(hostfs->current_prefix,  (char*) (ram+offset+2), sizeof(hostfs->current_prefix)-1);
      rewinddir(hostfs->directory);
      // FALL THROUGH
    }
    case 2: { // FileDir.Enumerate Next
      struct dirent *entry = readdir(hostfs->directory);
      while(entry != NULL) {
        if (strncmp(hostfs->current_prefix, entry->d_name, strlen(hostfs->current_prefix)) == 0 && entry->d_name[0] != '~' && entry->d_name[0] != '.') {
          break;
        }
        entry = readdir(hostfs->directory);
      }
      if (entry == NULL) {
        ram[offset + 1] = 0;
      } else {
        ram[offset+1] = hostfs_search_file(hostfs, entry->d_name);
        strcpy((char*) (ram+offset+2), entry->d_name);
      }
      break;
    }
    case 3: { // FileDir.GetAttributes / System.List
      uint32_t sector = ram[offset + 1] - HOSTFS_SECTOR_MAGIC;
      if (sector < hostfs->allocated_names_size && hostfs->allocated_names[sector] != NULL) {
        struct stat buf;
        if (stat(hostfs->allocated_full_names[sector], &buf) == 0) {
          struct tm *ft = localtime(&buf.st_mtime);
          ram[offset + 2] = ft->tm_sec + ft->tm_min * 0x40 + ft->tm_hour * 0x1000 + ft->tm_mday * 0x20000 + ft->tm_mon * 0x400000 + (ft->tm_year % 100) * 0x4000000;
          ram[offset + 3] = (uint32_t) buf.st_size;
        }
      }
      break;
    }
    case 4: { // FileDir.Insert
      char* fileName = (char*) (ram + offset + 2);
      uint32_t sector = ram[offset + 1] - HOSTFS_SECTOR_MAGIC;
      char newFullName[256];
      if (sector < hostfs->allocated_names_size && hostfs->allocated_names[sector] != NULL && hostfs->allocated_names[sector][0] == '~' && snprintf(newFullName, sizeof(newFullName), "%s/%s", hostfs->dirname, fileName) < (int) sizeof(newFullName)) {
        if (access(newFullName, F_OK) != -1) {
          int pos = -1;
          for (uint32_t i = 0; i < hostfs->allocated_names_size; i++) {
            if (hostfs->allocated_names[i] != NULL && strcmp(hostfs->allocated_names[i], fileName) == 0) {
              pos = i;
              break;
            }
          }
          if (pos == -1) {
            unlink(newFullName);
          } else {
           char template[256];
           snprintf(template, sizeof(template), "%s/~OvW~XXXXXX", hostfs->dirname);
           close(mkstemp(template));
           unlink(template);
           rename(newFullName, template);
           free(hostfs->allocated_names[pos]);
           free(hostfs->allocated_full_names[pos]);
           hostfs->allocated_names[pos] = strdup("~OvW");
           hostfs->allocated_full_names[pos] = strdup(template);
          }
        }
        rename(hostfs->allocated_full_names[sector], newFullName);
        free(hostfs->allocated_names[sector]);
        free(hostfs->allocated_full_names[sector]);
        hostfs->allocated_names[sector] = strdup(fileName);
        hostfs->allocated_full_names[sector] = strdup(newFullName);
      }
      break;
    }
    case 5: { // FileDir.Delete
      int sector = hostfs_search_file(hostfs, (char*) (ram+offset+2));
      ram[offset + 1] = sector;
      if (sector != 0) {
        char template[256];
        snprintf(template, sizeof(template), "%s/~Del~%s_XXXXXX", hostfs->dirname, (char*) (ram+offset+2));
        close(mkstemp(template));
        unlink(template);
        rename(hostfs->allocated_full_names[sector - HOSTFS_SECTOR_MAGIC], template);
        free(hostfs->allocated_names[sector - HOSTFS_SECTOR_MAGIC]);
        free(hostfs->allocated_full_names[sector - HOSTFS_SECTOR_MAGIC]);
        hostfs->allocated_names[sector - HOSTFS_SECTOR_MAGIC] = strdup("~Del");
        hostfs->allocated_full_names[sector - HOSTFS_SECTOR_MAGIC] = strdup(template);
      }
      break;
    }
    case 6: { // Files.New
      char template[256];
      snprintf(template, sizeof(template), "%s/~New~%s_XXXXXX", hostfs->dirname, (char*) (ram+offset+2));
      close(mkstemp(template));
      ram[offset + 1] = hostfs_search_file(hostfs, strrchr(template, '/') + 1);
      break;
    }
    case 7: { // Files.ReadBuf
      uint32_t sector = ram[offset + 1] - HOSTFS_SECTOR_MAGIC;
      if (sector < hostfs->allocated_names_size) {
        FILE *fd = fopen(hostfs->allocated_full_names[sector], "rb");
        fseek(fd, ram[offset+2], SEEK_SET);
        fread(&ram[ram[offset + 4]/4], ram[offset + 3], 1, fd);
        fclose(fd);
      }
      break;
    }
    case 8: { // Files.WriteBuf
      uint32_t sector = ram[offset + 1] - HOSTFS_SECTOR_MAGIC;
      if (sector < hostfs->allocated_names_size) {
        FILE *fd = fopen(hostfs->allocated_full_names[sector], "rb+");
        fseek(fd, ram[offset+2], SEEK_SET);
        fwrite(&ram[ram[offset + 4]/4], ram[offset + 3], 1, fd);
        fclose(fd);
      }
      break;
    }
  }
}

static uint32_t hostfs_search_file(struct HostFS *hostfs, char *filename) {
  for (uint32_t i = 0; i < hostfs->allocated_names_size; i++) {
    if (hostfs->allocated_names[i] != NULL && strcmp(hostfs->allocated_names[i], filename) == 0) {
      return HOSTFS_SECTOR_MAGIC + i;
    }
  }
  char fullname[256];
  if (hostfs->allocated_names_size < MAX_HOSTFS_FILES - 1 && snprintf(fullname, sizeof(fullname), "%s/%s", hostfs->dirname, filename) < (int) sizeof(fullname) && access(fullname, F_OK) != -1) {
    if (hostfs->allocated_names_size % 29 == 0)
      hostfs->allocated_names_size++;
    hostfs->allocated_names[hostfs->allocated_names_size] = strdup(filename);
    hostfs->allocated_full_names[hostfs->allocated_names_size] = strdup(fullname);
    hostfs->allocated_names_size++;
    return HOSTFS_SECTOR_MAGIC + hostfs->allocated_names_size - 1;
  }
  return 0;
}
