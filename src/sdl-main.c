#include <SDL.h>
#include <getopt.h>
#include <math.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "risc.h"
#include "risc-io.h"
#include "disk.h"
#include "pclink.h"
#include "raw-serial.h"
#include "sdl-ps2.h"
#include "sdl-clipboard.h"

#define CPU_HZ 25000000
#define FPS 60
#define MSPF 1000/FPS

static uint32_t BLACK = 0x657b83, WHITE = 0xfdf6e3;
//static uint32_t BLACK = 0x000000, WHITE = 0xFFFFFF;
//static uint32_t BLACK = 0x0000FF, WHITE = 0xFFFF00;
//static uint32_t BLACK = 0x000000, WHITE = 0x00FF00;

#define MAX_MODE_COUNT 32
#define MAX_HEIGHT 2048
#define MAX_WIDTH  2048

static int best_display(const SDL_Rect *rect);
static int clamp(int x, int min, int max);
static enum Action map_keyboard_event(SDL_KeyboardEvent *event);
static void show_leds(const struct RISC_LED *leds, uint32_t value);
static double scale_display(SDL_Window *window, const SDL_Rect *risc_rect, SDL_Rect *display_rect);
static void update_texture(struct RISC *risc, SDL_Texture *texture, const SDL_Rect *risc_rect, int depth);

enum Action {
  ACTION_OBERON_INPUT,
  ACTION_QUIT,
  ACTION_RESET,
  ACTION_TOGGLE_FULLSCREEN,
  ACTION_FAKE_MOUSE1,
  ACTION_FAKE_MOUSE2,
  ACTION_FAKE_MOUSE3
};

struct KeyMapping {
  int state;
  SDL_Keycode sym;
  SDL_Keymod mod1, mod2;
  enum Action action;
};

struct KeyMapping key_map[] = {
  { SDL_PRESSED,  SDLK_F4,     KMOD_ALT, 0,           ACTION_QUIT },
  { SDL_PRESSED,  SDLK_F12,    0, 0,                  ACTION_RESET },
  { SDL_PRESSED,  SDLK_DELETE, KMOD_CTRL, KMOD_SHIFT, ACTION_RESET },
  { SDL_PRESSED,  SDLK_F11,    0, 0,                  ACTION_TOGGLE_FULLSCREEN },
  { SDL_PRESSED,  SDLK_RETURN, KMOD_ALT, 0,           ACTION_TOGGLE_FULLSCREEN },
  { SDL_PRESSED,  SDLK_f,      KMOD_GUI, KMOD_SHIFT,  ACTION_TOGGLE_FULLSCREEN },  // Mac?
  { SDL_PRESSED,  SDLK_LALT,   0, 0,                  ACTION_FAKE_MOUSE2 },
  { SDL_RELEASED, SDLK_LALT,   0, 0,                  ACTION_FAKE_MOUSE2 },
};

static struct option long_options[] = {
  { "zoom",             required_argument, NULL, 'z' },
  { "fullscreen",       no_argument,       NULL, 'f' },
  { "leds",             no_argument,       NULL, 'L' },
  { "mem",              required_argument, NULL, 'm' },
  { "size",             required_argument, NULL, 's' },
  { "serial-in",        required_argument, NULL, 'I' },
  { "serial-out",       required_argument, NULL, 'O' },
  { "boot-from-serial", no_argument,       NULL, 'S' },
  { "dynsize",          no_argument,       NULL, 'd' },
  { "hostfs",           required_argument, NULL, 'H' },
  { NULL,               no_argument,       NULL, 0   }
};

static void fail(int code, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vfprintf(stderr, fmt, ap);
  va_end(ap);
  fputc('\n', stderr);
  exit(code);
}

static void usage() {
  puts("Usage: risc [OPTIONS...] DISK-IMAGE\n"
       "\n"
       "Options:\n"
       "  --fullscreen          Start the emulator in full screen mode\n"
       "  --zoom REAL           Scale the display in windowed mode\n"
       "  --leds                Log LED state on stdout\n"
       "  --mem MEGS            Set memory size\n"
       "  --dynsize             Allow dynamic screen resize from guest\n"
       "  --size WIDTHxHEIGHT[xDEPTH][,...]\n"
       "                        Set framebuffer size or multiple resolutions\n"
       "                        DEPTH has to be 1, 4 or 8, and multiple modes'\n"
       "                        depths must be ascending order.\n"
       "  --boot-from-serial    Boot from serial line (disk image not required)\n"
       "  --serial-in FILE      Read serial input from FILE\n"
       "  --serial-out FILE     Write serial output to FILE\n"
       "  --hostfs DIRECTORY    Use DIRECTORY as HostFS directory\n"
       );
  exit(1);
}

int main (int argc, char *argv[]) {
  struct RISC *risc = risc_new();
  risc_set_serial(risc, &pclink);
  risc_set_clipboard(risc, &sdl_clipboard);


  struct RISC_LED leds = {
    .write = show_leds
  };

  bool fullscreen = false;
  double zoom = 0;
  struct DisplayMode all_modes[MAX_MODE_COUNT];
  struct DisplayMode *current_mode;
  uint32_t previous_mode_index;
  SDL_Rect risc_rect = {};
  bool dynsize_option = false, seamless = false, resizable = false;
  int mem_option = 0, mode_count = 0, last_depth = 1;
  const char *serial_in = NULL;
  const char *serial_out = NULL;
  bool boot_from_serial = false;

  int opt;
  while ((opt = getopt_long(argc, argv, "z:fLm:s:I:O:SdH:", long_options, NULL)) != -1) {
    switch (opt) {
      case 'z': {
        double x = strtod(optarg, 0);
        if (x > 0) {
          zoom = x;
        }
        break;
      }
      case 'f': {
        fullscreen = true;
        break;
      }
      case 'L': {
        risc_set_leds(risc, &leds);
        break;
      }
      case 'm': {
        if (sscanf(optarg, "%d", &mem_option) != 1) {
          usage();
        }
        break;
      }
      case 's': {
        int w, h, d, len;
        char* ptr = optarg;
        while(*ptr != '\0') {
          if (sscanf(ptr, "%dx%d%n", &w, &h, &len) != 2) {
            usage();
          }
          d = 1;
          ptr += len;
          if (*ptr == 'x') {
            if (sscanf(ptr, "x%d%n", &d, &len) != 1) {
              usage();
            }
            ptr += len;
          }
          if ((d < last_depth) || (d != 1 && d != 4 && d != 8)) {
            usage();
          }
          while(*ptr == ',' || *ptr == ' ') ptr++;
          all_modes[mode_count].width = clamp(w, 32, MAX_WIDTH) & ~31;
          all_modes[mode_count].height = clamp(h, 32, MAX_HEIGHT);
          all_modes[mode_count].depth = d;
          all_modes[mode_count].index = mode_count;
          last_depth = d;
          mode_count++;
        }
        break;
      }
      case 'd': {
        dynsize_option = true;
        break;
      }
      case 'I': {
        serial_in = optarg;
        break;
      }
      case 'O': {
        serial_out = optarg;
        break;
      }
      case 'S': {
        boot_from_serial = true;
        risc_set_switches(risc, 1);
        break;
      }
      case 'H': {
        risc_set_host_fs(risc, host_fs_new(optarg));
        break;
      }
      default: {
        usage();
      }
    }
  }

  if (mem_option || mode_count != 0 || dynsize_option) {
    if (mode_count == 0) {
      all_modes[0].width = RISC_FRAMEBUFFER_WIDTH;
      all_modes[0].height = RISC_FRAMEBUFFER_HEIGHT;
      all_modes[0].depth = 1;
      all_modes[0].index = 0;
      mode_count = 1;
    }
    all_modes[mode_count].width = 0;
    all_modes[mode_count].height = 0;
    all_modes[mode_count].depth = 0;
    all_modes[mode_count].index = -1;
    risc_configure_memory(risc, mem_option, all_modes, dynsize_option);
    if (dynsize_option) {
      risc_size_hint(risc, all_modes[0].width, all_modes[0].height);
    }
  }

  if (optind == argc - 1) {
    risc_set_spi(risc, 1, disk_new(argv[optind]));
  } else if (optind == argc && boot_from_serial) {
    /* Allow diskless boot */
    risc_set_spi(risc, 1, disk_new(NULL));
  } else {
    usage();
  }

  if (serial_in || serial_out) {
    if (!serial_in) {
      serial_in = "/dev/null";
    }
    if (!serial_out) {
      serial_out = "/dev/null";
    }
    risc_set_serial(risc, raw_serial_new(serial_in, serial_out));
  }

  current_mode = risc_get_display_mode(risc, &seamless);
  risc_rect.w = current_mode->width;
  risc_rect.h = current_mode->height;
  previous_mode_index = current_mode->index;

  if (SDL_Init(SDL_INIT_VIDEO) != 0) {
    fail(1, "Unable to initialize SDL: %s", SDL_GetError());
  }
  atexit(SDL_Quit);
  SDL_EnableScreenSaver();
  SDL_ShowCursor(false);
  SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "best");

  int window_flags = SDL_WINDOW_HIDDEN;
  int display = 0;
  if (fullscreen) {
    window_flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
    display = best_display(&risc_rect);
  }
  if (zoom == 0) {
    SDL_Rect bounds;
    if (SDL_GetDisplayBounds(display, &bounds) == 0 &&
        bounds.h >= risc_rect.h * 2 && bounds.w >= risc_rect.w * 2) {
      zoom = 2;
    } else {
      zoom = 1;
    }
  }
  SDL_Window *window = SDL_CreateWindow("Project Oberon",
                                        SDL_WINDOWPOS_UNDEFINED_DISPLAY(display),
                                        SDL_WINDOWPOS_UNDEFINED_DISPLAY(display),
                                        (int)(risc_rect.w * zoom),
                                        (int)(risc_rect.h * zoom),
                                        window_flags);
  if (window == NULL) {
    fail(1, "Could not create window: %s", SDL_GetError());
  }

  SDL_Renderer *renderer = SDL_CreateRenderer(window, -1, 0);
  if (renderer == NULL) {
    fail(1, "Could not create renderer: %s", SDL_GetError());
  }

  SDL_Texture *texture = SDL_CreateTexture(renderer,
                                           SDL_PIXELFORMAT_ARGB8888,
                                           SDL_TEXTUREACCESS_STREAMING,
                                           risc_rect.w,
                                           risc_rect.h);
  if (texture == NULL) {
    fail(1, "Could not create texture: %s", SDL_GetError());
  }

  SDL_Rect display_rect;
  double display_scale = scale_display(window, &risc_rect, &display_rect);
  update_texture(risc, texture, &risc_rect, current_mode->depth);
  SDL_ShowWindow(window);
  SDL_RenderClear(renderer);
  SDL_RenderCopy(renderer, texture, &risc_rect, &display_rect);
  SDL_RenderPresent(renderer);

  bool done = false;
  bool mouse_was_offscreen = false;
  while (!done) {
    uint32_t frame_start = SDL_GetTicks();

    SDL_Event event;
    while (SDL_PollEvent(&event)) {
      switch (event.type) {
        case SDL_QUIT: {
          done = true;
          break;
        }

        case SDL_WINDOWEVENT: {
          if (event.window.event == SDL_WINDOWEVENT_RESIZED) {
            display_scale = scale_display(window, &risc_rect, &display_rect);
            if (dynsize_option) {
              int win_w, win_h;
              SDL_GetWindowSize(window, &win_w, &win_h);
              risc_size_hint(risc, (int)(win_w/zoom), (int)(win_h/zoom));
            }
          }
          break;
        }

        case SDL_DROPFILE: {
          char *dropped_file = event.drop.file;
          char *dropped_file_name = strrchr(dropped_file, '/');
          if (dropped_file_name == NULL)
            dropped_file_name = strrchr(dropped_file, '\\');
          if (dropped_file_name != NULL)
            dropped_file_name++;
          else
            dropped_file_name = dropped_file;
          printf("Dropped %s [%s]\n", dropped_file, dropped_file_name);
          FILE *f = fopen("PCLink.REC", "w");
          fputs(dropped_file_name, f);
          fputs(" ", f);
          fputs(dropped_file, f);
          fclose(f);
          SDL_free(dropped_file);
          break;
        }

        case SDL_MOUSEMOTION: {
          int scaled_x = (int)round((event.motion.x - display_rect.x) / display_scale);
          int scaled_y = (int)round((event.motion.y - display_rect.y) / display_scale);
          int x = clamp(scaled_x, 0, risc_rect.w - 1);
          int y = clamp(scaled_y, 0, risc_rect.h - 1);
          bool mouse_is_offscreen = x != scaled_x || y != scaled_y;
          if (mouse_is_offscreen != mouse_was_offscreen) {
            SDL_ShowCursor(mouse_is_offscreen);
            mouse_was_offscreen = mouse_is_offscreen;
          }
          risc_mouse_moved(risc, x, risc_rect.h - y - 1);
          break;
        }

        case SDL_MOUSEBUTTONDOWN:
        case SDL_MOUSEBUTTONUP: {
          bool down = event.button.state == SDL_PRESSED;
          risc_mouse_button(risc, event.button.button, down);
          break;
        }

        case SDL_KEYDOWN:
        case SDL_KEYUP: {
          bool down = event.key.state == SDL_PRESSED;
          switch (map_keyboard_event(&event.key)) {
            case ACTION_RESET: {
              risc_reset(risc);
              break;
            }
            case ACTION_TOGGLE_FULLSCREEN: {
              fullscreen ^= true;
              if (fullscreen) {
                SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN_DESKTOP);
              } else {
                SDL_SetWindowFullscreen(window, 0);
              }
              break;
            }
            case ACTION_QUIT: {
              SDL_PushEvent(&(SDL_Event){ .type=SDL_QUIT });
              break;
            }
            case ACTION_FAKE_MOUSE1: {
              risc_mouse_button(risc, 1, down);
              break;
            }
            case ACTION_FAKE_MOUSE2: {
              risc_mouse_button(risc, 2, down);
              break;
            }
            case ACTION_FAKE_MOUSE3: {
              risc_mouse_button(risc, 3, down);
              break;
            }
            case ACTION_OBERON_INPUT: {
              uint8_t ps2_bytes[MAX_PS2_CODE_LEN];
              int len = ps2_encode(event.key.keysym.scancode, down, ps2_bytes);
              risc_keyboard_input(risc, ps2_bytes, len);
              break;
            }
          }
        }
      }
    }

    risc_set_time(risc, frame_start);
    for (int i=0; i<MSPF; i++) {
      risc_run(risc, CPU_HZ / 1000 * MSPF);
      uint32_t frame_end = SDL_GetTicks();
      int delay = frame_start + MSPF - frame_end;
      if (delay > 0) {
        SDL_Delay(delay);
      }
      risc_trigger_interrupt(risc);
    }
    current_mode = risc_get_display_mode(risc, &seamless);
    if (current_mode != NULL && current_mode->index != previous_mode_index) {
      SDL_DestroyTexture(texture);
      risc_rect.w = current_mode->width;
      risc_rect.h = current_mode->height;
      previous_mode_index = current_mode->index;
      SDL_SetWindowSize(window, (int)(risc_rect.w * zoom), (int)(risc_rect.h * zoom));
      texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, risc_rect.w, risc_rect.h);
      if (texture == NULL) {
        fail(1, "Could not create texture: %s", SDL_GetError());
      }
      display_scale = scale_display(window, &risc_rect, &display_rect);
    }
    if (seamless && !resizable) {
      SDL_SetWindowResizable(window, true);
      resizable = true;
    }
    update_texture(risc, texture, &risc_rect, current_mode->depth);
    SDL_RenderClear(renderer);
    SDL_RenderCopy(renderer, texture, &risc_rect, &display_rect);
    SDL_RenderPresent(renderer);
  }
  return 0;
}


static int best_display(const SDL_Rect *rect) {
  int best = 0;
  int display_cnt = SDL_GetNumVideoDisplays();
  for (int i = 0; i < display_cnt; i++) {
    SDL_Rect bounds;
    if (SDL_GetDisplayBounds(i, &bounds) == 0 &&
        bounds.h == rect->h && bounds.w >= rect->w) {
      best = i;
      if (bounds.w == rect->w) {
        break;  // exact match
      }
    }
  }
  return best;
}

static int clamp(int x, int min, int max) {
  if (x < min) return min;
  if (x > max) return max;
  return x;
}

static enum Action map_keyboard_event(SDL_KeyboardEvent *event) {
  for (size_t i = 0; i < sizeof(key_map) / sizeof(key_map[0]); i++) {
    if ((event->state == key_map[i].state) &&
        (event->keysym.sym == key_map[i].sym) &&
        ((key_map[i].mod1 == 0) || (event->keysym.mod & key_map[i].mod1)) &&
        ((key_map[i].mod2 == 0) || (event->keysym.mod & key_map[i].mod2))) {
      return key_map[i].action;
    }
  }
  return ACTION_OBERON_INPUT;
}

static void show_leds(const struct RISC_LED *leds, uint32_t value) {
  printf("LEDs: ");
  for (int i = 7; i >= 0; i--) {
    if (value & (1 << i)) {
      printf("%d", i);
    } else {
      printf("-");
    }
  }
  printf("\n");
}

static double scale_display(SDL_Window *window, const SDL_Rect *risc_rect, SDL_Rect *display_rect) {
  int win_w, win_h;
  SDL_GetWindowSize(window, &win_w, &win_h);
  double oberon_aspect = (double)risc_rect->w / risc_rect->h;
  double window_aspect = (double)win_w / win_h;

  double scale;
  if (oberon_aspect > window_aspect) {
    scale = (double)win_w / risc_rect->w;
  } else {
    scale = (double)win_h / risc_rect->h;
  }

  int w = (int)ceil(risc_rect->w * scale);
  int h = (int)ceil(risc_rect->h * scale);
  *display_rect = (SDL_Rect){
    .w = w, .h = h,
    .x = (win_w - w) / 2,
    .y = (win_h - h) / 2
  };
  return scale;
}

// Only used in update_texture(), but some systems complain if you
// allocate three megabyte on the stack.
static uint32_t pixel_buf[MAX_WIDTH * MAX_HEIGHT];

static void update_texture(struct RISC *risc, SDL_Texture *texture, const SDL_Rect *risc_rect, int depth) {
  struct Damage damage = risc_get_framebuffer_damage(risc);
  if (damage.y1 <= damage.y2) {
    uint32_t *in = risc_get_framebuffer_ptr(risc);
    uint32_t *pal = depth > 1 ? risc_get_palette_ptr(risc) : NULL;
    uint32_t out_idx = 0;

    for (int line = damage.y2; line >= damage.y1; line--) {
      int line_start = line * (risc_rect->w / (32 / depth));
      for (int col = damage.x1; col <= damage.x2; col++) {
        uint32_t pixels = in[line_start + col];
        if (depth == 4) {
          for (int b = 0; b < 8; b++) {
            pixel_buf[out_idx] = pal[pixels & 0xF];
            pixels >>= 4;
            out_idx++;
          }
        } else if (depth == 8) {
          for (int b = 0; b < 4; b++) {
            pixel_buf[out_idx] = pal[pixels & 0xFF];
            pixels >>= 8;
            out_idx++;
          }
        } else {
          for (int b = 0; b < 32; b++) {
            pixel_buf[out_idx] = (pixels & 1) ? WHITE : BLACK;
            pixels >>= 1;
            out_idx++;
          }
        }
      }
    }

    SDL_Rect rect = {
      .x = damage.x1 * (32 / depth),
      .y = risc_rect->h - damage.y2 - 1,
      .w = (damage.x2 - damage.x1 + 1) * (32 / depth),
      .h = (damage.y2 - damage.y1 + 1)
    };
    SDL_UpdateTexture(texture, &rect, pixel_buf, rect.w * 4);
  }
}
