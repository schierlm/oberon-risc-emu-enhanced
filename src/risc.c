#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include "risc.h"
#include "risc-fp.h"


// Our memory layout is slightly different from the FPGA implementation:
// The FPGA uses a 20-bit address bus and thus ignores the top 12 bits,
// while we use all 32 bits. This allows us to have more than 1 megabyte
// of RAM and/or a 16 color framebuffer.
//
// In the default configuration, the emulator is compatible with the
// FPGA system. But If the user requests more memory, we move the
// framebuffer to make room for a larger Oberon heap. This requires a
// custom Display.Mod.


#define DefaultMemSize      0x00100000
#define DefaultDisplayStart 0x000E7F00

#define ROMStart     0xFFFFF800
#define ROMWords     512
#define IOStart      0xFFFFFFC0
#define PaletteStart 0xFFFFFB00

#define HW_ENUM_ID(a,b,c,d) (((a) << 24) | ((b) << 16) | ((c) << 8) | (d))

struct RISC {
  uint32_t PC;
  uint32_t R[16];
  uint32_t H;
  uint32_t SPC;                 // SPC: Saved PC
  bool     SZ, SN, SC, SV;      //    : Saved Condition Codes
  bool     Z, N, C, V, I, E, P; //   I: Interrupt mode
                                //   E: Interrupts enabled
                                //   P: Interrupt pending

  uint32_t mem_size;
  uint32_t display_start;

  uint32_t progress;
  uint32_t current_tick;
  uint32_t mouse;
  uint8_t  key_buf[16];
  uint32_t key_cnt;
  uint32_t switches;

  const struct RISC_LED *leds;
  const struct RISC_Serial *serial;
  uint32_t spi_selected;
  const struct RISC_SPI *spi[4];
  const struct RISC_Clipboard *clipboard;
  const struct RISC_HostFS *hostfs;
  const struct RISC_HostTransfer *hosttransfer;
  struct DisplayMode dyn_mode_slots[2];
  struct DisplayMode *modes;
  struct DisplayMode *current_mode;
  int current_mode_span; // in words
  int modes_by_depth[3];
  bool screen_dynsize, screen_seamless;
  uint32_t initial_clock;

  struct Damage damage;

  int32_t hwenum_buf[24];
  uint32_t hwenum_idx, hwenum_cnt;

  uint32_t *RAM;
  uint32_t ROM[ROMWords];
  uint32_t Palette[256];
  char debug_buffer[512];
  uint32_t debug_buffer_index;
};

enum {
  MOV, LSL, ASR, ROR,
  AND, ANN, IOR, XOR,
  ADD, SUB, MUL, DIV,
  FAD, FSB, FML, FDV,
};

static void risc_single_step(struct RISC *risc);
static void risc_set_register(struct RISC *risc, int reg, uint32_t value);
static uint32_t risc_load_word(struct RISC *risc, uint32_t address);
static uint8_t risc_load_byte(struct RISC *risc, uint32_t address);
static void risc_store_word(struct RISC *risc, uint32_t address, uint32_t value);
static void risc_store_byte(struct RISC *risc, uint32_t address, uint8_t value);
static uint32_t risc_load_io(struct RISC *risc, uint32_t address);
static void risc_store_io(struct RISC *risc, uint32_t address, uint32_t value);

static const uint32_t bootloader[ROMWords] = {
#include "risc-boot.inc"
};

static const uint32_t default_palette[16] = {
  0xffffff, 0xff0000, 0x00ff00, 0x0000ff, 0xff00ff, 0xffff00, 0x00ffff, 0xaa0000,
  0x009a00, 0x00009a, 0x0acbf3, 0x008282, 0x8a8a8a, 0xbebebe, 0xdfdfdf, 0x000000
};


struct RISC *risc_new() {
  struct RISC *risc = calloc(1, sizeof(*risc));
  risc->mem_size = DefaultMemSize;
  risc->display_start = DefaultDisplayStart;
  risc->modes = risc->dyn_mode_slots;
  risc->modes[0] = (struct DisplayMode){
    .index = 0, .width = RISC_FRAMEBUFFER_WIDTH, .height = RISC_FRAMEBUFFER_HEIGHT, .depth = 1
  };
  risc->modes[1] = (struct DisplayMode){
    .index = -1, .width = 0, .height = 0, .depth = 0
  };
  risc->current_mode = risc->modes;
  risc->current_mode_span = RISC_FRAMEBUFFER_WIDTH / 32;
  time_t now;
  time(&now);
  struct tm *t = localtime(&now);
  int clock = ((t->tm_year % 100) * 16 + t->tm_mon + 1) * 32 + t->tm_mday;
  clock = ((clock * 32 + t->tm_hour) * 64 + t->tm_min) * 64 + t->tm_sec;
  risc->initial_clock = clock;
  risc->damage = (struct Damage){
    .x1 = 0,
    .y1 = 0,
    .x2 = risc->current_mode_span - 1,
    .y2 = risc->current_mode->height - 1
  };
  risc->RAM = calloc(1, risc->mem_size);
  memcpy(risc->ROM, bootloader, sizeof(risc->ROM));
  risc_reset(risc);
  return risc;
}

void risc_configure_memory(struct RISC *risc, int megabytes_ram, struct DisplayMode *modes, bool screen_dynsize) {
  if (megabytes_ram < 1) {
    megabytes_ram = 1;
  }
  if (megabytes_ram > 64) {
    megabytes_ram = 64;
  }

  risc->display_start = megabytes_ram << 20;
  int framebuffer_size = 0, max_depth = 1;
  struct DisplayMode *mode = modes;
  if (screen_dynsize) {
    framebuffer_size = 2048 * 2048;
  }
  while (mode->width != 0) {
    if (mode->depth == 1) risc->modes_by_depth[0]++;
    if (mode->depth == 4) risc->modes_by_depth[1]++;
    if (mode->depth == 8) risc->modes_by_depth[2]++;
    int mode_framebuffer_size = mode->width * mode->height / (8 / mode->depth);
    if (mode_framebuffer_size > framebuffer_size)
      framebuffer_size = mode_framebuffer_size;
    if (mode->depth > max_depth)
      max_depth = mode->depth;
    mode++;
  }
  risc->mem_size = risc->display_start + framebuffer_size;
  if (max_depth > 1) {
    memcpy(risc->Palette, default_palette, sizeof(default_palette));
    if (max_depth == 8) {
      for(int i=16; i < 40; i++) {
        risc->Palette[i] = (i-15) * 10 * 0x010101;
      }
      int pos = 40;
      for(int i=0; i<6; i++) {
        for(int j=0; j<6; j++) {
          for (int k=0; k<6; k++) {
            risc->Palette[pos++] = i * 0x330000 + j * 0x3300 + k * 0x33;
          }
        }
      }
    }
  }
  risc->modes = modes;
  risc->current_mode = modes;
  risc->current_mode_span = risc->current_mode->width / (32 / risc->current_mode->depth);
  risc->damage = (struct Damage){
    .x1 = 0,
    .y1 = 0,
    .x2 = risc->current_mode_span - 1,
    .y2 = risc->current_mode->height - 1
  };
  risc->screen_dynsize = screen_dynsize;
  free(risc->RAM);
  risc->RAM = calloc(1, risc->mem_size);

  // Patch the new constants in the bootloader.
  uint32_t mem_lim = risc->display_start - 16;
  risc->ROM[372] = 0x61000000 + (mem_lim >> 16);
  risc->ROM[373] = 0x41160000 + (mem_lim & 0x0000FFFF);
  uint32_t stack_org = risc->display_start / 2;
  risc->ROM[376] = 0x61000000 + (stack_org >> 16);

  risc_reset(risc);
}

void risc_set_leds(struct RISC *risc, const struct RISC_LED *leds) {
  risc->leds = leds;
}

void risc_set_serial(struct RISC *risc, const struct RISC_Serial *serial) {
  risc->serial = serial;
}

void risc_set_spi(struct RISC *risc, int index, const struct RISC_SPI *spi) {
  if (index == 1 || index == 2) {
    risc->spi[index] = spi;
  }
}

void risc_set_clipboard(struct RISC *risc, const struct RISC_Clipboard *clipboard) {
  risc->clipboard = clipboard;
}

void risc_set_switches(struct RISC *risc, int switches) {
  risc->switches = switches;
}

void risc_set_host_fs(struct RISC *risc, const struct RISC_HostFS *hostfs) {
  risc->hostfs = hostfs;
}

void risc_set_host_transfer(struct RISC *risc, const struct RISC_HostTransfer *hosttransfer) {
  risc->hosttransfer = hosttransfer;
}

void risc_reset(struct RISC *risc) {
  risc->PC = ROMStart/4;
}

void risc_trigger_interrupt(struct RISC *risc) {
  risc->P = true;
}

void risc_run(struct RISC *risc, int cycles) {
  risc->progress = 20;
  // The progress value is used to detect that the RISC cpu is busy
  // waiting on the millisecond counter or on the keyboard ready
  // bit. In that case it's better to just pause emulation until the
  // next frame.
  for (int i = 0; i < cycles && risc->progress; i++) {
    risc_single_step(risc);
  }
}

static void risc_single_step(struct RISC *risc) {
  uint32_t ir;
  if (risc->P && risc->E && ! risc->I ) {
    risc->SPC = risc->PC;
    risc->SZ = risc->Z;
    risc->SN = risc->N;
    risc->SC = risc->C;
    risc->SV = risc->V;
    risc->I = true;
    risc->PC = 1;
  }
  if (risc->PC < risc->mem_size / 4) {
    ir = risc->RAM[risc->PC];
  } else if (risc->PC >= ROMStart/4 && risc->PC < ROMStart/4 + ROMWords) {
    ir = risc->ROM[risc->PC - ROMStart/4];
  } else {
    fprintf(stderr, "Branched into the void (PC=0x%08X), resetting...\n", risc->PC);
    risc_reset(risc);
    return;
  }
  risc->PC++;

  const uint32_t pbit = 0x80000000;
  const uint32_t qbit = 0x40000000;
  const uint32_t ubit = 0x20000000;
  const uint32_t vbit = 0x10000000;

  if ((ir & pbit) == 0) {
    // Register instructions
    uint32_t a  = (ir & 0x0F000000) >> 24;
    uint32_t b  = (ir & 0x00F00000) >> 20;
    uint32_t op = (ir & 0x000F0000) >> 16;
    uint32_t im =  ir & 0x0000FFFF;
    uint32_t c  =  ir & 0x0000000F;

    uint32_t a_val, b_val, c_val;
    b_val = risc->R[b];
    if ((ir & qbit) == 0) {
      c_val = risc->R[c];
    } else if ((ir & vbit) == 0) {
      c_val = im;
    } else {
      c_val = 0xFFFF0000 | im;
    }

    switch (op) {
      case MOV: {
        if ((ir & ubit) == 0) {
          a_val = c_val;
        } else if ((ir & qbit) != 0) {
          a_val = c_val << 16;
        } else if ((ir & vbit) != 0) {
          a_val = 0xD0 |   // ???
            (risc->N * 0x80000000U) |
            (risc->Z * 0x40000000U) |
            (risc->C * 0x20000000U) |
            (risc->V * 0x10000000U);
        } else {
          a_val = risc->H;
        }
        break;
      }
      case LSL: {
        a_val = b_val << (c_val & 31);
        break;
      }
      case ASR: {
        a_val = ((int32_t)b_val) >> (c_val & 31);
        break;
      }
      case ROR: {
        a_val = (b_val >> (c_val & 31)) | (b_val << (-c_val & 31));
        break;
      }
      case AND: {
        a_val = b_val & c_val;
        break;
      }
      case ANN: {
        a_val = b_val & ~c_val;
        break;
      }
      case IOR: {
        a_val = b_val | c_val;
        break;
      }
      case XOR: {
        a_val = b_val ^ c_val;
        break;
      }
      case ADD: {
        a_val = b_val + c_val;
        if ((ir & ubit) != 0) {
          a_val += risc->C;
        }
        risc->C = a_val < b_val;
        risc->V = ((a_val ^ c_val) & (a_val ^ b_val)) >> 31;
        break;
      }
      case SUB: {
        a_val = b_val - c_val;
        if ((ir & ubit) != 0) {
          a_val -= risc->C;
        }
        risc->C = a_val > b_val;
        risc->V = ((b_val ^ c_val) & (a_val ^ b_val)) >> 31;
        break;
      }
      case MUL: {
        uint64_t tmp;
        if ((ir & ubit) == 0) {
          tmp = (int64_t)(int32_t)b_val * (int64_t)(int32_t)c_val;
        } else {
          tmp = (uint64_t)b_val * (uint64_t)c_val;
        }
        a_val = (uint32_t)tmp;
        risc->H = (uint32_t)(tmp >> 32);
        break;
      }
      case DIV: {
        if ((int32_t)c_val > 0) {
          if ((ir & ubit) == 0) {
            a_val = (int32_t)b_val / (int32_t)c_val;
            risc->H = (int32_t)b_val % (int32_t)c_val;
            if ((int32_t)risc->H < 0) {
              a_val--;
              risc->H += c_val;
            }
          } else {
            a_val = b_val / c_val;
            risc->H = b_val % c_val;
          }
        } else {
          struct idiv q = idiv(b_val, c_val, ir & ubit);
          a_val = q.quot;
          risc->H = q.rem;
        }
        break;
      }
      case FAD: {
        a_val = fp_add(b_val, c_val, ir & ubit, ir & vbit);
        break;
      }
      case FSB: {
        a_val = fp_add(b_val, c_val ^ 0x80000000, ir & ubit, ir & vbit);
        break;
      }
      case FML: {
        a_val = fp_mul(b_val, c_val);
        break;
      }
      case FDV: {
        a_val = fp_div(b_val, c_val);
        break;
      }
      default: {
        abort();  // unreachable
      }
    }
    risc_set_register(risc, a, a_val);
  }
  else if ((ir & qbit) == 0) {
    // Memory instructions
    uint32_t a = (ir & 0x0F000000) >> 24;
    uint32_t b = (ir & 0x00F00000) >> 20;
    int32_t off = ir & 0x000FFFFF;
    off = (off ^ 0x00080000) - 0x00080000;  // sign-extend

    uint32_t address = risc->R[b] + off;
    if ((ir & ubit) == 0) {
      uint32_t a_val;
      if ((ir & vbit) == 0) {
        a_val = risc_load_word(risc, address);
      } else {
        a_val = risc_load_byte(risc, address);
      }
      risc_set_register(risc, a, a_val);
    } else {
      if ((ir & vbit) == 0) {
        risc_store_word(risc, address, risc->R[a]);
      } else {
        risc_store_byte(risc, address, (uint8_t)risc->R[a]);
      }
    }
  }
  else {
    // Branch instructions
    bool t = (ir >> 27) & 1;
    switch ((ir >> 24) & 7) {
      case 0: t ^= risc->N; break;
      case 1: t ^= risc->Z; break;
      case 2: t ^= risc->C; break;
      case 3: t ^= risc->V; break;
      case 4: t ^= risc->C | risc->Z; break;
      case 5: t ^= risc->N ^ risc->V; break;
      case 6: t ^= (risc->N ^ risc->V) | risc->Z; break;
      case 7: t ^= true; 
              if (((ir & ubit) == 0) && ((ir & 0x00000010) == 0x10) && risc->I) { // IRET
                 risc->PC = risc->SPC;
                 risc->Z = risc->SZ;
                 risc->N = risc->SN;
                 risc->C = risc->SC;
                 risc->V = risc->SV;
                 risc->I = false;
                 risc->P = false;
                 return;
              }else{
                if (((ir & ubit) == 0) && ((ir & 0x00000020) == 0x20)) { // STI and CLI
                   risc->E = (ir & 1) == 1 ? true: false;
                   return;
                }
              }
              break;
      default: abort();  // unreachable
    }
    if (t) {
      if ((ir & vbit) != 0) {
        risc_set_register(risc, 15, risc->PC * 4);
      }
      if ((ir & ubit) == 0) {
        uint32_t c = ir & 0x0000000F;
        risc->PC = risc->R[c] / 4;
      } else {
        int32_t off = ir & 0x00FFFFFF;
        off = (off ^ 0x00800000) - 0x00800000;  // sign-extend
        risc->PC = risc->PC + off;
      }
    }
  }
}

static void risc_set_register(struct RISC *risc, int reg, uint32_t value) {
  risc->R[reg] = value;
  risc->Z = value == 0;
  risc->N = (int32_t)value < 0;
}

static uint32_t risc_load_word(struct RISC *risc, uint32_t address) {
  if (address < risc->mem_size) {
    return risc->RAM[address/4];
  } else {
    return risc_load_io(risc, address);
  }
}

static uint8_t risc_load_byte(struct RISC *risc, uint32_t address) {
  uint32_t w = risc_load_word(risc, address);
  return (uint8_t)(w >> (address % 4 * 8));
}

static void risc_update_damage(struct RISC *risc, int w) {
  int row = w / risc->current_mode_span;
  int col = w % risc->current_mode_span;
  if (row < risc->current_mode->height) {
    if (col < risc->damage.x1) {
      risc->damage.x1 = col;
    }
    if (col > risc->damage.x2) {
      risc->damage.x2 = col;
    }
    if (row < risc->damage.y1) {
      risc->damage.y1 = row;
    }
    if (row > risc->damage.y2) {
      risc->damage.y2 = row;
    }
  }
}

static void risc_store_word(struct RISC *risc, uint32_t address, uint32_t value) {
  if (address < risc->display_start) {
    risc->RAM[address/4] = value;
  } else if (address < risc->mem_size) {
    risc->RAM[address/4] = value;
    risc_update_damage(risc, address/4 - risc->display_start/4);
  } else {
    risc_store_io(risc, address, value);
  }
}

static void risc_store_byte(struct RISC *risc, uint32_t address, uint8_t value) {
  if (address < risc->mem_size) {
    uint32_t w = risc_load_word(risc, address);
    uint32_t shift = (address & 3) * 8;
    w &= ~(0xFFu << shift);
    w |= (uint32_t)value << shift;
    risc_store_word(risc, address, w);
  } else {
    risc_store_io(risc, address, (uint32_t)value);
  }
}

static uint32_t risc_load_io(struct RISC *risc, uint32_t address) {
  if (address >= PaletteStart && address < PaletteStart + 0x400) {
    return risc->Palette[(address - PaletteStart)/4];
  }
  switch (address - IOStart) {
    case 0: {
      // Millisecond counter
      risc->progress--;
      return risc->current_tick;
    }
    case 4: {
      // Switches
      return risc->switches;
    }
    case 8: {
      // RS232 data
      if (risc->serial) {
        return risc->serial->read_data(risc->serial);
      }
      return 0;
    }
    case 12: {
      // RS232 status
      if (risc->serial) {
        return risc->serial->read_status(risc->serial);
      }
      return 0;
    }
    case 16: {
      // SPI data
      const struct RISC_SPI *spi = risc->spi[risc->spi_selected];
      if (spi != NULL) {
        return spi->read_data(spi);
      }
      return 255;
    }
    case 20: {
      // SPI status
      // Bit 0: rx ready
      // Other bits unused
      return 1;
    }
    case 24: {
      // Mouse input / keyboard status
      uint32_t mouse = risc->mouse;
      if (risc->key_cnt > 0) {
        mouse |= 0x10000000;
      } else {
        risc->progress--;
      }
      return mouse;
    }
    case 28: {
      // Keyboard input
      if (risc->key_cnt > 0) {
        uint8_t scancode = risc->key_buf[0];
        risc->key_cnt--;
        memmove(&risc->key_buf[0], &risc->key_buf[1], risc->key_cnt);
        return scancode;
      }
      return 0;
    }
    case 40: {
      // Clipboard control
      if (risc->clipboard) {
        return risc->clipboard->read_control(risc->clipboard);
      }
      return 0;
    }
    case 44: {
      // Clipboard data
      if (risc->clipboard) {
        return risc->clipboard->read_data(risc->clipboard);
      }
      return 0;
    }
    case 48: {
      // Screen mode
      return risc->current_mode->index;
    }
    case 60: {
      // hardware enumerator
      if (risc->hwenum_idx < risc->hwenum_cnt) {
        return risc->hwenum_buf[risc->hwenum_idx++];
      }
      return 0;
    }
    default: {
      return 0;
    }
  }
}

static void risc_store_io(struct RISC *risc, uint32_t address, uint32_t value) {
  if (address >= PaletteStart && address < PaletteStart + 0x400) {
    risc->Palette[(address - PaletteStart)/4] = value;
    risc->damage = (struct Damage){
      .x1 = 0,
      .y1 = 0,
      .x2 = risc->current_mode_span - 1,
      .y2 = risc->current_mode->height - 1
    };
    return;
  }
  switch (address - IOStart) {
    case 4: {
      // LED control
      if (risc->leds) {
        risc->leds->write(risc->leds, value);
      }
      break;
    }
    case 8: {
      // RS232 data
      if (risc->serial) {
        risc->serial->write_data(risc->serial, value);
      }
      break;
    }
    case 16: {
      // SPI write
      const struct RISC_SPI *spi = risc->spi[risc->spi_selected];
      if (spi != NULL) {
        spi->write_data(spi, value);
      }
      break;
    }
    case 20: {
      // SPI control
      // Bit 0-1: slave select
      // Bit 2:   fast mode
      // Bit 3:   netwerk enable
      // Other bits unused
      risc->spi_selected = value & 3;
      break;
    }
    case 32: {
      // Host FS
      if (risc->hostfs) {
        risc->hostfs->write(risc->hostfs, value, risc->RAM);
      }
      // Host Transfer
      if (risc->hosttransfer) {
        risc->hosttransfer->write(risc->hosttransfer, value, risc->RAM);
      }
      break;
    }
    case 36: {
      // Paravirtual disk
      if (risc->spi[1] != NULL && risc->spi[1]->paravirtual_write != NULL) {
        risc->spi[1]->paravirtual_write(risc->spi[1], value, risc->RAM);
      }
      break;
    }
    case 40: {
      // Clipboard control
      if (risc->clipboard) {
        risc->clipboard->write_control(risc->clipboard, value);
      }
      break;
    }
    case 44: {
      // Clipboard data
      if (risc->clipboard) {
        risc->clipboard->write_data(risc->clipboard, value);
      }
      break;
    }
    case 48: {
      // mode switch
      bool found = false;
      struct DisplayMode *mode = risc->modes;
      while (mode->width != 0) {
        if (mode->index == value) {
          risc->current_mode = mode;
          risc->current_mode_span = risc->current_mode->width / (32 / risc->current_mode->depth);
          found = true;
          break;
        }
        mode++;
      }
      risc->screen_seamless = false;
      if (!found && risc->screen_dynsize) {
        unsigned int mode = value >> 30;
        unsigned int width = (value >> 15) & ((1 << 15) - 1);
        unsigned int height = (value) & ((1 << 15) - 1);
        if (width == 0 && height == 0) {
          risc->screen_seamless = true;
          width = risc->dyn_mode_slots[1].width;
          height = risc->dyn_mode_slots[1].height;
          width = width / 32 * 32;
          if (width < 64) width = 64;
          if (height < 64) height = 64;
          if (width > 2048) width = 2048;
          if (height > 2048) height = 2048;
          value = (mode << 30) | (width << 15) | height;
        }
        if (width <= 2048 && width % 32 == 0 && height <= 2045 && mode >=1 && mode <=3) {
          risc->current_mode = &risc->dyn_mode_slots[0];
          risc->current_mode->index = value;
          risc->current_mode->width = width;
          risc->current_mode->height = height;
          risc->current_mode->depth  = mode == 1 ? 1 : mode == 2 ? 8 : 4;
          risc->current_mode_span = width / (32 / risc->current_mode->depth);
        }
      }
      break;
    }
    case 52: {
      // Debug console
      if (value == 0 || risc->debug_buffer_index == 511) {
        risc->debug_buffer[risc->debug_buffer_index] = '\0';
        printf("%s", risc->debug_buffer);
        risc->debug_buffer_index = 0;
      }
      if (value != 0) {
        if (value == '\r') value = '\n';
        risc->debug_buffer[risc->debug_buffer_index++] = (char) value;
      }
      break;
    }
    case 60: {
      // hardware enumerator
      risc->hwenum_cnt = 0;
      risc->hwenum_idx = 0;
      switch(value) {
      case 0:
        risc->hwenum_buf[risc->hwenum_cnt++] = 1; // version
        if (risc->modes_by_depth[0] > 0) {
          risc->hwenum_buf[risc->hwenum_cnt++] = HW_ENUM_ID('m','V','i','d');
          if (risc->screen_dynsize) {
            risc->hwenum_buf[risc->hwenum_cnt++] = HW_ENUM_ID('m','D','y','n');
          }
        }
        if (risc->modes_by_depth[1] > 0) {
          risc->hwenum_buf[risc->hwenum_cnt++] = HW_ENUM_ID('1','6','c','V');
          if (risc->screen_dynsize) {
            risc->hwenum_buf[risc->hwenum_cnt++] = HW_ENUM_ID('1','6','c','D');
          }
        }
        if (risc->modes_by_depth[2] > 0) {
          risc->hwenum_buf[risc->hwenum_cnt++] = HW_ENUM_ID('8','b','c','V');
          if (risc->screen_dynsize) {
            risc->hwenum_buf[risc->hwenum_cnt++] = HW_ENUM_ID('8','b','c','D');
          }
        }
        risc->hwenum_buf[risc->hwenum_cnt++] = HW_ENUM_ID('T','i','m','r');
        risc->hwenum_buf[risc->hwenum_cnt++] = HW_ENUM_ID('S','w','t','c');
        risc->hwenum_buf[risc->hwenum_cnt++] = HW_ENUM_ID('S','P','I','f');
        risc->hwenum_buf[risc->hwenum_cnt++] = HW_ENUM_ID('M','s','K','b');
        risc->hwenum_buf[risc->hwenum_cnt++] = HW_ENUM_ID('R','s','e','t');
        risc->hwenum_buf[risc->hwenum_cnt++] = HW_ENUM_ID('v','R','T','C');
        risc->hwenum_buf[risc->hwenum_cnt++] = HW_ENUM_ID('D','b','g','C');
        if (risc->leds) {
          risc->hwenum_buf[risc->hwenum_cnt++] = HW_ENUM_ID('L','E','D','s');
        }
        if (risc->serial) {
          risc->hwenum_buf[risc->hwenum_cnt++] = HW_ENUM_ID('S','P','r','t');
        }
        if (risc->clipboard) {
          risc->hwenum_buf[risc->hwenum_cnt++] = HW_ENUM_ID('v','C','l','p');
        }
        if (risc->hostfs) {
          risc->hwenum_buf[risc->hwenum_cnt++] = HW_ENUM_ID('H','s','F','s');
        }
        if (risc->hosttransfer) {
          risc->hwenum_buf[risc->hwenum_cnt++] = HW_ENUM_ID('v','H','T','x');
        }
        if (risc->spi[1] != NULL && risc->spi[1]->paravirtual_write != NULL) {
          risc->hwenum_buf[risc->hwenum_cnt++] = HW_ENUM_ID('v','D','s','k');
        }
        break;
      case HW_ENUM_ID('m','V','i','d'):
        if (risc->modes_by_depth[0] > 0) {
          risc->hwenum_buf[risc->hwenum_cnt++] = risc->modes_by_depth[0]; // number of modes
          risc->hwenum_buf[risc->hwenum_cnt++] = -16; // mode switching MMIO address
          struct DisplayMode *mode = risc->modes;
          while (mode->width != 0) {
            if (mode->depth == 1) {
              risc->hwenum_buf[risc->hwenum_cnt++] = mode->width; // screen width
              risc->hwenum_buf[risc->hwenum_cnt++] = mode->height; // screen height
              risc->hwenum_buf[risc->hwenum_cnt++] = mode->width / 8; // scanline span
              risc->hwenum_buf[risc->hwenum_cnt++] = risc->display_start; // base address
            }
            mode++;
          }
        }
        break;
      case HW_ENUM_ID('m','D','y','n'):
        if (risc->modes_by_depth[0] > 0 && risc->screen_dynsize) {
          risc->hwenum_buf[risc->hwenum_cnt++] = -16; // mode switching MMIO address
          risc->hwenum_buf[risc->hwenum_cnt++] = 2048; // maximum width
          risc->hwenum_buf[risc->hwenum_cnt++] = 2048; // maximum height
          risc->hwenum_buf[risc->hwenum_cnt++] = 32; // width increment
          risc->hwenum_buf[risc->hwenum_cnt++] = 1; // height increment
          risc->hwenum_buf[risc->hwenum_cnt++] = -1; // dynamic scan line span
          risc->hwenum_buf[risc->hwenum_cnt++] = risc->display_start; // base address
          risc->hwenum_buf[risc->hwenum_cnt++] = 1; // seamless resize
        }
        break;
      case HW_ENUM_ID('1','6','c','V'):
        if (risc->modes_by_depth[1] > 0) {
          risc->hwenum_buf[risc->hwenum_cnt++] = risc->modes_by_depth[1]; // number of modes
          risc->hwenum_buf[risc->hwenum_cnt++] = risc->modes_by_depth[0]; // first mode
          risc->hwenum_buf[risc->hwenum_cnt++] = -16; // mode switching MMIO address
          risc->hwenum_buf[risc->hwenum_cnt++] = PaletteStart; // palette address
          struct DisplayMode *mode = risc->modes;
          while (mode->width != 0) {
            if (mode->depth == 4) {
              risc->hwenum_buf[risc->hwenum_cnt++] = mode->width; // screen width
              risc->hwenum_buf[risc->hwenum_cnt++] = mode->height; // screen height
              risc->hwenum_buf[risc->hwenum_cnt++] = mode->width / 2; // scanline span
              risc->hwenum_buf[risc->hwenum_cnt++] = risc->display_start; // base address
            }
            mode++;
          }
        }
        break;
      case HW_ENUM_ID('1','6','c','D'):
        if (risc->modes_by_depth[1] > 0 && risc->screen_dynsize) {
          risc->hwenum_buf[risc->hwenum_cnt++] = -16; // mode switching MMIO address
          risc->hwenum_buf[risc->hwenum_cnt++] = PaletteStart; // palette address
          risc->hwenum_buf[risc->hwenum_cnt++] = 2048; // maximum width
          risc->hwenum_buf[risc->hwenum_cnt++] = 2048; // maximum height
          risc->hwenum_buf[risc->hwenum_cnt++] = 32; // width increment
          risc->hwenum_buf[risc->hwenum_cnt++] = 1; // height increment
          risc->hwenum_buf[risc->hwenum_cnt++] = -1; // dynamic scan line span
          risc->hwenum_buf[risc->hwenum_cnt++] = risc->display_start; // base address
          risc->hwenum_buf[risc->hwenum_cnt++] = 1; // seamless resize
        }
        break;
      case HW_ENUM_ID('8','b','c','V'):
        if (risc->modes_by_depth[2] > 0) {
          risc->hwenum_buf[risc->hwenum_cnt++] = risc->modes_by_depth[2]; // number of modes
          risc->hwenum_buf[risc->hwenum_cnt++] = risc->modes_by_depth[0] + risc->modes_by_depth[1]; // first mode
          risc->hwenum_buf[risc->hwenum_cnt++] = -16; // mode switching MMIO address
          risc->hwenum_buf[risc->hwenum_cnt++] = PaletteStart; // palette address
          struct DisplayMode *mode = risc->modes;
          while (mode->width != 0) {
            if (mode->depth == 8) {
              risc->hwenum_buf[risc->hwenum_cnt++] = mode->width; // screen width
              risc->hwenum_buf[risc->hwenum_cnt++] = mode->height; // screen height
              risc->hwenum_buf[risc->hwenum_cnt++] = mode->width; // scanline span
              risc->hwenum_buf[risc->hwenum_cnt++] = risc->display_start; // base address
            }
            mode++;
          }
        }
        break;
      case HW_ENUM_ID('8','b','c','D'):
        if (risc->modes_by_depth[1] > 0 && risc->screen_dynsize) {
          risc->hwenum_buf[risc->hwenum_cnt++] = -16; // mode switching MMIO address
          risc->hwenum_buf[risc->hwenum_cnt++] = PaletteStart; // palette address
          risc->hwenum_buf[risc->hwenum_cnt++] = 2048; // maximum width
          risc->hwenum_buf[risc->hwenum_cnt++] = 2048; // maximum height
          risc->hwenum_buf[risc->hwenum_cnt++] = 32; // width increment
          risc->hwenum_buf[risc->hwenum_cnt++] = 1; // height increment
          risc->hwenum_buf[risc->hwenum_cnt++] = -1; // dynamic scan line span
          risc->hwenum_buf[risc->hwenum_cnt++] = risc->display_start; // base address
          risc->hwenum_buf[risc->hwenum_cnt++] = 1; // seamless resize
        }
        break;
      case HW_ENUM_ID('T','i','m','r'):
        risc->hwenum_buf[risc->hwenum_cnt++] = -64; // MMIO address
        break;
      case HW_ENUM_ID('S','w','t','c'):
        risc->hwenum_buf[risc->hwenum_cnt++] = 1; // number of switches
        risc->hwenum_buf[risc->hwenum_cnt++] = -60; // MMIO address
        break;
      case HW_ENUM_ID('L','E','D','s'):
        if (risc->leds) {
          risc->hwenum_buf[risc->hwenum_cnt++] = 8; // number of LEDs
          risc->hwenum_buf[risc->hwenum_cnt++] = -60; // MMIO address
        }
        break;
      case HW_ENUM_ID('S','P','r','t'):
        if (risc->serial) {
          risc->hwenum_buf[risc->hwenum_cnt++] = 1;  // number of serial ports
          risc->hwenum_buf[risc->hwenum_cnt++] = -52; // MMIO status address
          risc->hwenum_buf[risc->hwenum_cnt++] = -56; // MMIO data address
        }
        break;
      case HW_ENUM_ID('S','P','I','f'):
        risc->hwenum_buf[risc->hwenum_cnt++] = -44;  // MMIO control address
        risc->hwenum_buf[risc->hwenum_cnt++] = -48;  // MMIO status address
        if (risc->spi[1] != NULL) {
          risc->hwenum_buf[risc->hwenum_cnt++] = HW_ENUM_ID('S','D','C','r'); // SD card
        }
        if (risc->spi[2] != NULL) {
          risc->hwenum_buf[risc->hwenum_cnt++] = HW_ENUM_ID('w','N','e','t'); // wireless network
        }
        break;
      case HW_ENUM_ID('M','s','K','b'):
        risc->hwenum_buf[risc->hwenum_cnt++] = -40; // MMIO mouse address + keyboard status
        risc->hwenum_buf[risc->hwenum_cnt++] = -36; // MMIO keyboard address
        break;
      case HW_ENUM_ID('v','C','l','p'):
        if (risc->clipboard) {
          risc->hwenum_buf[risc->hwenum_cnt++] = -24; // MMIO clipboard control address
          risc->hwenum_buf[risc->hwenum_cnt++] = -20; // MMIO clipboard data address
        }
        break;
      case HW_ENUM_ID('v','D','s','k'):
        if (risc->spi[1] != NULL && risc->spi[1]->paravirtual_write != NULL) {
          risc->hwenum_buf[risc->hwenum_cnt++] = -28; // MMIO address
        }
        break;
      case HW_ENUM_ID('H','s','F','s'):
        if (risc->hostfs) {
          risc->hwenum_buf[risc->hwenum_cnt++] = -32; // MMIO host fs address
        }
        break;
      case HW_ENUM_ID('v','H','T','x'):
        if (risc->hosttransfer) {
          risc->hwenum_buf[risc->hwenum_cnt++] = -32; // MMIO host transfer address
        }
        break;
      case HW_ENUM_ID('D','b','g','C'):
        risc->hwenum_buf[risc->hwenum_cnt++] = -12; // MMIO debug console address
        break;
      case HW_ENUM_ID('R','s','e','t'):
        risc->hwenum_buf[risc->hwenum_cnt++] = ROMStart; // Soft reset vector
        break;
      case HW_ENUM_ID('v','R','T','C'):
        risc->hwenum_buf[risc->hwenum_cnt++] = 0; // SDL_GetTicks starts at zero
        risc->hwenum_buf[risc->hwenum_cnt++] = risc->initial_clock;
        break;
      }
      break;
    }
  }
}


void risc_set_time(struct RISC *risc, uint32_t tick) {
  risc->current_tick = tick;
}

void risc_mouse_moved(struct RISC *risc, int mouse_x, int mouse_y) {
  if (mouse_x >= 0 && mouse_x < 4096) {
    risc->mouse = (risc->mouse & ~0x00000FFF) | mouse_x;
  }
  if (mouse_y >= 0 && mouse_y < 4096) {
    risc->mouse = (risc->mouse & ~0x00FFF000) | (mouse_y << 12);
  }
}

void risc_mouse_button(struct RISC *risc, int button, bool down) {
  if (button >= 1 && button < 4) {
    uint32_t bit = 1 << (27 - button);
    if (down) {
      risc->mouse |= bit;
    } else {
      risc->mouse &= ~bit;
    }
  }
}

void risc_keyboard_input(struct RISC *risc, uint8_t *scancodes, uint32_t len) {
  if (sizeof(risc->key_buf) - risc->key_cnt >= len) {
    memmove(&risc->key_buf[risc->key_cnt], scancodes, len);
    risc->key_cnt += len;
  }
}

uint32_t *risc_get_framebuffer_ptr(struct RISC *risc) {
  return &risc->RAM[risc->display_start/4];
}

uint32_t *risc_get_palette_ptr(struct RISC *risc) {
  return risc->Palette;
}

struct DisplayMode *risc_get_display_mode(struct RISC *risc, bool *screen_seamless) {
  if (screen_seamless != NULL)
    *screen_seamless = risc->screen_seamless;
  return risc->current_mode;
}

void risc_size_hint(struct RISC *risc, int width, int height) {
  if (risc->screen_dynsize) {
    risc->dyn_mode_slots[1].width = width;
    risc->dyn_mode_slots[1].height = height;
  }
}

struct Damage risc_get_framebuffer_damage(struct RISC *risc) {
  struct Damage dmg = risc->damage;
  risc->damage = (struct Damage){
    .x1 = risc->current_mode_span,
    .x2 = 0,
    .y1 = risc->current_mode->height,
    .y2 = 0
  };
  return dmg;
}
