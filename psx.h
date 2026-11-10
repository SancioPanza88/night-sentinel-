/* Shim host: emula il minimo di PSn00bSDK (GPU/GTE/pad) per provare il gioco su PC. */
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdarg.h>
#define ONE (1 << 12)
typedef struct { int16_t m[3][3]; int32_t t[3]; } MATRIX;
typedef struct { int32_t vx, vy, vz; } VECTOR;
typedef struct { int16_t vx, vy, vz, pad; } SVECTOR;
typedef struct { int16_t x, y, w, h; } RECT;
typedef struct { RECT disp, screen; uint8_t isinter, isrgb24, reverse, _r; } DISPENV;
typedef struct { RECT clip; int16_t ofs[2]; RECT tw; uint16_t tpage; uint8_t dtd, dfe, isbg, r0, g0, b0; } DRAWENV;
typedef struct { uint32_t tag; uint8_t r0,g0,b0,code; int16_t x0,y0; uint8_t u0,v0; uint16_t clut; int16_t x1,y1; uint8_t u1,v1; uint16_t tpage; int16_t x2,y2; uint8_t u2,v2; uint16_t pad0; int16_t x3,y3; uint8_t u3,v3; uint16_t pad1; } POLY_FT4;
typedef struct { uint32_t tag; uint8_t r0,g0,b0,code; int16_t x0,y0,x1,y1,x2,y2,x3,y3; } POLY_F4;
typedef struct { uint32_t tag; uint8_t r0,g0,b0,code; int16_t x0,y0; int16_t w,h; } TILE;
typedef struct { uint32_t tag; uint8_t r0,g0,b0,code; int16_t x0,y0,x1,y1; } LINE_F2;
typedef struct { uint8_t stat; uint8_t len:4, type:4; uint16_t btn; uint8_t rs_x, rs_y, ls_x, ls_y; } PADTYPE;
enum { PAD_SELECT=1<<0, PAD_L3=1<<1, PAD_R3=1<<2, PAD_START=1<<3, PAD_UP=1<<4, PAD_RIGHT=1<<5, PAD_DOWN=1<<6, PAD_LEFT=1<<7,
       PAD_L2=1<<8, PAD_R2=1<<9, PAD_L1=1<<10, PAD_R1=1<<11, PAD_TRIANGLE=1<<12, PAD_CIRCLE=1<<13, PAD_CROSS=1<<14, PAD_SQUARE=1<<15 };
#define setRGB0(p,r,g,b) ((p)->r0=(r),(p)->g0=(g),(p)->b0=(b))
#define setPolyFT4(p) ((p)->code=0x2c)
#define setPolyF4(p)  ((p)->code=0x28)
#define setTile(p)    ((p)->code=0x60)
#define setLineF2(p)  ((p)->code=0x40)
#define setRECT(v,_x,_y,_w,_h) ((v)->x=(_x),(v)->y=(_y),(v)->w=(_w),(v)->h=(_h))
#define getTPage(tp,abr,x,y) ((((tp)&3)<<7)|(((abr)&3)<<5)|(((y)&0x100)>>4)|(((x)&0x3ff)>>6)|(((y)&0x200)<<2))
#define getClut(x,y) (((y)<<6)|(((x)>>4)&0x3f))
#define addPrim(ot,p) host_addprim((uint32_t*)(ot),(void*)(p))
void host_addprim(uint32_t *slot, void *p);
void ResetGraph(int m); void SetDispMask(int m);
DISPENV *SetDefDispEnv(DISPENV *e,int x,int y,int w,int h); DRAWENV *SetDefDrawEnv(DRAWENV *e,int x,int y,int w,int h);
void PutDrawEnv(DRAWENV *e); void PutDispEnv(const DISPENV *e);
void ClearOTagR(uint32_t *ot, size_t n); int DrawOTag(const uint32_t *ot); int DrawSync(int m); int VSync(int m);
int LoadImage(const RECT *r, const uint32_t *d);
void FntLoad(int x,int y); int FntOpen(int x,int y,int w,int h,int bg,int n); int FntPrint(int id,const char*f,...); void *FntFlush(int id);
void InitPAD(uint8_t*a,int la,uint8_t*b,int lb); void StartPAD(void); void ChangeClearPAD(int m);
void InitGeom(void);
/* GTE */
void h_setrot(const MATRIX*m); void h_settrans(const MATRIX*m);
void h_ldv(int i,const SVECTOR*v); void h_rtps(void); void h_rtpt(void); void h_nclip(void); void h_avsz4(void);
extern int h_mac0, h_otz, h_sxy[3][2], h_sz[4], h_ofx, h_ofy, h_H;
#define gte_SetRotMatrix(m) h_setrot(m)
#define gte_SetTransMatrix(m) h_settrans(m)
#define gte_SetGeomOffset(x,y) (h_ofx=(x),h_ofy=(y))
#define gte_SetGeomScreen(h) (h_H=(h))
#define gte_ldv0(v) h_ldv(0,v)
#define gte_ldv3(a,b,c) (h_ldv(0,a),h_ldv(1,b),h_ldv(2,c))
#define gte_rtps() h_rtps()
#define gte_rtpt() h_rtpt()
#define gte_nclip() h_nclip()
#define gte_avsz4() h_avsz4()
#define gte_stopz(p) (*(p)=h_mac0)
#define gte_stotz(p) (*(p)=h_otz)
#define gte_stsz(p) (*(p)=h_sz[3])
#define gte_stsz3(a,b,c) (*(a)=h_sz[1],*(b)=h_sz[2],*(c)=h_sz[3])
#define H_ST(p,i) do{ int16_t *q_=(int16_t*)(p); q_[0]=h_sxy[i][0]; q_[1]=h_sxy[i][1]; }while(0)
#define gte_stsxy0(p) H_ST(p,0)
#define gte_stsxy1(p) H_ST(p,1)
#define gte_stsxy2(p) H_ST(p,2)
#define gte_stsxy(p)  H_ST(p,2)
#define GTE_SET_ZSF() ((void)0)
/* host controls */
extern uint16_t vram[512][1024];
extern uint8_t  fb[240][320][3];
extern char     host_text[4][512];
