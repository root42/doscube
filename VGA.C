#include <dos.h>
#include <mem.h>

#include "vga.h"

#define SET_MODE 0x00
#define VIDEO_INT 0x10
#define VGA_256_COLOR_MODE 0x13
#define TEXT_MODE 0x03

#define SCREEN_WIDTH 320
#define SCREEN_HEIGHT 200
#define ABS(a) ((a)>0?(a):-(a))
#define SETPIX(x,y,c) *(BUF + (dword)SCREEN_WIDTH * (y) + (x)) = c

byte far * const VGA=(byte far *)0xA0000000L;
byte far * BUF=(byte far *)0xA0000000L;

/* dimensions of each page and offset */
word vga_width = 320;
word vga_height = 200;
word vga_page[4];
word vga_current_page = 0;

void set_graphics_mode()
{
  set_mode(VGA_256_COLOR_MODE);
}

void set_text_mode()
{
  set_mode(TEXT_MODE);
}

void set_mode(byte mode)
{
  union REGS regs;
  regs.h.ah = SET_MODE;
  regs.h.al = mode;
  int86( VIDEO_INT, &regs, &regs );
}

void update_page_offsets()
{
  vga_page[0] = 0;
  vga_page[1] = ((dword)vga_width*vga_height) / 4;
  vga_page[2] = vga_page[1] * 2;
  vga_page[3] = vga_page[1] * 3;
}

void set_mode_y()
{
  set_mode( VGA_256_COLOR_MODE );
  update_page_offsets();
  /* disable chain 4 */
  outportb( SC_INDEX, MEMORY_MODE );
  outportb( SC_DATA, 0x06 );
  /* disable doubleword mode */
  outportb( CRTC_INDEX, UNDERLINE_LOCATION );
  outportb( CRTC_DATA, 0x00 );
  /* disable word mode */
  outportb( CRTC_INDEX, MODE_CONTROL );
  outportb( CRTC_DATA, 0xE3 );
  /* clear all VGA mem */
  outportb( SC_INDEX, MAP_MASK );
  outportb( SC_DATA, 0xFF );
  /* write 2^16 nulls */
  memset( VGA + 0x0000, 0x0, 0x8000 ); /* 0x10000 / 2 = 0x8000 */
  memset( VGA + 0x8000, 0x0, 0x8000 ); /* 0x10000 / 2 = 0x8000 */
  update_page_offsets();
}

void setpix( word page, int x, int y, byte c )
{
  outportb( SC_INDEX, MAP_MASK );
  outportb( SC_DATA, 1 << (x & 3) );
  VGA[ page + ((dword)vga_width * y >> 2) + (x >> 2) ] = c;
  /* x/4 is equal to x>>2 */
}

void page_flip( word *page1, word *page2 )
{
  byte ac;
  word temp;
  word high_address, low_address;

  temp = *page1;
  *page1 = *page2;
  *page2 = temp;

  high_address = HIGH_ADDRESS | ((*page1) & 0xFF00);
  low_address = LOW_ADDRESS | ((*page1) << 8);
  /*
    instead of:
    outportb( CRTC_INDEX, HIGH_ADDRESS );
    outportb( CRTC_DATA, (*page1 & 0xFF00) >> 8 );

    do this:
    high_address = HIGH_ADDRESS | (*page1 & 0xFF00);
    outport( CRTC_INDEX, high_address );
   */
  outport( CRTC_INDEX, high_address );
  outport( CRTC_INDEX, low_address );
  disable();
  while( inp( INPUT_STATUS ) & VRETRACE );
  while( !(inp( INPUT_STATUS ) & VRETRACE ) );
  enable();
  vga_current_page = *page1;
}

void copy2page( byte far *s, word page, int x0, int y0, int w, int h )
{
  int x,y;
  byte c;
  for( y = 0; y < h; y++ ) {
    for( x = 0; x < w; ++x ) {
      c = *(s + (dword)y * w + x);
      setpix( page, x0 + x, y0 + y, c);
    }
  }
}

void wait_for_retrace()
{
  while( inp( INPUT_STATUS ) & VRETRACE );
  while( ! (inp( INPUT_STATUS ) & VRETRACE) );
}

void set_palette(byte *palette)
{
  int i;

  outp( PALETTE_INDEX, 0 );
  for( i = 0; i < NUM_COLORS * 3; ++i ) {
    outp( PALETTE_DATA, palette[ i ] );
  }
}

void cycle_palette(byte *palette, int j)
{
  int i;

  outp( PALETTE_INDEX, 0 );
  for( i = 0; i < NUM_COLORS * 3; ++i ) {
    outp( PALETTE_DATA, palette[ (i + j * 3) % (NUM_COLORS * 3) ] );
  }
}

void blit2page( byte far *s[], word page, int x, int y, int w, int h )
{
  int j;
  byte p;
  dword screen_offset;
  dword bitmap_offset;

  for( p = 0; p < 4; p++ ) {
    outportb( SC_INDEX, MAP_MASK );
    outportb( SC_DATA, 1 << ((p + x) & 3) );
    bitmap_offset = x >> 2;
    screen_offset = ((dword)y * vga_width + x + p) >> 2;
    for(j=0; j<h; j++)
    {
      memcpy(
	VGA + page + screen_offset,
	s[p] + bitmap_offset ,
	w >> 2
	);
      bitmap_offset += vga_width >> 2;
      screen_offset += vga_width >> 2;
    }
  }
}

void blit4( byte far *s, int x, int y, int w, int h )
{
  int j,i;
  byte p;
  dword screen_offset;
  dword bitmap_offset;

  outportb( SC_INDEX, MAP_MASK );
  outportb( SC_DATA, 0x0F ); /* Write to all four planes at once */
  bitmap_offset = 0;
  screen_offset = ((dword)y * vga_width + x) >> 2;
  for(j=0; j<h; j++)
  {
    for(i = 0; i < 4; i++) {
      memcpy(
	VGA + screen_offset,
	s + bitmap_offset ,
	w
      );
      screen_offset += vga_width >> 2;
    }
    bitmap_offset += vga_width;
  }
}

void memcpy_rect(
  byte far *dest, byte far *src,
  word width, word height,
  word x0_src, word y0_src,
  word x0_dst, word y0_dst,
  word copy_width, word copy_height
)
{
  word i;
  byte far *s = src + x0_src + y0_src * width;
  byte far *d = dest + x0_dst + y0_dst * height;

  for( i = 0; i < copy_height; ++i, s += width, d += width ) {
    memcpy( d, s, copy_width );
  }
}

void
draw_line(int x0, int y0, int x1, int y1, char col)
{
    int dx,dy,sx,sy,error,e2;

    dx = ABS(x1 - x0);
    sx = x0 < x1 ? 1 : -1;
    dy = ABS(y1 - y0);
    sy = y0 < y1 ? 1 : -1;
    error = dx - dy;

    while(1)
    {
	if(x0 >= 0 && y0 >= 0 && x0 < SCREEN_WIDTH && y0 < SCREEN_HEIGHT)
            SETPIX(x0, y0, col);
        if( x0 == x1 && y0 == y1 ) break;
	e2 = 2 * error;
	if( e2 >= -dy ) {
            if( x0 == x1 ) break;
	    error -= dy;
	    x0 += sx;
        }
        if( e2 <= dx ) {
            if( y0 == y1 ) break;
	    error += dx;
	    y0 += sy;
        }
    }
}

