#include <alloc.h>
#include <mem.h>
#include <stdio.h>
#include <math.h>
#include "vga.h"

#define FIX_PREC 9
#define TO_FIX(x) ((long)((x)*(1<<FIX_PREC)))
#define TO_DBL(x) (((double)x)/(double)(1<<FIX_PREC))
#define TO_LONG(x) ((x)/(1<<FIX_PREC))

#define SIN_SIZE 512
#define COS_OFF 128

long SIN[ SIN_SIZE + COS_OFF ];
long *COS = SIN + COS_OFF;

void init_sin()
{
  int i;
  double v;
  for(i = 0; i < SIN_SIZE + COS_OFF; ++i) {
    v = sin(2.0 * M_PI * i / (double)SIN_SIZE);
    SIN[i] = TO_FIX( v );
  }
}

long fix_mul(long a, long b)
{
  return (a * b) >> FIX_PREC;
}

long fix_sqr(long a)
{
  return (a * a) >> FIX_PREC;
}

long fix_div(long a, long b)
{
   return (a << FIX_PREC) / b;
}

void
draw_cube(int t)
{
  static const int edges[] = {
    0, 1,  1, 3,  3, 2,  2, 0,
    1, 5,  0, 4,  2, 6,  3, 7,
    4, 5,  5, 7,  7, 6,  6, 4
  };
  static const long cubeX[] = {
    TO_FIX(-1), TO_FIX(-1), TO_FIX( 1), TO_FIX( 1),
    TO_FIX(-1), TO_FIX(-1), TO_FIX( 1), TO_FIX( 1)
  };
  static const long cubeY[] = {
    TO_FIX(-1), TO_FIX( 1), TO_FIX(-1), TO_FIX( 1),
    TO_FIX(-1), TO_FIX( 1), TO_FIX(-1), TO_FIX( 1)
  };
  static const long cubeZ[] = {
    TO_FIX(-1), TO_FIX(-1), TO_FIX(-1), TO_FIX(-1),
    TO_FIX( 1), TO_FIX( 1), TO_FIX( 1), TO_FIX( 1)
  };

  long cubeRotX[8], cubeRotY[8], cubeRotZ[8];
  long tempY, tempZ;
  long cubeProjX[8], cubeProjY[8];
  int i, e1, e2, x1, y1, x2, y2;
  long a = TO_FIX(4), scale = TO_FIX(40);

  for(i = 0; i < 8; ++i)
  {
     /* Rotation around Y */
     cubeRotX[i] =   fix_mul(cubeX[i], COS[t%SIN_SIZE])
		   + fix_mul(cubeZ[i], SIN[t%SIN_SIZE]);
     cubeRotY[i] = cubeY[i];
     cubeRotZ[i] = - fix_mul(cubeX[i], SIN[t%SIN_SIZE])
		   + fix_mul(cubeZ[i], COS[t%SIN_SIZE]);
     /* Rotation around X */
     tempY = fix_mul(cubeRotY[i], COS[t%SIN_SIZE]) - fix_mul(cubeRotZ[i], SIN[t%SIN_SIZE]);
     tempZ = fix_mul(cubeRotY[i], SIN[t%SIN_SIZE]) + fix_mul(cubeRotZ[i], COS[t%SIN_SIZE]);
     cubeRotY[i] = tempY;
     cubeRotZ[i] = tempZ;
     /* Translate further away */
     cubeRotZ[i] += TO_FIX(4);
     /* projection to screen space*/
     cubeProjX[i] = fix_div( fix_mul(a, cubeRotX[i]), cubeRotZ[i] );
     cubeProjY[i] = fix_div( fix_mul(a, cubeRotY[i]), cubeRotZ[i] );
  }
  for(i = 0; i < 24; i+=2)
  {
    e1 = edges[i]; e2 = edges[i+1];
    x1 = TO_LONG(fix_mul(cubeProjX[e1], scale)) + (vga_width>>1);
    y1 = TO_LONG(fix_mul(cubeProjY[e1], scale)) + (vga_height>>1);
    x2 = TO_LONG(fix_mul(cubeProjX[e2], scale)) + (vga_width>>1);
    y2 = TO_LONG(fix_mul(cubeProjY[e2], scale)) + (vga_height>>1);

    draw_line(x1, y1, x2, y2, 15);
  }
}

int main()
{
  int t=0;
  char kc=0;
  byte far *buf=farmalloc(64000);
  BUF=buf;

  init_sin();
  set_graphics_mode();
  while(kc!=0x1b) {
    if(kbhit()) kc=getch();
    memset(buf,0,64000);
    draw_cube(t);
    wait_for_retrace();
    memcpy(VGA,buf,64000);
    t+=2;
  }
  set_text_mode();

  return 0;
}