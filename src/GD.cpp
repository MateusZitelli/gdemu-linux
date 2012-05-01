#include <SDL.h>
#include <time.h>

#include "SPI.h"
#include "GD.h"

#define RAM_SIZE 0x8000
static byte RAM[RAM_SIZE];

#define WINDOW_WIDTH 400
#define WINDOW_HEIGHT 300
#define WINDOW_ZOOM 2

void GDClass::begin() {
  SDL_Init(SDL_INIT_VIDEO);
  SDL_SetVideoMode(WINDOW_WIDTH * WINDOW_ZOOM, WINDOW_HEIGHT * WINDOW_ZOOM, 15, SDL_HWSURFACE|SDL_DOUBLEBUF);

  // Hide all sprites
  for(int spr = 0; spr < 512; ++ spr) {
    *(RAM + RAM_SPR + (spr << 2) + 0) = lowByte(400);
    *(RAM + RAM_SPR + (spr << 2) + 1) = highByte(400);
    *(RAM + RAM_SPR + (spr << 2) + 2) = lowByte(400);
    *(RAM + RAM_SPR + (spr << 2) + 3) = highByte(400);
  }
}

void GDClass::end() {
  SDL_Quit();
}

void GDClass::waitvblank() {
}

void GDClass::copy(unsigned int addr, prog_uchar *src, int count) {
  addr %= RAM_SIZE;
  if(addr + count > RAM_SIZE) count = RAM_SIZE - addr;
  memcpy(RAM + addr, src, count);
}

void GDClass::fill(int addr, byte v, unsigned int count) {
  addr %= RAM_SIZE;
  if(addr + count > RAM_SIZE) count = RAM_SIZE - addr;
  memset(RAM + addr, v, count);
}

void GDClass::wr(unsigned int addr, byte v) {
  addr %= RAM_SIZE;
  RAM[addr] = v;
}

void GDClass::wr16(unsigned int addr, unsigned int v) {
  addr %= RAM_SIZE;
  //((uint16_t *)RAM)[addr >> 1] = v;
  RAM[addr + 1] = (v & 0xff00) >> 8;
  RAM[addr + 0] = (v & 0x00ff) >> 0;
}

#include "font8x8.h"
static byte stretch[16] = {
  0x00, 0x03, 0x0c, 0x0f,
  0x30, 0x33, 0x3c, 0x3f,
  0xc0, 0xc3, 0xcc, 0xcf,
  0xf0, 0xf3, 0xfc, 0xff
};

void GDClass::ascii() {
  long i;
  for (i = 0; i < 768; i++) {
    byte b = font8x8[i];
    byte h = stretch[b >> 4];
    byte l = stretch[b & 0xf];
    GD.wr(0x1000 + (16 * ' ') + (2 * i), h);
    GD.wr(0x1000 + (16 * ' ') + (2 * i) + 1, l);
  }
  for (i = 0x20; i < 0x80; i++) {
    GD.setpal(4 * i + 0, TRANSPARENT);
    GD.setpal(4 * i + 3, RGB(255,255,255));
  }
  GD.fill(RAM_PIC, ' ', 4096);
}

void GDClass::putstr(int x, int y, const char *s) {
  int len = strlen(s);
  int start = y * 64 + x;

  if(start + len > 4096) len = 4096 - start;
  for(int i = 0; i < len; ++ i) {
    wr(RAM_PIC + start + i, s[i]);
  }
}

void GDClass::setpal(int pal, unsigned int rgb) {
  wr16(RAM_PAL + (pal << 1), rgb);
}

void GDClass::sprite(int spr, int x, int y, byte image, byte palette, byte rot, byte jk) {
  byte *sprite_ptr = RAM + RAM_SPR + 4 * spr;
  sprite_ptr[0] = x & 0x00ff;
  sprite_ptr[1] = ((palette & 0x0f) << 4) | ((rot & 0x07) << 1) | ((x & 0x0100) >> 8);
  sprite_ptr[2] = y & 0x00ff;
  sprite_ptr[3] = ((jk & 0x01) << 7) | ((image & 0x3f) << 1) | ((y & 0x0100) >> 8);
}

void redraw_background(SDL_Surface *surface) {
  int min_x = (RAM[SCROLL_X] | ((RAM[SCROLL_X + 1] & 0x01) << 8)) >> 3;
  int min_y = (RAM[SCROLL_Y] | ((RAM[SCROLL_Y + 1] & 0x01) << 8)) >> 3;
  uint16_t *pixels = (uint16_t *)surface->pixels;

  for(int y = min_y; y < min_y + 38; ++ y) {
    for(int x = min_x; x < min_x + 51; ++ x) {
      byte char_idx = RAM[RAM_PIC + (y & 0x3f) * 64 + (x & 0x3f)];
      byte *char_data = RAM + RAM_CHR + (char_idx << 4);
      uint16_t *color_data = (uint16_t *)(RAM + RAM_PAL) + (char_idx << 2);

      int first_pixel_x = (x << 3) - (RAM[SCROLL_X] | ((RAM[SCROLL_X + 1] & 0x01) << 8));
      int first_pixel_y = (y << 3) - (RAM[SCROLL_Y] | ((RAM[SCROLL_Y + 1] & 0x01) << 8));
      uint16_t *first_pixel = pixels + WINDOW_WIDTH * WINDOW_ZOOM * WINDOW_ZOOM * first_pixel_y + WINDOW_ZOOM * first_pixel_x;

      for(int py = 0; py < 8; ++ py) {
        for(int px = 0; px < 8; ++ px) {
          int pixel_address_in_char = (py << 3) | px;
          int bit_shift = (6 - ((pixel_address_in_char & 3) << 1));
          int color_index = (char_data[pixel_address_in_char >> 2] & (3 << bit_shift)) >> bit_shift;
          if(first_pixel_x + px >= 0 && first_pixel_x + px < WINDOW_WIDTH
              && first_pixel_y + py >= 0 && first_pixel_y + py < WINDOW_HEIGHT) {
            for(int i = 0; i < WINDOW_ZOOM; ++ i) {
              for(int j = 0; j < WINDOW_ZOOM; ++ j) {
                if(!(color_data[color_index] & 0x8000)) {
                  first_pixel[WINDOW_WIDTH * WINDOW_ZOOM * (WINDOW_ZOOM * py + j) + WINDOW_ZOOM * px + i] = color_data[color_index];
                }
              }
            }
          }
        }
      }
    }
  }
}

void redraw_sprites(SDL_Surface *surface) {
  uint16_t *pixels = (uint16_t *)surface->pixels;

  for(int spr_ = 0; spr_ < 256; ++ spr_) {
    byte *sprite_ptr = RAM + RAM_SPR + 4 * spr_;
    int x, y;
    byte rot, palette, image, jk;
    uint16_t palette_nb_colors = 0;
    uint16_t *palette_ptr;
    byte palette_mask, palette_shift;
    byte *sprite_image;

    x = sprite_ptr[0] | ((sprite_ptr[1] & 0x01) << 8);
    y = sprite_ptr[2] | ((sprite_ptr[3] & 0x01) << 8);
    rot = (sprite_ptr[1] & 0x0e) >> 1;
    palette = (sprite_ptr[1] & 0xf0) >> 4;
    image = (sprite_ptr[3] & 0x7e) >> 1;
    jk = (sprite_ptr[3] & 0x80) >> 7;

    if(palette & B00001000) {
      // 2-bit mode
      palette_nb_colors = 4;
      if(palette & 0x01) {
        palette_ptr = (uint16_t *)(RAM + PALETTE4B);
      } else {
        palette_ptr = (uint16_t *)(RAM + PALETTE4A);
      }
      switch((palette & 0x06) >> 1) {
        case 0:
          palette_mask = 0x03;
          palette_shift = 0;
          break;
        case 1:
          palette_mask = 0x0c;
          palette_shift = 2;
          break;
        case 2:
          palette_mask = 0x30;
          palette_shift = 4;
          break;
        case 3:
          palette_mask = 0xc0;
          palette_shift = 6;
          break;
      }
    } else if(palette & B00000100) {
      // 4-bit mode
      palette_nb_colors = 16;
      if(palette & 0x01) {
        palette_ptr = (uint16_t *)(RAM + PALETTE16B);
      } else {
        palette_ptr = (uint16_t *)(RAM + PALETTE16A);
      }
      if(palette & 0x02) {
        palette_mask = 0xf0;
        palette_shift = 4;
      } else {
        palette_mask = 0x0f;
        palette_shift = 0;
      }
    } else {
      // 8-bit mode
      palette_nb_colors = 256;
      palette_ptr = (uint16_t *)(RAM + RAM_SPRPAL + ((palette & 0x03) << 8));
      palette_mask = 0xff;
      palette_shift = 0;
    }

    sprite_image = RAM + RAM_SPRIMG + ((image & 0x3f) << 8);

    for(int spr_y = 0; spr_y < 16; ++ spr_y) {
      for(int spr_x = 0; spr_x < 16; ++ spr_x) {
        if(((x + spr_x) & 0x1ff) >= 0
            && ((x + spr_x) & 0x1ff) < WINDOW_WIDTH
            && ((y + spr_y) & 0x1ff) >= 0
            && ((y + spr_y) & 0x1ff) < WINDOW_HEIGHT) {
          for(int j = 0; j < WINDOW_ZOOM; ++ j) {
            for(int i = 0; i < WINDOW_ZOOM; ++ i) {
              byte color_index = (sprite_image[(spr_y << 4) | spr_x] & palette_mask) >> palette_shift;
              uint16_t color = palette_ptr[color_index];
              if(!(color & 0x8000)) {
                pixels[(((y + spr_y) & 0x1ff) * WINDOW_ZOOM + j) * WINDOW_ZOOM * WINDOW_WIDTH + ((x + spr_x) & 0x1ff) * WINDOW_ZOOM + i] = color;
              }
            }
          }
        }
      }
    }
  }
}

int main() {
  const struct timespec sleep_time = { 0, 10000000 }; // 10 ms
  byte do_exit = 0;
  SDL_Event event;
  SDL_Surface *surface;

  memset(RAM, 0, RAM_SIZE);

  setup();

  while(!do_exit && clock_nanosleep(CLOCK_MONOTONIC, 0, &sleep_time, NULL) == 0) {
    while(SDL_PollEvent(&event)) {
      switch(event.type) {
        case SDL_QUIT:
          do_exit = 1;
      }
    }

    surface = SDL_GetVideoSurface();

    SDL_LockSurface(surface);
    redraw_background(surface);
    redraw_sprites(surface);
    SDL_UnlockSurface(surface);

    SDL_Flip(surface);

    loop();
  }

  GD.end();
}
