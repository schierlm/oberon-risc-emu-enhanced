#ifndef RISC_H
#define RISC_H

#include <stdbool.h>
#include <stdint.h>
#include "risc-io.h"

// This is the standard size of the framebuffer, can be overridden.
#define RISC_FRAMEBUFFER_WIDTH 1024
#define RISC_FRAMEBUFFER_HEIGHT 768

struct RISC;

struct Damage {
  int x1, x2, y1, y2;
};

struct DisplayMode {
  uint32_t index;
  int width, height, depth;
};

struct RISC *risc_new(void);
void risc_configure_memory(struct RISC *risc, int megabytes_ram, struct DisplayMode* modes, bool screen_dynsize);
void risc_set_leds(struct RISC *risc, const struct RISC_LED *leds);
void risc_set_serial(struct RISC *risc, const struct RISC_Serial *serial);
void risc_set_spi(struct RISC *risc, int index, const struct RISC_SPI *spi);
void risc_set_clipboard(struct RISC *risc, const struct RISC_Clipboard *clipboard);
void risc_set_switches(struct RISC *risc, int switches);
void risc_set_host_fs(struct RISC *risc, const struct RISC_HostFS *hostfs);
void risc_set_host_transfer(struct RISC *risc, const struct RISC_HostTransfer *hosttransfer);

void risc_reset(struct RISC *risc);
void risc_trigger_interrupt(struct RISC *risc); 
void risc_run(struct RISC *risc, int cycles);
void risc_set_time(struct RISC *risc, uint32_t tick);
void risc_mouse_moved(struct RISC *risc, int mouse_x, int mouse_y);
void risc_mouse_button(struct RISC *risc, int button, bool down);
void risc_keyboard_input(struct RISC *risc, uint8_t *scancodes, uint32_t len);
void risc_size_hint(struct RISC *risc, int width, int height);

uint32_t *risc_get_framebuffer_ptr(struct RISC *risc);
struct DisplayMode *risc_get_display_mode(struct RISC *risc, bool *screen_seamless);
uint32_t *risc_get_palette_ptr(struct RISC *risc);
struct Damage risc_get_framebuffer_damage(struct RISC *risc);

#endif  // RISC_H
