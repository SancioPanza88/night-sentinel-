#include "psx.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
uint16_t vram[512][1024];
uint8_t fb[240][320][3];
char host_text[4][512];
static char textbuf[4][512];
static int vsync_count, bg[3] = {0,0,0};
/* ---- GTE */
static MATRIX rot; static SVECTOR V[3];
int h_mac0, h_otz, h_sxy[3][2], h_sz[4], h_ofx, h_ofy, h_H;
void h_setrot(const MATRIX*m){ rot=*m; } void h_settrans(const MATRIX*m){ memcpy(rot.t,m->t,sizeof rot.t); }
void h_ldv(int i,const SVECTOR*v){ V[i]=*v; }
static void push(int sx,int sy,int sz){ h_sxy[0][0]=h_sxy[1][0]; h_sxy[0][1]=h_sxy[1][1]; h_sxy[1][0]=h_sxy[2][0]; h_sxy[1][1]=h_sxy[2][1]; h_sxy[2][0]=sx; h_sxy[2][1]=sy;
  h_sz[0]=h_sz[1]; h_sz[1]=h_sz[2]; h_sz[2]=h_sz[3]; h_sz[3]=sz; }
static void rtp(const SVECTOR*v){ long long m[3]; int i;
  for(i=0;i<3;i++) m[i]=rot.t[i]+(((long long)rot.m[i][0]*v->vx+(long long)rot.m[i][1]*v->vy+(long long)rot.m[i][2]*v->vz)>>12);
  int sz=(int)(m[2]<0?0:(m[2]>65535?65535:m[2]));
  long long n= sz==0?0x1ffff:(((long long)h_H<<16)/sz); if(n>0x1ffff)n=0x1ffff;
  long long sx=h_ofx+((n*m[0])>>16), sy=h_ofy+((n*m[1])>>16);
  if(sx<-1024)sx=-1024; if(sx>1023)sx=1023; if(sy<-1024)sy=-1024; if(sy>1023)sy=1023;
  push((int)sx,(int)sy,sz); }
void h_rtps(void){ rtp(&V[0]); } void h_rtpt(void){ rtp(&V[0]); rtp(&V[1]); rtp(&V[2]); }
void h_nclip(void){ long long a=h_sxy[0][0],b=h_sxy[0][1],c=h_sxy[1][0],d=h_sxy[1][1],e=h_sxy[2][0],f=h_sxy[2][1];
  h_mac0=(int)(a*(d-f)+c*(f-b)+e*(b-d)); }
void h_avsz4(void){ h_otz=((1024*(h_sz[0]+h_sz[1]+h_sz[2]+h_sz[3]))>>12); if(h_otz>65535)h_otz=65535; }
void InitGeom(void){} 
/* ---- OT */
typedef struct { int idx, seq; void *p; } ENT;
static ENT *ents; static int nents, capents, seqc; int host_maxprims;
static uint32_t *otbase[4]; static size_t otlen[4]; static int notb;
void host_addprim(uint32_t*slot,void*p){ int k; for(k=0;k<notb;k++) if(slot>=otbase[k]&&slot<otbase[k]+otlen[k]) break;
  if(k==notb){ fprintf(stderr,"addPrim: slot fuori da ogni OT!\n"); abort(); }
  if(nents==capents){ capents=capents?capents*2:4096; ents=realloc(ents,capents*sizeof(ENT)); }
  ents[nents].idx=(int)(slot-otbase[k])+k*100000; ents[nents].seq=seqc++; ents[nents].p=p; nents++; }
void ClearOTagR(uint32_t*ot,size_t n){ int k; for(k=0;k<notb;k++) if(otbase[k]==ot) return; otbase[notb]=ot; otlen[notb]=n; notb++; }
static int cmp(const void*a,const void*b){ const ENT*x=a,*y=b; if(x->idx!=y->idx) return y->idx-x->idx; return y->seq-x->seq; }
/* ---- raster */
static int tex(uint16_t tpage,uint16_t clut,int u,int v,int*alpha){ int mode=(tpage>>7)&3, bx=(tpage&15)*64, by=((tpage>>4)&1)*256; int idx;
  if(mode==0){ uint16_t w=vram[by+v][bx+(u>>2)]; idx=(w>>((u&3)*4))&15; }
  else { uint16_t w=vram[by+v][bx+(u>>1)]; idx=(w>>((u&1)*8))&255; }
  uint16_t c=vram[(clut>>6)][((clut&0x3f)*16)+idx]; *alpha=(c!=0); return c; }
static void px(int x,int y,int r,int g,int b){ if(x<0||y<0||x>=320||y>=240) return; fb[y][x][0]=r; fb[y][x][1]=g; fb[y][x][2]=b; }
static void tri(const int X[3],const int Y[3],const int U[3],const int Vv[3],int ft,int r,int g,int b,uint16_t tp,uint16_t cl){
  int minx=X[0],maxx=X[0],miny=Y[0],maxy=Y[0],i; for(i=1;i<3;i++){ if(X[i]<minx)minx=X[i]; if(X[i]>maxx)maxx=X[i]; if(Y[i]<miny)miny=Y[i]; if(Y[i]>maxy)maxy=Y[i]; }
  if(maxx-minx>1023||maxy-miny>511) return;
  if(minx<0)minx=0; if(miny<0)miny=0; if(maxx>319)maxx=319; if(maxy>239)maxy=239;
  double den=(double)(Y[1]-Y[2])*(X[0]-X[2])+(double)(X[2]-X[1])*(Y[0]-Y[2]); if(den==0) return;
  for(int y=miny;y<=maxy;y++) for(int x=minx;x<=maxx;x++){ double px_=x+0.5,py_=y+0.5;
    double l0=((Y[1]-Y[2])*(px_-X[2])+(X[2]-X[1])*(py_-Y[2]))/den, l1=((Y[2]-Y[0])*(px_-X[2])+(X[0]-X[2])*(py_-Y[2]))/den, l2=1-l0-l1;
    if(l0<-1e-9||l1<-1e-9||l2<-1e-9) continue;
    if(!ft){ px(x,y,r,g,b); continue; }
    int u=(int)floor(l0*U[0]+l1*U[1]+l2*U[2]+0.0), v=(int)floor(l0*Vv[0]+l1*Vv[1]+l2*Vv[2]+0.0); if(u<0)u=0; if(u>255)u=255; if(v<0)v=0; if(v>255)v=255;
    int a; uint16_t c=tex(tp,cl,u,v,&a); if(!a) continue;
    int cr=(c&31)<<3,cg=((c>>5)&31)<<3,cb=((c>>10)&31)<<3; cr|=cr>>5; cg|=cg>>5; cb|=cb>>5;
    cr=cr*r/128; cg=cg*g/128; cb=cb*b/128; if(cr>255)cr=255; if(cg>255)cg=255; if(cb>255)cb=255; px(x,y,cr,cg,cb); } }
int DrawOTag(const uint32_t*ot){ if(nents>host_maxprims) host_maxprims=nents; qsort(ents,nents,sizeof(ENT),cmp);
  for(int i=0;i<nents;i++){ uint8_t *p=ents[i].p; uint8_t code=p[7];
    if(code==0x2c){ POLY_FT4*q=ents[i].p; int X[4]={q->x0,q->x1,q->x2,q->x3},Y[4]={q->y0,q->y1,q->y2,q->y3},U[4]={q->u0,q->u1,q->u2,q->u3},V_[4]={q->v0,q->v1,q->v2,q->v3};
      int a[3]={0,1,2},b[3]={1,2,3}; int Xa[3],Ya[3],Ua[3],Va[3]; for(int k=0;k<3;k++){Xa[k]=X[a[k]];Ya[k]=Y[a[k]];Ua[k]=U[a[k]];Va[k]=V_[a[k]];}
      tri(Xa,Ya,Ua,Va,1,q->r0,q->g0,q->b0,q->tpage,q->clut);
      for(int k=0;k<3;k++){Xa[k]=X[b[k]];Ya[k]=Y[b[k]];Ua[k]=U[b[k]];Va[k]=V_[b[k]];}
      tri(Xa,Ya,Ua,Va,1,q->r0,q->g0,q->b0,q->tpage,q->clut);
    } else if(code==0x28){ POLY_F4*q=ents[i].p; int X[4]={q->x0,q->x1,q->x2,q->x3},Y[4]={q->y0,q->y1,q->y2,q->y3}; int z[3]={0};
      int a[3]={0,1,2},b[3]={1,2,3}; int Xa[3],Ya[3];
      for(int k=0;k<3;k++){Xa[k]=X[a[k]];Ya[k]=Y[a[k]];} tri(Xa,Ya,z,z,0,q->r0,q->g0,q->b0,0,0);
      for(int k=0;k<3;k++){Xa[k]=X[b[k]];Ya[k]=Y[b[k]];} tri(Xa,Ya,z,z,0,q->r0,q->g0,q->b0,0,0);
    } else if(code==0x60){ TILE*q=ents[i].p; for(int y=0;y<q->h;y++) for(int x=0;x<q->w;x++) px(q->x0+x,q->y0+y,q->r0,q->g0,q->b0);
    } else if(code==0x40){ LINE_F2*q=ents[i].p; int x0=q->x0,y0=q->y0,x1=q->x1,y1=q->y1,dx=abs(x1-x0),dy=-abs(y1-y0),sx=x0<x1?1:-1,sy=y0<y1?1:-1,e=dx+dy;
      for(int n=0;n<2000;n++){ px(x0,y0,q->r0,q->g0,q->b0); if(x0==x1&&y0==y1)break; int e2=2*e; if(e2>=dy){e+=dy;x0+=sx;} if(e2<=dx){e+=dx;y0+=sy;} }
    } else { fprintf(stderr,"codice prim sconosciuto %x\n",code); abort(); } }
  nents=0; seqc=0; return 0; }
void PutDrawEnv(DRAWENV*e){ if(e->isbg){ bg[0]=e->r0;bg[1]=e->g0;bg[2]=e->b0; for(int y=0;y<240;y++)for(int x=0;x<320;x++){fb[y][x][0]=bg[0];fb[y][x][1]=bg[1];fb[y][x][2]=bg[2];} } }
void PutDispEnv(const DISPENV*e){(void)e;} void ResetGraph(int m){(void)m;} void SetDispMask(int m){(void)m;}
DISPENV*SetDefDispEnv(DISPENV*e,int x,int y,int w,int h){memset(e,0,sizeof*e);e->disp.x=x;e->disp.y=y;e->disp.w=w;e->disp.h=h;return e;}
DRAWENV*SetDefDrawEnv(DRAWENV*e,int x,int y,int w,int h){memset(e,0,sizeof*e);e->clip.x=x;e->clip.y=y;e->clip.w=w;e->clip.h=h;return e;}
int DrawSync(int m){(void)m;return 0;} int VSync(int m){ if(m<0) return vsync_count; vsync_count+= (m?m:1); return vsync_count; }
int LoadImage(const RECT*r,const uint32_t*d){ const uint16_t*s=(const uint16_t*)d; for(int y=0;y<r->h;y++) for(int x=0;x<r->w;x++) vram[r->y+y][r->x+x]=s[y*r->w+x]; return 0; }
void FntLoad(int x,int y){(void)x;(void)y;} int FntOpen(int x,int y,int w,int h,int bg_,int n){(void)x;(void)y;(void)w;(void)h;(void)bg_;(void)n; static int id=0; return id++; }
int FntPrint(int id,const char*f,...){ va_list ap; va_start(ap,f); size_t l=strlen(textbuf[id]); vsnprintf(textbuf[id]+l,512-l,f,ap); va_end(ap); return 0; }
void *FntFlush(int id){ memcpy(host_text[id],textbuf[id],512); textbuf[id][0]=0; return 0; }
void InitPAD(uint8_t*a,int la,uint8_t*b,int lb){(void)a;(void)la;(void)b;(void)lb;} void StartPAD(void){} void ChangeClearPAD(int m){(void)m;}
