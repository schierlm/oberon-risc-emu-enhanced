#include <SDL.h>
#include <SDL2/SDL_net.h>
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

static uint32_t BLACK = 0x657b83, WHITE = 0xfdf6e3;
//static uint32_t BLACK = 0x000000, WHITE = 0xFFFFFF;
//static uint32_t BLACK = 0x0000FF, WHITE = 0xFFFF00;
//static uint32_t BLACK = 0x000000, WHITE = 0x00FF00;

#define MAX_HEIGHT 2048
#define MAX_WIDTH  2048

static int best_display(const SDL_Rect *rect);
static int clamp(int x, int min, int max);
static enum Action map_keyboard_event(SDL_KeyboardEvent *event);
static void show_leds(const struct RISC_LED *leds, uint32_t value);
static double scale_display(SDL_Window *window, const SDL_Rect *risc_rect, SDL_Rect *display_rect);
static void update_texture(struct RISC *risc, SDL_Texture *texture, const SDL_Rect *risc_rect, bool color);
static void wiznet_write(const struct RISC_HostFS *wiznet_wiznet, uint32_t value, uint32_t *ram);

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

#define MAX_WIZNET_SOCKETS 256
#define WIZNET_BUFSIZE 1024

struct WizNetTCP {
        TCPsocket sock;
        uint32_t len;
        bool closed;
        uint8_t buf[WIZNET_BUFSIZE];
};

struct WizNet {
  struct RISC_HostFS wiznet;
  SDLNet_SocketSet sockset;
  UDPsocket udpsock[MAX_WIZNET_SOCKETS];
  struct WizNetTCP* tcpsock[MAX_WIZNET_SOCKETS];
  TCPsocket listener[MAX_WIZNET_SOCKETS];
};

static struct option long_options[] = {
  { "zoom",             required_argument, NULL, 'z' },
  { "fullscreen",       no_argument,       NULL, 'f' },
  { "leds",             no_argument,       NULL, 'L' },
  { "rtc",              no_argument,       NULL, 'r' },
  { "mem",              required_argument, NULL, 'm' },
  { "size",             required_argument, NULL, 's' },
  { "serial-in",        required_argument, NULL, 'I' },
  { "serial-out",       required_argument, NULL, 'O' },
  { "boot-from-serial", no_argument,       NULL, 'S' },
  { "color",            no_argument,       NULL, 'c' },
  { "hostfs",           required_argument, NULL, 'H' },
  { "wiznet",           no_argument,       NULL, 'W' },
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
       "  --color               Use 16 color mode (requires modified Display.Mod)\n"
       "  --size WIDTHxHEIGHT   Set framebuffer size\n"
       "  --boot-from-serial    Boot from serial line (disk image not required)\n"
       "  --serial-in FILE      Read serial input from FILE\n"
       "  --serial-out FILE     Write serial output to FILE\n"
       "  --hostfs DIRECTORY    Use DIRECTORY as HostFS directory\n"
       "  --wiznet              Enable WizNet emulation\n"
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
  SDL_Rect risc_rect = {
    .w = RISC_FRAMEBUFFER_WIDTH,
    .h = RISC_FRAMEBUFFER_HEIGHT
  };
  bool size_option = false, rtc_option = false, color_option = false;
  int mem_option = 0;
  const char *serial_in = NULL;
  const char *serial_out = NULL;
  bool boot_from_serial = false;

  int opt;
  while ((opt = getopt_long(argc, argv, "z:fLrm:s:I:O:ScH:W", long_options, NULL)) != -1) {
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
      case 'r': {
        rtc_option = true;
        break;
      }
      case 'm': {
        if (sscanf(optarg, "%d", &mem_option) != 1) {
          usage();
        }
        break;
      }
      case 's': {
        int w, h;
        if (sscanf(optarg, "%dx%d", &w, &h) != 2) {
          usage();
        }
        risc_rect.w = clamp(w, 32, MAX_WIDTH) & ~31;
        risc_rect.h = clamp(h, 32, MAX_HEIGHT);
        size_option = true;
        break;
      }
      case 'c': {
        color_option = true;
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
      case 'W': {
        if (SDLNet_Init() != 0) {
          fail(1, "Unable to initialize SDLNet: %s", SDLNet_GetError());
        }
        struct WizNet *wiznet = calloc(1, sizeof(*wiznet));
        wiznet->wiznet = (struct RISC_HostFS) {
          .write = wiznet_write
        };
        wiznet->sockset = SDLNet_AllocSocketSet(MAX_WIZNET_SOCKETS);
        if(!wiznet->sockset) {
          fail(1, "Unable to allocate socket set: %s\n", SDLNet_GetError());
        }
        risc_set_wiznet(risc, &wiznet->wiznet);
        break;
      }
      default: {
        usage();
      }
    }
  }

  if (mem_option || size_option || rtc_option || color_option) {
    risc_configure_memory(risc, mem_option, rtc_option, risc_rect.w, risc_rect.h, color_option);
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
  update_texture(risc, texture, &risc_rect, color_option);
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
    risc_run(risc, CPU_HZ / FPS);

    update_texture(risc, texture, &risc_rect, color_option);
    SDL_RenderClear(renderer);
    SDL_RenderCopy(renderer, texture, &risc_rect, &display_rect);
    SDL_RenderPresent(renderer);

    uint32_t frame_end = SDL_GetTicks();
    int delay = frame_start + 1000/FPS - frame_end;
    if (delay > 0) {
      SDL_Delay(delay);
    }
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

static void update_texture(struct RISC *risc, SDL_Texture *texture, const SDL_Rect *risc_rect, bool color) {
  struct Damage damage = risc_get_framebuffer_damage(risc);
  if (damage.y1 <= damage.y2) {
    uint32_t *in = risc_get_framebuffer_ptr(risc);
    uint32_t *pal = color ? risc_get_palette_ptr(risc) : NULL;
    uint32_t out_idx = 0;

    for (int line = damage.y2; line >= damage.y1; line--) {
      int line_start = line * (risc_rect->w / (color ? 8 : 32));
      for (int col = damage.x1; col <= damage.x2; col++) {
        uint32_t pixels = in[line_start + col];
        if (color) {
          for (int b = 0; b < 8; b++) {
            pixel_buf[out_idx] = pal[pixels & 0xF];
            pixels >>= 4;
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
      .x = damage.x1 * (color ? 8 : 32),
      .y = risc_rect->h - damage.y2 - 1,
      .w = (damage.x2 - damage.x1 + 1) * (color ? 8 : 32),
      .h = (damage.y2 - damage.y1 + 1)
    };
    SDL_UpdateTexture(texture, &rect, pixel_buf, rect.w * 4);
  }
}

static void wiznet_write(const struct RISC_HostFS *wiznet_wiznet, uint32_t value, uint32_t *ram) {
  struct WizNet *wiznet = (struct WizNet *)wiznet_wiznet;

  // process pending socket activity
  if (SDLNet_CheckSockets(wiznet->sockset, 0) > 0) {
    for (int i = 0; i < MAX_WIZNET_SOCKETS; i++) {
      if (wiznet->tcpsock[i] != NULL && wiznet->tcpsock[i]->sock != NULL) {
        struct WizNetTCP* tcpsock = wiznet->tcpsock[i];
        while (!tcpsock->closed && tcpsock->len < WIZNET_BUFSIZE && SDLNet_SocketReady(tcpsock->sock)) {
          if (SDLNet_TCP_Recv(tcpsock->sock,&tcpsock->buf[tcpsock->len],1) > 0) {
             tcpsock->len++;
          } else {
             tcpsock->closed = true;
             break;
          }
          SDLNet_CheckSockets(wiznet->sockset, 0);
        }
      }
    }
  }

  uint32_t offset = value / 4;
  switch(ram[offset]) {
    case 0x10001: { // IP.StrToAdr + DNS.HostByName
      IPaddress addr;
      if (SDLNet_ResolveHost (&addr, (char*)(ram+offset+3), 0) != 0) {
          fprintf(stderr, "Host lookup %s failed: %s\n", (char*)(ram+offset+3), SDLNet_GetError());
          ram[offset+1] = 3601;
          ram[offset+2] = 0;
      } else {
          ram[offset+1] = 0;
          ram[offset+2] = SDL_SwapBE32(addr.host);
      }
      break;
    }
    case 0x10002: { // IP.AdrToStr
      int adr = SDL_SwapBE32(ram[offset+2]);
      sprintf((char*) (ram+offset+3), "%d.%d.%d.%d", (uint8_t)(adr), (uint8_t)(adr>>8), (uint8_t)(adr>>16), (uint8_t)(adr>>24));
      ram[offset+1] = 0;
      break;
    }
    case 0x10003: { // DNS.HostByNumber
      int adr = SDL_SwapBE32(ram[offset+2]);
      IPaddress addr;
      addr.host = adr;
      const char *host;
      if (!(host = SDLNet_ResolveIP(&addr))) {
        ram[offset+1] = 3601;
        sprintf((char*) (ram+offset+3), "%d.%d.%d.%d", (uint8_t)(adr), (uint8_t)(adr>>8), (uint8_t)(adr>>16), (uint8_t)(adr>>24));
      } else {
        ram[offset+1] = 0;
        strncpy((char*) (ram+offset+3), host, 128);
      }
      break;
    }
    case 0x10004: { // UDP.Open
      uint32_t lport = ram[offset+3];
      uint32_t socketid = 0;
      while (socketid < MAX_WIZNET_SOCKETS && wiznet->udpsock[socketid] != NULL)
        socketid++;
      ram[offset+2] = socketid;
      if (socketid == MAX_WIZNET_SOCKETS) {
        ram[offset+1] = 9999;
      } else {
        wiznet->udpsock[socketid] = SDLNet_UDP_Open((uint16_t)lport);
        if(!wiznet->udpsock[socketid]) {
          fprintf(stderr, "Opening UDP port on %d failed: %s\n", lport, SDLNet_GetError());
          ram[offset+1] = 9999;
        } else {
          IPaddress *address = SDLNet_UDP_GetPeerAddress(wiznet->udpsock[socketid], -1);
          ram[offset+3] = SDL_SwapBE16(address->port);
          ram[offset+1] = 0;
        }
      }
      break;
    }
    case 0x10005: { // UDP.Close
      uint32_t socketid = ram[offset+2];
      if (socketid < MAX_WIZNET_SOCKETS && wiznet->udpsock[socketid] != NULL) {
        SDLNet_UDP_Close(wiznet->udpsock[socketid]);
        wiznet->udpsock[socketid] = NULL;
        ram[offset+1] = 0;
      } else {
        ram[offset+1] = 3505;
      }
      break;
    }
    case 0x10006: { // UDP.Send
      uint32_t socketid = ram[offset+2], len = ram[offset+5];
      if (socketid < MAX_WIZNET_SOCKETS && wiznet->udpsock[socketid] != NULL) {
        UDPpacket *packet = SDLNet_AllocPacket(len);
        if(!packet) {
          fprintf(stderr, "Allocating UDP packet of size %d failed: %s\n", len, SDLNet_GetError());
          ram[offset+1] = 9999;
        } else {
          packet->len = len;
          memcpy(packet->data, (void*) &ram[offset+6], len);
          packet->address.host = SDL_SwapBE32(ram[offset+3]);
          packet->address.port = SDL_SwapBE16((uint16_t) ram[offset+4]);
          if (!SDLNet_UDP_Send(wiznet->udpsock[socketid], -1, packet)) {
            fprintf(stderr, "Sending UDP packet failed: %s\n", SDLNet_GetError());
            ram[offset+1] = 9999;
          } else {
            ram[offset+1] = 0;
          }
          SDLNet_FreePacket(packet);
        }
      } else {
        ram[offset+1] = 3505;
      }
      break;
    }
    case 0x10007: { // UDP.Receive
      uint32_t socketid = ram[offset+2], len = ram[offset+5];
      if (socketid < MAX_WIZNET_SOCKETS && wiznet->udpsock[socketid] != NULL) {
        UDPpacket *packet = SDLNet_AllocPacket(len);
        if(!packet) {
          fprintf(stderr, "Allocating UDP packet of size %d failed: %s\n", len, SDLNet_GetError());
          ram[offset+1] = 9999;
          ram[offset+5] = 0;
        } else {
          uint32_t wait_end = SDL_GetTicks() + ram[offset+6];
          int count;
          while (true) {
            count = SDLNet_UDP_Recv(wiznet->udpsock[socketid], packet);
            if (count != 0) break;
            int delay = wait_end - SDL_GetTicks();
            if (delay > 50) {
              SDL_Delay(50);
            } else if (delay > 0) {
              SDL_Delay(delay);
            } else {
              break;
            }
          }
          if (count == -1) { // error
            fprintf(stderr, "Receiving UDP packet failed: %s\n", SDLNet_GetError());
            ram[offset+1] = 9999;
            ram[offset+5] = 0;
          } else if (count == 0) { // no packet received -> timed out
            ram[offset+1] = 3704;
            ram[offset+5] = 0;
          } else { // packet received
            ram[offset+1] = 0;
            ram[offset+3] = SDL_SwapBE32(packet->address.host);
            ram[offset+4] = SDL_SwapBE16(packet->address.port);
            ram[offset+5] = packet->len;
            memcpy((void*)&ram[offset+7], packet->data, packet->len);
          }
          SDLNet_FreePacket(packet);
        }
      } else {
        ram[offset+1] = 3505;
        ram[offset+5] = 0;
      }
      break;
    }
    case 0x10008: { // TCP.Open
      uint32_t lport = ram[offset+3];
      uint32_t fip = SDL_SwapBE32(ram[offset+4]);
      uint32_t fport = ram[offset+5];
      if (fip == 0 && fport == 0) { // listen
        uint32_t socketid = 0;
        while (socketid < MAX_WIZNET_SOCKETS && wiznet->listener[socketid] != NULL)
          socketid++;
        ram[offset+2] = socketid + MAX_WIZNET_SOCKETS*2;
        if (socketid == MAX_WIZNET_SOCKETS) {
          ram[offset+1] = 3706;
        } else {
          IPaddress ip;
          if(SDLNet_ResolveHost(&ip, NULL, (uint16_t)lport) == 0) {
             wiznet->listener[socketid] = SDLNet_TCP_Open(&ip);
          }
          if(!wiznet->listener[socketid]) {
            fprintf(stderr, "Opening TCP listener on %d failed: %s\n", lport, SDLNet_GetError());
            ram[offset+1] = 3705;
          } else {
            ram[offset+1] = 0;
          }
        }
      } else { // connect
        uint32_t socketid = 0;
        while (socketid < MAX_WIZNET_SOCKETS && wiznet->tcpsock[socketid] != NULL)
          socketid++;
        ram[offset+2] = socketid;
        if (socketid == MAX_WIZNET_SOCKETS) {
          ram[offset+1] = 3706;
        } else {
          struct WizNetTCP *sock = calloc(1, sizeof(*sock));
          wiznet->tcpsock[socketid] = sock;
          IPaddress ip;
          ip.host = fip;
          ip.port = SDL_SwapBE16((uint16_t)fport);
          sock->sock=SDLNet_TCP_Open(&ip);
          if(!sock->sock) {
            fprintf(stderr, "Opening TCP socket to host %d.%d.%d.%d port %d failed: %s\n", (uint8_t)(fip), (uint8_t)(fip>>8), (uint8_t)(fip>>16), (uint8_t)(fip>>24), fport, SDLNet_GetError());
            ram[offset+1] = 3701;
          } else if (SDLNet_TCP_AddSocket(wiznet->sockset,sock->sock) == -1) {
             fprintf(stderr, "Adding socket to socket set failed: %s\n", SDLNet_GetError());
             ram[offset+1] = 3702;
          } else {
            ram[offset+1] = 0;
          }
        }
      }
      break;
    }
    case 0x10009: { // TCP.SendChunk
      uint32_t socketid = ram[offset+2];
      uint32_t len = ram[offset+3];
      if (socketid < MAX_WIZNET_SOCKETS && wiznet->tcpsock[socketid] != NULL) {
        if (SDLNet_TCP_Send(wiznet->tcpsock[socketid]->sock, (void*) &ram[offset+5], len) < (int)len) {
          fprintf(stderr, "Sending %d bytes via TCP failed: %s\n", len, SDLNet_GetError());
          ram[offset+1] = 3702;
        } else {
          ram[offset+1] = 0;
        }
      } else {
        ram[offset+1] = 3706;
      }
      break;
    }
    case 0x1000A: { // TCP.ReceiveChunk
      uint32_t socketid = ram[offset+2];
      uint32_t len = ram[offset+3];
      uint32_t minlen = ram[offset+4];
      if (socketid < MAX_WIZNET_SOCKETS && wiznet->tcpsock[socketid] != NULL) {
        if (wiznet->tcpsock[socketid]->len == 0 && wiznet->tcpsock[socketid]->closed) {
          ram[offset+1] = 3707;
          ram[offset+3] = 0;
        } else if (wiznet->tcpsock[socketid]->len < minlen) {
          ram[offset+1] = 3704;
          ram[offset+3] = 0;
        } else {
          if (len > wiznet->tcpsock[socketid]->len) {
            len = wiznet->tcpsock[socketid]->len;
          }
          minlen = wiznet->tcpsock[socketid]->len-len;
          ram[offset+1] = 0;
          ram[offset+3] = len;
          memcpy((void*)&ram[offset+5], wiznet->tcpsock[socketid]->buf, len);
          memcpy(wiznet->tcpsock[socketid]->buf, &wiznet->tcpsock[socketid]->buf[len], minlen);
          wiznet->tcpsock[socketid]->len = minlen;
        }
      } else {
        ram[offset+1] = 3706;
        ram[offset+3] = 0;
      }
      break;
    }
    case 0x1000B: { // TCP.Available
      uint32_t socketid = ram[offset+2];
      if (socketid < MAX_WIZNET_SOCKETS && wiznet->tcpsock[socketid] != NULL) {
        ram[offset+1] = wiznet->tcpsock[socketid]->len + (wiznet->tcpsock[socketid]->closed ? 1 : 0);
      } else {
        ram[offset+1] = 0;
      }
      break;
    }
    case 0x1000C: { // TCP.Close
      uint32_t socketid = ram[offset+2];
      if (socketid < MAX_WIZNET_SOCKETS && wiznet->tcpsock[socketid] != NULL) {
        if (SDLNet_TCP_DelSocket(wiznet->sockset, wiznet->tcpsock[socketid]->sock) == -1) {
          fprintf(stderr, "Removing socket from socket set failed: %s\n", SDLNet_GetError());
        }
        SDLNet_TCP_Close(wiznet->tcpsock[socketid]->sock);
        free(wiznet->tcpsock[socketid]);
        wiznet->tcpsock[socketid] = NULL;
        ram[offset+1] = 0;
      } else if (socketid >= MAX_WIZNET_SOCKETS * 2 && socketid < MAX_WIZNET_SOCKETS * 3 && wiznet->listener[socketid-MAX_WIZNET_SOCKETS*2] != NULL) {
        SDLNet_TCP_Close(wiznet->listener[socketid-MAX_WIZNET_SOCKETS*2]);
        wiznet->listener[socketid-MAX_WIZNET_SOCKETS*2] = NULL;
        ram[offset+1] = 0;
      } else {
        ram[offset+1] = 3706;
      }
      break;
    }
    case 0x1000D: { // TCP.Accept
      uint32_t socketid = ram[offset+2] - MAX_WIZNET_SOCKETS*2;
      if (socketid < MAX_WIZNET_SOCKETS && wiznet->listener[socketid] != NULL) {
        uint32_t clientid = 0;
        while (clientid < MAX_WIZNET_SOCKETS && wiznet->tcpsock[clientid] != NULL)
          clientid++;
        ram[offset+1] = 0;
        if (clientid == MAX_WIZNET_SOCKETS) {
          ram[offset+3] = -1;
        } else {
          TCPsocket clientsock = SDLNet_TCP_Accept(wiznet->listener[socketid]);
          if (clientsock == NULL) {
            ram[offset+3] = -1;
          } else if (SDLNet_TCP_AddSocket(wiznet->sockset,clientsock) == -1) {
             fprintf(stderr, "Adding accepted socket to socket set failed: %s\n", SDLNet_GetError());
             ram[offset+3] = -1;
          } else {
            struct WizNetTCP *sock = calloc(1, sizeof(*sock));
            wiznet->tcpsock[clientid] = sock;
            sock->sock=clientsock;
            IPaddress *remote_ip = SDLNet_TCP_GetPeerAddress(clientsock);
            ram[offset+3] = clientid;
            if(!remote_ip) {
              printf("Obtain remote IP failed: %s\n", SDLNet_GetError());
              ram[offset+4] = 0;
              ram[offset+5] = 0;
            }
            else {
              ram[offset+4] = SDL_SwapBE32(remote_ip->host);
              ram[offset+5] = SDL_SwapBE16(remote_ip->port);
            }
          }
        }
      } else {
        ram[offset+1] = 3706;
        ram[offset+3] = 0;
      }
      break;
    }
  }
}
