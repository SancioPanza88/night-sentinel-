/*
 * NIGHT SENTINEL - demo tecnica (PS1, PSn00bSDK)
 * Strada 3D, 3 edifici esplorabili, nemico con IA, combattimento,
 * impulso energetico, rampino, planata e una piccola missione.
 * Tutto a virgola fissa (nessun float), rendering GTE + ordering table.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <psxgpu.h>
#include <psxgte.h>
#include <psxpad.h>
#include <psxapi.h>
#include <psxetc.h>
#include <inline_c.h>
#include "model_data_v3.h"
#include "city_data.h"

#ifndef HOST_TEST
#define host_freeze 0
#define host_noplayer 0
#endif

#ifndef GTE_SET_ZSF
#define GTE_SET_ZSF() __asm__ volatile ("ctc2 %0, $29;" "ctc2 %1, $30;" : : "r"(ONE / 3), "r"(ONE / 4))
#endif

#define OT_LEN    1024
#define PRI_LEN   110000
#define SCR_W     320
#define SCR_H     240
#define CX        160
#define CY        120
#define PROJ_H    240

typedef struct {
	DISPENV  disp;
	DRAWENV  draw;
	uint32_t ot[OT_LEN];
	char     pri[PRI_LEN];
} DB;

static DB   db[2];
static int  dba;
static char *nextpri, *pri_end;
uint8_t     pad_buff[2][34];
static int  font0, font1;
static int  last_v;

/* VRAM layout (halfword coordinates) */
#define CHR_X 640   /* personaggio 8bpp 256x256  -> 128x256 hw */
#define CTY_X 768   /* citta' 4bpp 256x128       -> 64x128 hw  */
#define CLUT_Y 256
static uint16_t tp_chr, tp_city;
static uint16_t clut_pl, clut_en, clut_en2, clut_city[4];
static uint16_t en_clut_data[256] __attribute__((aligned(4)));
static uint16_t en2_clut_data[256] __attribute__((aligned(4)));

/* ------------------------------------------------------------------ */
/* matematica a virgola fissa                                          */
/* ------------------------------------------------------------------ */
static int sn(int a) { return sintab[(a >> 2) & 1023]; }
static int cs(int a) { return sintab[((a >> 2) + 256) & 1023]; }
static int iabs(int a) { return a < 0 ? -a : a; }
static int imin(int a, int b) { return a < b ? a : b; }
static int imax(int a, int b) { return a > b ? a : b; }
static int iclamp(int a, int lo, int hi) { return a < lo ? lo : (a > hi ? hi : a); }
static int angdiff(int a, int b) { int d = (a - b) & 4095; return d > 2048 ? d - 4096 : d; }
static int D2U(int d) { return (d * 1456) >> 7; }   /* gradi -> 4096/giro */

static int isqrt(int v) {
	unsigned n = (unsigned)v, r = 0, b = 1u << 30;
	if (v <= 0) return 0;
	while (b > n) b >>= 2;
	while (b) {
		if (n >= r + b) { n -= r + b; r = (r >> 1) + b; } else r >>= 1;
		b >>= 2;
	}
	return (int)r;
}

/* atan2 in unita' 4096/giro: restituisce a tale che (sin a, cos a) ~ (y, x)/|.| */
static int iatan2(int y, int x) {
	int ax = iabs(x), ay = iabs(y), a, t;
	if (!ax && !ay) return 0;
	if (ax >= ay) t = (ay << 12) / ax; else t = (ax << 12) / ay;
	a = ((512 * t) >> 12) + ((178 * t * (4096 - t)) >> 24);
	if (ax < ay) a = 1024 - a;
	if (x < 0) a = 2048 - a;
	if (y < 0) a = -a;
	return a & 4095;
}

static void m_rotx(MATRIX *r, int a) {
	int c = cs(a), s = sn(a);
	r->m[0][0] = ONE; r->m[0][1] = 0; r->m[0][2] = 0;
	r->m[1][0] = 0;   r->m[1][1] = c; r->m[1][2] = -s;
	r->m[2][0] = 0;   r->m[2][1] = s; r->m[2][2] = c;
}
static void m_roty(MATRIX *r, int a) {
	int c = cs(a), s = sn(a);
	r->m[0][0] = c;  r->m[0][1] = 0;   r->m[0][2] = s;
	r->m[1][0] = 0;  r->m[1][1] = ONE; r->m[1][2] = 0;
	r->m[2][0] = -s; r->m[2][1] = 0;   r->m[2][2] = c;
}
static void m_rotz(MATRIX *r, int a) {
	int c = cs(a), s = sn(a);
	r->m[0][0] = c; r->m[0][1] = -s; r->m[0][2] = 0;
	r->m[1][0] = s; r->m[1][1] = c;  r->m[1][2] = 0;
	r->m[2][0] = 0; r->m[2][1] = 0;  r->m[2][2] = ONE;
}
static void m_mul(const MATRIX *a, const MATRIX *b, MATRIX *o) {
	int i, j;
	int16_t t[3][3];
	for (i = 0; i < 3; i++)
		for (j = 0; j < 3; j++)
			t[i][j] = (int16_t)((a->m[i][0] * b->m[0][j] + a->m[i][1] * b->m[1][j] + a->m[i][2] * b->m[2][j]) >> 12);
	for (i = 0; i < 3; i++)
		for (j = 0; j < 3; j++) o->m[i][j] = t[i][j];
}
static void m_vec(const MATRIX *a, const int *v, int *o) {
	int i, t[3];
	for (i = 0; i < 3; i++) t[i] = (a->m[i][0] * v[0] + a->m[i][1] * v[1] + a->m[i][2] * v[2]) >> 12;
	o[0] = t[0]; o[1] = t[1]; o[2] = t[2];
}

/* ------------------------------------------------------------------ */
/* scheletro, pose e animazione                                        */
/* ------------------------------------------------------------------ */
#define NB 16
enum { B_PELVIS, B_TORSO, B_HEAD, B_VISOR, B_CREST, B_ARM_L, B_FORE_L, B_ARM_R, B_FORE_R,
       B_THIGH_L, B_SHIN_L, B_THIGH_R, B_SHIN_R, B_CAPE1, B_CAPE2, B_PLATE };

/*
 * Convenzioni di segno (personaggio rivolto a +z, +x = sua destra, y verso il basso):
 *   rx  arti/mantello (puntano in basso):  + avanti, - indietro
 *   rx  busto/testa/bacino (puntano in alto): + indietro, - AVANTI
 *   ry  + gira a destra (la spalla destra va indietro)
 *   rz  arti: + verso sinistra(-x); busto: + inclina a destra(+x)
 *   braccio sinistro: all'esterno = rz+, braccio destro: all'esterno = rz-
 * Gli angoli sono in 1/8 di grado; root = spostamento del bacino (x lato, y giu', z avanti).
 */
typedef struct { int16_t a[NB][3]; int16_t root[3]; } POSE;

static int E2U(int v8) { return (v8 * 1456) >> 10; }   /* 1/8 grado -> 4096/giro */

static void ps(POSE *p, int b, int rx, int ry, int rz) {
	p->a[b][0] = (int16_t)(rx * 8); p->a[b][1] = (int16_t)(ry * 8); p->a[b][2] = (int16_t)(rz * 8);
}
static void pr(POSE *p, int x, int y, int z) { p->root[0] = (int16_t)x; p->root[1] = (int16_t)y; p->root[2] = (int16_t)z; }

static void pose_ease(POSE *c, const POSE *t, int rate) {
	int i, j;
	for (i = 0; i < NB; i++)
		for (j = 0; j < 3; j++) c->a[i][j] += (int16_t)(((t->a[i][j] - c->a[i][j]) * rate) >> 8);
	for (j = 0; j < 3; j++) c->root[j] += (int16_t)(((t->root[j] - c->root[j]) * rate) >> 8);
}
static void pose_mix(POSE *o, const POSE *a, const POSE *b, int t) {
	int i, j;
	for (i = 0; i < NB; i++)
		for (j = 0; j < 3; j++) o->a[i][j] = (int16_t)(a->a[i][j] + (((b->a[i][j] - a->a[i][j]) * t) >> 8));
	for (j = 0; j < 3; j++) o->root[j] = (int16_t)(a->root[j] + (((b->root[j] - a->root[j]) * t) >> 8));
}

enum { L_IDLE, L_GUARD, L_JAB_W, L_JAB_H, L_HOOK_W, L_HOOK_H, L_SPIN_W, L_SPIN_H, L_KICK_W, L_KICK_H,
       L_PUL_W, L_PUL_H, L_HURT, L_JSQ, L_UP, L_FALL, L_GLIDE, L_LAND, L_GRAP, L_MANT, L_ROLL, L_ROLL_IN, L_SPIN_C, L_KNEEL, L_DEAD,
       L_BR_IDLE, L_BR_SLAM_W, L_BR_SLAM_H, L_BR_HOOK_W, L_BR_HOOK_H, L_BR_HURT, L_BR_KNEEL, L_BR_FALL,
       L_ST_IDLE, L_ST_COIL, L_ST_KICK, L_ST_SLIDE, L_ST_HURT, L_ST_DEAD, NL };
static POSE lib[NL];

static void base_pose(POSE *p) {
	memset(p, 0, sizeof(POSE));
	ps(p, B_ARM_L, 0, 0, 7);  ps(p, B_ARM_R, 0, 0, -7);
	ps(p, B_FORE_L, 12, 0, 0); ps(p, B_FORE_R, 12, 0, 0);
	ps(p, B_THIGH_L, 0, 0, 2); ps(p, B_THIGH_R, 0, 0, -2);
}
static void guard_pose(POSE *p) {
	base_pose(p);
	ps(p, B_TORSO, -9, 0, 0);  ps(p, B_HEAD, 6, 0, 0);
	ps(p, B_ARM_L, 52, 0, -10); ps(p, B_FORE_L, 108, 0, 0);
	ps(p, B_ARM_R, 60, 0, 10);  ps(p, B_FORE_R, 112, 0, 0);
	ps(p, B_THIGH_L, 20, 0, 8); ps(p, B_SHIN_L, -24, 0, 0);
	ps(p, B_THIGH_R, -14, 0, -8); ps(p, B_SHIN_R, -16, 0, 0);
	pr(p, 0, 16, 0);
}

static void init_poses(void) {
	POSE *p;
	base_pose(&lib[L_IDLE]);

	guard_pose(&lib[L_GUARD]);

	/* jab destro: carica -> colpo */
	p = &lib[L_JAB_W]; guard_pose(p);
	ps(p, B_PELVIS, 0, 10, 0); ps(p, B_TORSO, -8, 24, 0); ps(p, B_HEAD, 4, -14, 0);
	ps(p, B_ARM_R, 22, 0, 4);  ps(p, B_FORE_R, 125, 0, 0);
	ps(p, B_THIGH_R, -26, 0, -8); ps(p, B_SHIN_R, -30, 0, 0);
	pr(p, 0, 20, -10);
	p = &lib[L_JAB_H]; guard_pose(p);
	ps(p, B_PELVIS, 0, -14, 0); ps(p, B_TORSO, -12, -30, 0); ps(p, B_HEAD, 6, 22, 0);
	ps(p, B_ARM_R, 92, 0, 2);  ps(p, B_FORE_R, 6, 0, 0);
	ps(p, B_THIGH_L, 32, 0, 8); ps(p, B_SHIN_L, -34, 0, 0);
	ps(p, B_THIGH_R, -30, 0, -8); ps(p, B_SHIN_R, -6, 0, 0);
	pr(p, 0, 14, 26);

	/* gancio sinistro */
	p = &lib[L_HOOK_W]; guard_pose(p);
	ps(p, B_PELVIS, 0, -10, 0); ps(p, B_TORSO, -10, -24, 0);
	ps(p, B_ARM_L, 30, 0, 60);  ps(p, B_FORE_L, 70, 0, 0);
	ps(p, B_THIGH_L, 10, 0, 8); ps(p, B_THIGH_R, -22, 0, -8);
	pr(p, 4, 20, -8);
	p = &lib[L_HOOK_H]; guard_pose(p);
	ps(p, B_PELVIS, 0, 18, 0); ps(p, B_TORSO, -12, 34, 0); ps(p, B_HEAD, 6, -26, 0);
	ps(p, B_ARM_L, 84, 0, -34); ps(p, B_FORE_L, 92, 0, 0);
	ps(p, B_THIGH_L, -8, 0, 8); ps(p, B_THIGH_R, 26, 0, -8); ps(p, B_SHIN_R, -26, 0, 0);
	pr(p, -4, 16, 18);

	/* calcio rotante (finale combo): carica -> giro con gamba raccolta -> gamba tesa */
	p = &lib[L_SPIN_W]; base_pose(p);
	ps(p, B_PELVIS, -4, 50, 0); ps(p, B_TORSO, -14, 30, 0); ps(p, B_HEAD, 8, -34, 0);
	ps(p, B_ARM_L, 10, 0, 64); ps(p, B_ARM_R, 10, 0, -64);
	ps(p, B_THIGH_L, 40, 0, 10); ps(p, B_SHIN_L, -58, 0, 0);
	ps(p, B_THIGH_R, 24, 0, -10); ps(p, B_SHIN_R, -46, 0, 0);
	pr(p, 0, 38, -6);
	p = &lib[L_SPIN_C]; base_pose(p);
	ps(p, B_TORSO, 8, 0, 0); ps(p, B_HEAD, -6, 0, 0);
	ps(p, B_ARM_L, 6, 0, 80); ps(p, B_ARM_R, 6, 0, -80); ps(p, B_FORE_L, 4, 0, 0); ps(p, B_FORE_R, 4, 0, 0);
	ps(p, B_THIGH_R, 78, 0, -4); ps(p, B_SHIN_R, -110, 0, 0);
	ps(p, B_THIGH_L, 12, 0, 4); ps(p, B_SHIN_L, -22, 0, 0);
	pr(p, 0, 8, 6);
	p = &lib[L_SPIN_H]; base_pose(p);
	ps(p, B_TORSO, 24, 0, 0); ps(p, B_HEAD, -16, 0, 0);
	ps(p, B_ARM_L, 8, 0, 82); ps(p, B_ARM_R, 8, 0, -82); ps(p, B_FORE_L, 4, 0, 0); ps(p, B_FORE_R, 4, 0, 0);
	ps(p, B_THIGH_R, 96, 0, -3); ps(p, B_SHIN_R, 0, 0, 0);
	ps(p, B_THIGH_L, -8, 0, 4); ps(p, B_SHIN_L, -12, 0, 0);
	pr(p, 0, -2, 14);

	/* calcio frontale potente (triangolo) */
	p = &lib[L_KICK_W]; base_pose(p);
	ps(p, B_TORSO, 10, 0, 0);  ps(p, B_HEAD, -4, 0, 0);
	ps(p, B_ARM_L, 16, 0, 42); ps(p, B_ARM_R, 16, 0, -42);
	ps(p, B_THIGH_R, 76, 0, -4); ps(p, B_SHIN_R, -112, 0, 0);
	ps(p, B_THIGH_L, -4, 0, 4); ps(p, B_SHIN_L, -14, 0, 0);
	pr(p, 0, 8, -14);
	p = &lib[L_KICK_H]; base_pose(p);
	ps(p, B_PELVIS, 6, 0, 0);  ps(p, B_TORSO, 24, 0, 0); ps(p, B_HEAD, -14, 0, 0);
	ps(p, B_ARM_L, -22, 0, 48); ps(p, B_ARM_R, -16, 0, -54);
	ps(p, B_THIGH_R, 98, 0, -3); ps(p, B_SHIN_R, -2, 0, 0);
	ps(p, B_THIGH_L, -8, 0, 4); ps(p, B_SHIN_L, -10, 0, 0);
	pr(p, 0, 4, 28);

	/* impulso energetico */
	p = &lib[L_PUL_W]; base_pose(p);
	ps(p, B_TORSO, 14, 0, 0);  ps(p, B_HEAD, -6, 0, 0);
	ps(p, B_ARM_L, 38, 0, -14); ps(p, B_FORE_L, 125, 0, 0);
	ps(p, B_ARM_R, 38, 0, 14);  ps(p, B_FORE_R, 125, 0, 0);
	ps(p, B_THIGH_L, 22, 0, 10); ps(p, B_SHIN_L, -30, 0, 0);
	ps(p, B_THIGH_R, -14, 0, -10); ps(p, B_SHIN_R, -20, 0, 0);
	pr(p, 0, 24, -12);
	p = &lib[L_PUL_H]; base_pose(p);
	ps(p, B_TORSO, -18, 0, 0); ps(p, B_HEAD, 14, 0, 0);
	ps(p, B_ARM_L, 90, 0, -4); ps(p, B_FORE_L, 0, 0, 0);
	ps(p, B_ARM_R, 90, 0, 4);  ps(p, B_FORE_R, 0, 0, 0);
	ps(p, B_THIGH_L, 30, 0, 10); ps(p, B_SHIN_L, -24, 0, 0);
	ps(p, B_THIGH_R, -26, 0, -10); ps(p, B_SHIN_R, -4, 0, 0);
	pr(p, 0, 14, 18);

	/* colpo subito */
	p = &lib[L_HURT]; base_pose(p);
	ps(p, B_TORSO, 30, 0, 0);  ps(p, B_HEAD, 22, 0, 0);
	ps(p, B_ARM_L, -34, 0, 44); ps(p, B_ARM_R, -34, 0, -44);
	ps(p, B_FORE_L, 20, 0, 0); ps(p, B_FORE_R, 20, 0, 0);
	ps(p, B_THIGH_L, -10, 0, 4); ps(p, B_THIGH_R, 22, 0, -4); ps(p, B_SHIN_R, -32, 0, 0);
	pr(p, 0, 8, -26);

	/* salto: accovacciata, salita, caduta, atterraggio */
	p = &lib[L_JSQ]; base_pose(p);
	ps(p, B_TORSO, -24, 0, 0); ps(p, B_HEAD, 14, 0, 0);
	ps(p, B_ARM_L, -52, 0, 8); ps(p, B_ARM_R, -52, 0, -8);
	ps(p, B_THIGH_L, 58, 0, 6); ps(p, B_SHIN_L, -88, 0, 0);
	ps(p, B_THIGH_R, 54, 0, -6); ps(p, B_SHIN_R, -84, 0, 0);
	pr(p, 0, 48, 0);
	p = &lib[L_UP]; base_pose(p);
	ps(p, B_TORSO, -6, 0, 0);
	ps(p, B_ARM_L, 150, 0, 14); ps(p, B_ARM_R, 146, 0, -14); ps(p, B_FORE_L, 14, 0, 0); ps(p, B_FORE_R, 14, 0, 0);
	ps(p, B_THIGH_R, 72, 0, -4); ps(p, B_SHIN_R, -104, 0, 0);
	ps(p, B_THIGH_L, -20, 0, 3); ps(p, B_SHIN_L, -18, 0, 0);
	p = &lib[L_FALL]; base_pose(p);
	ps(p, B_TORSO, -4, 0, 0);  ps(p, B_HEAD, 8, 0, 0);
	ps(p, B_ARM_L, 34, 0, 62); ps(p, B_ARM_R, 34, 0, -62);
	ps(p, B_THIGH_L, 16, 0, 5); ps(p, B_SHIN_L, -24, 0, 0);
	ps(p, B_THIGH_R, -12, 0, -5); ps(p, B_SHIN_R, -18, 0, 0);
	p = &lib[L_GLIDE]; base_pose(p);
	ps(p, B_PELVIS, -54, 0, 0); ps(p, B_TORSO, 14, 0, 0); ps(p, B_HEAD, 38, 0, 0);
	ps(p, B_ARM_L, -14, 0, 86); ps(p, B_ARM_R, -14, 0, -86); ps(p, B_FORE_L, 6, 0, 0); ps(p, B_FORE_R, 6, 0, 0);
	ps(p, B_THIGH_L, 4, 0, 1); ps(p, B_THIGH_R, 6, 0, -1); ps(p, B_SHIN_L, -16, 0, 0); ps(p, B_SHIN_R, -12, 0, 0);
	pr(p, 0, -26, 30);
	p = &lib[L_LAND]; base_pose(p);
	ps(p, B_TORSO, -26, 0, 0); ps(p, B_HEAD, 16, 0, 0);
	ps(p, B_ARM_L, 42, 0, 32); ps(p, B_ARM_R, 42, 0, -32);
	ps(p, B_THIGH_L, 64, 0, 8); ps(p, B_SHIN_L, -94, 0, 0);
	ps(p, B_THIGH_R, 58, 0, -8); ps(p, B_SHIN_R, -90, 0, 0);
	pr(p, 0, 52, 0);

	/* rampino: braccio destro verso l'ancora (inclinazione aggiunta a runtime) */
	p = &lib[L_GRAP]; base_pose(p);
	ps(p, B_TORSO, -8, 0, 0);  ps(p, B_HEAD, 16, 0, 0);
	ps(p, B_ARM_R, 150, 0, -6); ps(p, B_FORE_R, 0, 0, 0);
	ps(p, B_ARM_L, -28, 0, 28); ps(p, B_FORE_L, 20, 0, 0);
	ps(p, B_THIGH_L, 24, 0, 4); ps(p, B_SHIN_L, -40, 0, 0);
	ps(p, B_THIGH_R, -12, 0, -4); ps(p, B_SHIN_R, -20, 0, 0);
	p = &lib[L_MANT]; base_pose(p);
	ps(p, B_TORSO, -40, 0, 0); ps(p, B_HEAD, 24, 0, 0);
	ps(p, B_ARM_L, -34, 0, 10); ps(p, B_ARM_R, -34, 0, -10); ps(p, B_FORE_L, 24, 0, 0); ps(p, B_FORE_R, 24, 0, 0);
	ps(p, B_THIGH_R, 96, 0, -6); ps(p, B_SHIN_R, -102, 0, 0);
	ps(p, B_THIGH_L, 36, 0, 6); ps(p, B_SHIN_L, -62, 0, 0);
	pr(p, 0, 20, 14);

	/* rotolata a terra: la rotazione del bacino e' aggiunta a runtime */
	p = &lib[L_ROLL_IN]; base_pose(p);
	ps(p, B_TORSO, -52, 0, 0); ps(p, B_HEAD, 20, 0, 0);
	ps(p, B_ARM_L, 128, 0, -8); ps(p, B_ARM_R, 128, 0, 8); ps(p, B_FORE_L, 10, 0, 0); ps(p, B_FORE_R, 10, 0, 0);
	ps(p, B_THIGH_L, 54, 0, 6); ps(p, B_SHIN_L, -76, 0, 0);
	ps(p, B_THIGH_R, -22, 0, -6); ps(p, B_SHIN_R, -30, 0, 0);
	pr(p, 0, 30, 20);
	p = &lib[L_ROLL]; base_pose(p);
	ps(p, B_TORSO, -74, 0, 0); ps(p, B_HEAD, -36, 0, 0);
	ps(p, B_ARM_L, 66, 0, -14); ps(p, B_FORE_L, 128, 0, 0);
	ps(p, B_ARM_R, 66, 0, 14);  ps(p, B_FORE_R, 128, 0, 0);
	ps(p, B_THIGH_L, 124, 0, 4); ps(p, B_SHIN_L, -142, 0, 0);
	ps(p, B_THIGH_R, 124, 0, -4); ps(p, B_SHIN_R, -142, 0, 0);
	pr(p, 0, 50, 0);

	/* morte del protagonista: in ginocchio, poi a terra */
	p = &lib[L_KNEEL]; base_pose(p);
	ps(p, B_TORSO, -12, 0, 0); ps(p, B_HEAD, -32, 0, 0);
	ps(p, B_ARM_L, 8, 0, 14); ps(p, B_ARM_R, 8, 0, -14);
	ps(p, B_THIGH_L, 88, 0, 6); ps(p, B_SHIN_L, -108, 0, 0);
	ps(p, B_THIGH_R, 84, 0, -6); ps(p, B_SHIN_R, -106, 0, 0);
	pr(p, 0, 64, 6);
	p = &lib[L_DEAD]; base_pose(p);
	ps(p, B_PELVIS, 88, 0, 0);
	ps(p, B_ARM_L, 10, 0, 46); ps(p, B_ARM_R, 10, 0, -46);
	ps(p, B_THIGH_L, 8, 0, 6); ps(p, B_SHIN_L, -12, 0, 0);
	ps(p, B_THIGH_R, -6, 0, -8);
	pr(p, 0, 100, 0);

	/* ---------------- BRUTO: massiccio, curvo, pesante ---------------- */
	p = &lib[L_BR_IDLE]; base_pose(p);
	ps(p, B_TORSO, -16, 0, 0); ps(p, B_HEAD, 8, 0, 0);
	ps(p, B_ARM_L, 10, 0, 16); ps(p, B_FORE_L, 26, 0, 0);
	ps(p, B_ARM_R, 10, 0, -16); ps(p, B_FORE_R, 26, 0, 0);
	ps(p, B_THIGH_L, 6, 0, 10); ps(p, B_SHIN_L, -14, 0, 0);
	ps(p, B_THIGH_R, 4, 0, -10); ps(p, B_SHIN_R, -12, 0, 0);
	pr(p, 0, 12, 0);
	p = &lib[L_BR_SLAM_W]; base_pose(p);
	ps(p, B_TORSO, 28, 0, 0);  ps(p, B_HEAD, 10, 0, 0);
	ps(p, B_ARM_L, 172, 0, 8); ps(p, B_FORE_L, -28, 0, 0);
	ps(p, B_ARM_R, 172, 0, -8); ps(p, B_FORE_R, -28, 0, 0);
	ps(p, B_THIGH_L, 14, 0, 12); ps(p, B_SHIN_L, -26, 0, 0);
	ps(p, B_THIGH_R, 10, 0, -12); ps(p, B_SHIN_R, -22, 0, 0);
	pr(p, 0, 14, -14);
	p = &lib[L_BR_SLAM_H]; base_pose(p);
	ps(p, B_PELVIS, -10, 0, 0); ps(p, B_TORSO, -54, 0, 0); ps(p, B_HEAD, 30, 0, 0);
	ps(p, B_ARM_L, 36, 0, 4);  ps(p, B_FORE_L, 12, 0, 0);
	ps(p, B_ARM_R, 36, 0, -4); ps(p, B_FORE_R, 12, 0, 0);
	ps(p, B_THIGH_L, 42, 0, 12); ps(p, B_SHIN_L, -46, 0, 0);
	ps(p, B_THIGH_R, -28, 0, -12); ps(p, B_SHIN_R, -10, 0, 0);
	pr(p, 0, 24, 36);
	p = &lib[L_BR_HOOK_W]; base_pose(p);
	ps(p, B_PELVIS, 0, 20, 0); ps(p, B_TORSO, -14, 56, 0); ps(p, B_HEAD, 6, -36, 0);
	ps(p, B_ARM_R, 72, 0, -52); ps(p, B_FORE_R, 62, 0, 0);
	ps(p, B_ARM_L, 42, 0, -6);  ps(p, B_FORE_L, 92, 0, 0);
	ps(p, B_THIGH_L, 16, 0, 12); ps(p, B_SHIN_L, -24, 0, 0);
	ps(p, B_THIGH_R, -18, 0, -12); ps(p, B_SHIN_R, -14, 0, 0);
	pr(p, 0, 16, -6);
	p = &lib[L_BR_HOOK_H]; base_pose(p);
	ps(p, B_PELVIS, 0, -26, 0); ps(p, B_TORSO, -16, -64, 0); ps(p, B_HEAD, 6, 44, 0);
	ps(p, B_ARM_R, 88, 0, 22); ps(p, B_FORE_R, 40, 0, 0);
	ps(p, B_ARM_L, -22, 0, 38); ps(p, B_FORE_L, 30, 0, 0);
	ps(p, B_THIGH_L, 4, 0, 12); ps(p, B_THIGH_R, 22, 0, -12); ps(p, B_SHIN_R, -26, 0, 0);
	pr(p, 0, 16, 22);
	p = &lib[L_BR_HURT]; base_pose(p);
	ps(p, B_TORSO, 14, 0, 0);  ps(p, B_HEAD, 20, 0, 0);
	ps(p, B_ARM_L, 20, 0, 30); ps(p, B_ARM_R, 20, 0, -30);
	ps(p, B_THIGH_L, 4, 0, 10); ps(p, B_THIGH_R, 8, 0, -10); ps(p, B_SHIN_R, -14, 0, 0);
	pr(p, 0, 14, -14);
	p = &lib[L_BR_KNEEL]; base_pose(p);
	ps(p, B_TORSO, -32, 0, 0); ps(p, B_HEAD, -38, 0, 0);
	ps(p, B_ARM_L, 4, 0, 16); ps(p, B_ARM_R, 4, 0, -16); ps(p, B_FORE_L, 16, 0, 0); ps(p, B_FORE_R, 16, 0, 0);
	ps(p, B_THIGH_L, 92, 0, 10); ps(p, B_SHIN_L, -112, 0, 0);
	ps(p, B_THIGH_R, 90, 0, -10); ps(p, B_SHIN_R, -110, 0, 0);
	pr(p, 0, 66, 10);
	p = &lib[L_BR_FALL]; base_pose(p);
	ps(p, B_PELVIS, -88, 0, 0);
	ps(p, B_ARM_L, 24, 0, 52); ps(p, B_ARM_R, 24, 0, -52);
	ps(p, B_THIGH_L, -4, 0, 8); ps(p, B_THIGH_R, -2, 0, -8);
	pr(p, 0, 100, 20);

	/* ---------------- SCATTANTE: leggero, in guardia, agile ---------------- */
	p = &lib[L_ST_IDLE]; guard_pose(p);
	ps(p, B_TORSO, -14, 0, 0); ps(p, B_HEAD, 10, 0, 0);
	ps(p, B_ARM_L, 62, 0, -12); ps(p, B_FORE_L, 118, 0, 0);
	ps(p, B_ARM_R, 70, 0, 12);  ps(p, B_FORE_R, 122, 0, 0);
	ps(p, B_THIGH_L, 26, 0, 10); ps(p, B_SHIN_L, -34, 0, 0);
	ps(p, B_THIGH_R, -18, 0, -10); ps(p, B_SHIN_R, -26, 0, 0);
	pr(p, 0, 24, 0);
	p = &lib[L_ST_COIL]; base_pose(p);
	ps(p, B_TORSO, -36, 0, 0); ps(p, B_HEAD, 32, 0, 0);
	ps(p, B_ARM_L, -48, 0, 16); ps(p, B_ARM_R, -48, 0, -16); ps(p, B_FORE_L, 28, 0, 0); ps(p, B_FORE_R, 28, 0, 0);
	ps(p, B_THIGH_L, 66, 0, 12); ps(p, B_SHIN_L, -94, 0, 0);
	ps(p, B_THIGH_R, 52, 0, -12); ps(p, B_SHIN_R, -84, 0, 0);
	pr(p, 0, 50, -16);
	p = &lib[L_ST_KICK]; base_pose(p);
	ps(p, B_PELVIS, 42, 0, 0); ps(p, B_TORSO, 10, 0, 0); ps(p, B_HEAD, -26, 0, 0);
	ps(p, B_ARM_L, -34, 0, 58); ps(p, B_ARM_R, -22, 0, -62);
	ps(p, B_THIGH_R, 102, 0, -4); ps(p, B_SHIN_R, 0, 0, 0);
	ps(p, B_THIGH_L, 44, 0, 6); ps(p, B_SHIN_L, -80, 0, 0);
	pr(p, 0, -54, 22);
	p = &lib[L_ST_SLIDE]; base_pose(p);
	ps(p, B_TORSO, -24, 0, 0); ps(p, B_HEAD, 16, 0, 0);
	ps(p, B_ARM_L, 34, 0, 28); ps(p, B_ARM_R, 30, 0, -28);
	ps(p, B_THIGH_L, 74, 0, 8); ps(p, B_SHIN_L, -22, 0, 0);
	ps(p, B_THIGH_R, -54, 0, -10); ps(p, B_SHIN_R, -92, 0, 0);
	pr(p, 0, 52, 0);
	p = &lib[L_ST_HURT]; base_pose(p);
	ps(p, B_PELVIS, 0, 44, 0); ps(p, B_TORSO, 18, 30, 0); ps(p, B_HEAD, 22, -30, 0);
	ps(p, B_ARM_L, -22, 0, 52); ps(p, B_ARM_R, -22, 0, -52);
	ps(p, B_THIGH_L, -10, 0, 4); ps(p, B_THIGH_R, 16, 0, -4); ps(p, B_SHIN_R, -24, 0, 0);
	pr(p, 0, 6, -26);
	p = &lib[L_ST_DEAD]; base_pose(p);
	ps(p, B_PELVIS, 0, 0, 88);
	ps(p, B_ARM_L, 30, 0, 22); ps(p, B_ARM_R, 70, 0, -20); ps(p, B_FORE_R, 50, 0, 0);
	ps(p, B_THIGH_L, 44, 0, 4); ps(p, B_SHIN_L, -66, 0, 0);
	ps(p, B_THIGH_R, 20, 0, -4); ps(p, B_SHIN_R, -30, 0, 0);
	pr(p, 0, 100, 0);
}

/* ---- andature: un solo generatore, parametrizzato per personaggio ---- */
typedef struct { int thigh, knee, kmin, arm, eb, ea, lean, bob, sway, twist, stance; } GAIT;
static const GAIT G_RUN   = { 54, 72,  8, 48, 45, 40, 15, 12,  6, 7,  3 };
static const GAIT G_BRUTE = { 30, 44, 14, 32, 42, 16, 17, 22, 13, 5, 11 };
static const GAIT G_STRK  = { 64, 98, 10, 60, 88, 14, 30, 10,  3, 9,  2 };

static void gait_pose(POSE *p, int ph, int amp, const GAIT *g) {
	int s = sn(ph), c = cs(ph), c2 = cs(ph * 2);
	int A = (g->thigh * 8 * amp) >> 8, AA = (g->arm * 8 * amp) >> 8;
	int K = (g->knee * 8 * amp) >> 8, EA = (g->ea * 8 * amp) >> 8;
	int tw = (g->twist * 8 * amp) >> 8, ln = (g->lean * 8 * amp) >> 8;
	int fl = imax(0, cs(ph - 300)), fr = imax(0, cs(ph + 2048 - 300));
	int thL = (s * A) >> 12;
	base_pose(p);
	p->a[B_THIGH_L][0] = (int16_t)thL;               p->a[B_THIGH_R][0] = (int16_t)(-thL);
	p->a[B_SHIN_L][0]  = (int16_t)(-(g->kmin * 8 + ((K * fl) >> 12)));
	p->a[B_SHIN_R][0]  = (int16_t)(-(g->kmin * 8 + ((K * fr) >> 12)));
	p->a[B_THIGH_L][2] = (int16_t)((2 + g->stance) * 8); p->a[B_THIGH_R][2] = (int16_t)(-(2 + g->stance) * 8);
	p->a[B_ARM_L][0]   = (int16_t)(-((s * AA) >> 12));  p->a[B_ARM_R][0] = (int16_t)((s * AA) >> 12);
	p->a[B_FORE_L][0]  = (int16_t)(g->eb * 8 + ((EA * imax(0, -c)) >> 12));
	p->a[B_FORE_R][0]  = (int16_t)(g->eb * 8 + ((EA * imax(0, c)) >> 12));
	p->a[B_TORSO][0]   = (int16_t)(-ln + ((c2 * 16) >> 12));
	p->a[B_HEAD][0]    = (int16_t)((ln * 3) >> 2);
	p->a[B_PELVIS][0]  = (int16_t)(-(ln / 3));
	p->a[B_PELVIS][1]  = (int16_t)((s * tw) >> 12);
	p->a[B_TORSO][1]   = (int16_t)(-((s * tw * 2) >> 12));
	p->a[B_HEAD][1]    = (int16_t)((s * tw * 5) >> 14);
	p->a[B_PELVIS][2]  = (int16_t)((s * tw) >> 13);
	p->root[1] = (int16_t)((((g->bob * amp) >> 8) * (4096 - c2)) >> 13);
	p->root[0] = (int16_t)((((g->sway * amp) >> 8) * s) >> 12);
}

/* ---- stato di animazione per personaggio (posa corrente + mantello) ---- */
typedef struct { POSE cur; int c1, c2a, cz, cw, init; } ANIM;

static void anim_init(ANIM *a, int pose) { a->cur = lib[pose]; a->c1 = 0; a->c2a = 0; a->cz = 0; a->cw = 4096; a->init = 1; }

/* mantello: un'inerzia a due stadi (il secondo segue il primo in ritardo) + ondeggio da sterzata */
static void anim_cape(ANIM *a, int speed, int vy, int turn, int glide, int flare) {
	int t1 = -(4 * 8 + speed * 13);
	if (vy > 6) t1 -= imin(vy, 40) * 9;
	if (vy < -6) t1 += 18 * 8 / 2;
	if (glide) t1 = -14 * 8;
	if (flare) t1 = -62 * 8;
	t1 = iclamp(t1, -100 * 8, 12 * 8);
	a->c1 += (t1 - a->c1) >> 2;
	a->c2a += (a->c1 - a->c2a) >> 3;
	a->cz += ((glide ? -turn * 9 : -turn * 5) - a->cz) >> 2;
	a->cw += ((glide ? 13800 : 4096) - a->cw) >> 2;      /* in planata il mantello si allarga ad ala */
	a->cz = iclamp(a->cz, -24 * 8, 24 * 8);
}
static void anim_out(const ANIM *a, POSE *o) {
	*o = a->cur;
	o->a[B_CAPE1][0] = (int16_t)a->c1;
	o->a[B_CAPE2][0] = (int16_t)(a->c2a - a->c1);
	o->a[B_CAPE1][2] = (int16_t)a->cz;
}

static MATRIX bR[NB];
static int    bJ[NB][3];

static void fk(const POSE *p, int yaw, int wx, int wy, int wz) {
	MATRIX ry, rl, rx, rz, r2;
	int b;
	wx += (p->root[0] * cs(yaw) + p->root[2] * sn(yaw)) >> 12;
	wz += (-p->root[0] * sn(yaw) + p->root[2] * cs(yaw)) >> 12;
	wy += p->root[1];
	m_roty(&ry, yaw);
	for (b = 0; b < NB; b++) {
		int off[3], w[3], par = bone_parent[b];
		m_rotz(&rz, E2U(p->a[b][2]));
		m_rotx(&rx, E2U(p->a[b][0]));
		m_mul(&rz, &rx, &rl);
		if (p->a[b][1]) { m_roty(&r2, E2U(p->a[b][1])); m_mul(&r2, &rl, &rl); }
		off[0] = bone_off[b][0]; off[1] = bone_off[b][1]; off[2] = bone_off[b][2];
		if (par < 0) {
			m_mul(&ry, &rl, &bR[b]);
			m_vec(&ry, off, w);
			bJ[b][0] = wx + w[0]; bJ[b][1] = wy + w[1]; bJ[b][2] = wz + w[2];
		} else {
			m_mul(&bR[par], &rl, &bR[b]);
			m_vec(&bR[par], off, w);
			bJ[b][0] = bJ[par][0] + w[0]; bJ[b][1] = bJ[par][1] + w[1]; bJ[b][2] = bJ[par][2] + w[2];
		}
	}
}

/* ------------------------------------------------------------------ */
/* camera                                                              */
/* ------------------------------------------------------------------ */
static MATRIX Rc;          /* mondo -> vista */
static int    cam[3];
static int    cyaw, cpit = 200;
static int    fx, fy, fz;  /* fuoco della camera (smorzato) */
static uint8_t fogtab[OT_LEN];

static void cam_update(int tx, int ty, int tz, int dist) {
	MATRIX a, b;
	cam[0] = tx - ((sn(cyaw) * dist) >> 12);
	cam[2] = tz - ((cs(cyaw) * dist) >> 12);
	cam[1] = ty - 205;
	m_rotx(&a, cpit);
	m_roty(&b, -cyaw);
	m_mul(&a, &b, &Rc);
}

static int proj(int wx, int wy, int wz, int *sx, int *sy) {
	int v[3], o[3];
	v[0] = wx - cam[0]; v[1] = wy - cam[1]; v[2] = wz - cam[2];
	m_vec(&Rc, v, o);
	if (o[2] < 80) return 0;
	*sx = CX + (o[0] * PROJ_H) / o[2];
	*sy = CY + (o[1] * PROJ_H) / o[2];
	return o[2];
}

/* ------------------------------------------------------------------ */
/* mondo statico                                                       */
/* ------------------------------------------------------------------ */
enum { T_GND, T_OUT, T_ROOF, T_IN };

enum { TL_ASPH, TL_DASH, TL_SIDEW, TL_CURB, TL_WALL, TL_WIN, TL_WINL, TL_DOOR, TL_ROOF, TL_SHOP,
       TL_TRIM, TL_CRATE, TL_FLOOR, TL_NEON, TL_IWALL, TL_CONS, TL_IPLAIN };

typedef struct {
	SVECTOR v[4];
	uint8_t tile, shade, clut, type;
	uint8_t bld, flip;
	int16_t cx, cz;
} __attribute__((aligned(4))) WQ;

#define MAXQ 1500
static WQ  wq[MAXQ];
static int nwq;

typedef struct { int x0, x1, y0, y1, z0, z1; } CB;
static CB  cb[48];
static int ncb;

typedef struct { int x0, x1, z0, z1, H, dside, dcell, clut, drows; } BLD;
static BLD bl[3];

typedef struct { int x, y, z, inx; } ANCHOR;
static ANCHOR anc[4];
static int    nanc;

static void wq_add(int tile, int clut, int shade, int type, int bld, int flip,
                   int x0, int y0, int z0, int x1, int y1, int z1,
                   int x2, int y2, int z2, int x3, int y3, int z3) {
	WQ *q;
	if (nwq >= MAXQ) return;
	q = &wq[nwq++];
	q->v[0].vx = x0; q->v[0].vy = y0; q->v[0].vz = z0; q->v[0].pad = 0;
	q->v[1].vx = x1; q->v[1].vy = y1; q->v[1].vz = z1; q->v[1].pad = 0;
	q->v[2].vx = x2; q->v[2].vy = y2; q->v[2].vz = z2; q->v[2].pad = 0;
	q->v[3].vx = x3; q->v[3].vy = y3; q->v[3].vz = z3; q->v[3].pad = 0;
	q->tile = tile; q->shade = shade; q->clut = clut; q->type = type; q->bld = bld; q->flip = flip;
	q->cx = (x0 + x3) >> 1; q->cz = (z0 + z3) >> 1;
}

/* parete verticale tra (xa,za) e (xb,zb); normale uscente (nx,nz) */
static void qwall(int bld, int type, int tile, int clut, int shade, int flip,
                  int xa, int za, int xb, int zb, int ytop, int ybot, int nx, int nz) {
	int dot = (xb - xa) * (-nz) + (zb - za) * nx;
	int xl, zl, xr, zr;
	if (dot > 0) { xl = xa; zl = za; xr = xb; zr = zb; } else { xl = xb; zl = zb; xr = xa; zr = za; }
	wq_add(tile, clut, shade, type, bld, flip, xl, ytop, zl, xr, ytop, zr, xl, ybot, zl, xr, ybot, zr);
}
/* faccia rivolta verso l'alto */
static void qtop(int bld, int type, int tile, int clut, int shade, int x0, int z0, int x1, int z1, int y) {
	wq_add(tile, clut, shade, type, bld, 0, x0, y, z1, x1, y, z1, x0, y, z0, x1, y, z0);
}
/* faccia rivolta verso il basso */
static void qbot(int bld, int type, int tile, int clut, int shade, int x0, int z0, int x1, int z1, int y) {
	wq_add(tile, clut, shade, type, bld, 0, x0, y, z0, x1, y, z0, x0, y, z1, x1, y, z1);
}

static void add_cb(int x0, int x1, int y0, int y1, int z0, int z1) {
	CB *b;
	if (ncb >= 48) return;
	b = &cb[ncb++];
	b->x0 = x0; b->x1 = x1; b->y0 = y0; b->y1 = y1; b->z0 = z0; b->z1 = z1;
}

/* cassa/console: solido con collisione, 4 pareti + top */
static void make_box(int bld, int tile, int topt, int clut, int x0, int x1, int z0, int z1, int h) {
	int y = -h;
	qtop(bld, T_IN, topt, clut, 140, x0, z0, x1, z1, y);
	qwall(bld, T_IN, tile, clut, 110, 0, x0, z0, x1, z0, y, 0, 0, -1);
	qwall(bld, T_IN, tile, clut, 100, 0, x0, z1, x1, z1, y, 0, 0, 1);
	qwall(bld, T_IN, tile, clut, 120, 0, x0, z0, x0, z1, y, 0, -1, 0);
	qwall(bld, T_IN, tile, clut, 90, 0, x1, z0, x1, z1, y, 0, 1, 0);
	add_cb(x0, x1, y, 0, z0, z1);
}

static int ncell(int len) { int n = (len + 60) / 120; return n < 1 ? 1 : n; }

#define WT 24
static void make_building(int id, BLD b) {
	int H = b.H, rh = H / 4, T = WT, r, i, n, w;
	int dx = b.dside > 0 ? b.x1 : b.x0;               /* piano della porta (esterno) */
	int dxi = b.dside > 0 ? b.x1 - T : b.x0 + T;      /* piano interno */
	int ox = b.dside > 0 ? -1 : 1;                    /* direzione verso interno */
	int doorh = b.drows * rh;
	int ceil_y = -(H - 20);
	int nz = ncell(b.z1 - b.z0), nxc = ncell(b.x1 - b.x0);
	int zc0 = b.z0 + (b.z1 - b.z0) * b.dcell / nz;
	int zc1 = b.z0 + (b.z1 - b.z0) * (b.dcell + 1) / nz;
	int ix0 = b.x0 + T, ix1 = b.x1 - T, iz0 = b.z0 + T, iz1 = b.z1 - T;
	bl[id] = b;

	/* ---- parete della porta (esterna + interna) */
	for (i = 0; i < nz; i++) {
		int za = b.z0 + (b.z1 - b.z0) * i / nz, zb = b.z0 + (b.z1 - b.z0) * (i + 1) / nz;
		for (r = 0; r < 4; r++) {
			int yt = -((r + 1) * rh), yb = -(r * rh);
			int tile;
			if (i == b.dcell && r < b.drows) continue;
			if (r == 0) tile = TL_SHOP;
			else if (i == b.dcell && r == b.drows) tile = TL_NEON;
			else tile = (((i * 3 + r * 5 + id) & 3) == 0) ? TL_WINL : TL_WIN;
			qwall(id, T_OUT, tile, b.clut, b.dside > 0 ? 118 : 108, 0, dx, za, dx, zb, yt, yb, b.dside, 0);
			yt = -imin((r + 1) * rh, H - 20);
			qwall(id, T_IN, r ? TL_IPLAIN : TL_IWALL, b.clut, 120, 0, dxi, za, dxi, zb, yt, yb, -b.dside, 0);
		}
	}
	/* stipiti e architrave della porta */
	qwall(id, T_OUT, TL_TRIM, b.clut, 100, 0, dxi, zc0, dx, zc0, -doorh, 0, 0, 1);
	qwall(id, T_OUT, TL_TRIM, b.clut, 100, 0, dxi, zc1, dx, zc1, -doorh, 0, 0, -1);
	qbot(id, T_OUT, TL_TRIM, b.clut, 90, imin(dx, dxi), zc0, imax(dx, dxi), zc1, -doorh);

	/* ---- parete opposta */
	{
		int ox0 = b.dside > 0 ? b.x0 : b.x1;           /* piano esterno opposto */
		int oxi = b.dside > 0 ? b.x0 + T : b.x1 - T;
		for (i = 0; i < nz; i++) {
			int za = b.z0 + (b.z1 - b.z0) * i / nz, zb = b.z0 + (b.z1 - b.z0) * (i + 1) / nz;
			for (r = 0; r < 4; r++) {
				int yt = -((r + 1) * rh), yb = -(r * rh);
				int tile = (r == 0) ? TL_WALL : ((((i + r * 2 + id) & 3) == 1) ? TL_WINL : TL_WIN);
				qwall(id, T_OUT, tile, b.clut, b.dside > 0 ? 90 : 100, 0, ox0, za, ox0, zb, yt, yb, -b.dside, 0);
				yt = -imin((r + 1) * rh, H - 20);
				qwall(id, T_IN, r ? TL_IPLAIN : TL_IWALL, b.clut, 110, 0, oxi, za, oxi, zb, yt, yb, b.dside, 0);
			}
		}
		add_cb(imin(ox0, oxi), imax(ox0, oxi), -H, 0, b.z0, b.z1);
	}
	/* ---- pareti z0 / z1 */
	for (w = 0; w < 2; w++) {
		int zo = w ? b.z1 : b.z0, zi = w ? b.z1 - T : b.z0 + T, nzn = w ? 1 : -1;
		for (i = 0; i < nxc; i++) {
			int xa = b.x0 + (b.x1 - b.x0) * i / nxc, xb = b.x0 + (b.x1 - b.x0) * (i + 1) / nxc;
			for (r = 0; r < 4; r++) {
				int yt = -((r + 1) * rh), yb = -(r * rh);
				int tile = (r == 0) ? TL_WALL : ((((i * 2 + r + id + w) & 3) == 2) ? TL_WINL : TL_WIN);
				qwall(id, T_OUT, tile, b.clut, w ? 112 : 84, 0, xa, zo, xb, zo, yt, yb, 0, nzn);
				yt = -imin((r + 1) * rh, H - 20);
				qwall(id, T_IN, r ? TL_IPLAIN : TL_IWALL, b.clut, 108, 0, xa, zi, xb, zi, yt, yb, 0, -nzn);
			}
		}
		add_cb(b.x0, b.x1, -H, 0, imin(zo, zi), imax(zo, zi));
	}
	/* collisione parete porta: due tratti + architrave */
	{
		int xa = imin(dx, dxi), xb = imax(dx, dxi);
		add_cb(xa, xb, -H, 0, b.z0, zc0);
		add_cb(xa, xb, -H, 0, zc1, b.z1);
		add_cb(xa, xb, -H, -doorh, zc0, zc1);
	}
	/* ---- tetto (calpestabile) + soffitto + pavimento interno */
	n = ncell(b.x1 - b.x0);
	for (i = 0; i < n; i++) {
		int xa = b.x0 + (b.x1 - b.x0) * i / n, xb = b.x0 + (b.x1 - b.x0) * (i + 1) / n;
		for (r = 0; r < nz; r++) {
			int za = b.z0 + (b.z1 - b.z0) * r / nz, zb = b.z0 + (b.z1 - b.z0) * (r + 1) / nz;
			qtop(id, T_ROOF, TL_ROOF, 0, 120, xa, za, xb, zb, -H);
		}
	}
	add_cb(b.x0, b.x1, -H, ceil_y, b.z0, b.z1);
	n = ncell(ix1 - ix0);
	w = ncell(iz1 - iz0);
	for (i = 0; i < n; i++) {
		int xa = ix0 + (ix1 - ix0) * i / n, xb = ix0 + (ix1 - ix0) * (i + 1) / n;
		for (r = 0; r < w; r++) {
			int za = iz0 + (iz1 - iz0) * r / w, zb = iz0 + (iz1 - iz0) * (r + 1) / w;
			qtop(id, T_GND, TL_FLOOR, b.clut, 118, xa, za, xb, zb, 0);
			qbot(id, T_IN, TL_ROOF, 0, 80, xa, za, xb, zb, ceil_y);
		}
	}
	/* ancora per il rampino sul bordo del tetto, lato strada */
	if (nanc < 4) {
		anc[nanc].x = dx + (b.dside > 0 ? 34 : -34);
		anc[nanc].y = -(H + 50);
		anc[nanc].z = (b.z0 + b.z1) / 2;
		anc[nanc].inx = ox;
		nanc++;
	}
}

static void build_world(void) {
	int x, z, i, j;
	BLD b;
	nwq = 0; ncb = 0; nanc = 0;
	/* strada e marciapiedi */
	for (z = -2600; z < 2600; z += 100) {
		for (i = 0; i < 3; i++) {
			int xa = -300 + i * 200;
			qtop(0, T_GND, i == 1 ? TL_DASH : TL_ASPH, 0, 124, xa, z, xa + 200, z + 100, 0);
		}
		wq_add(TL_CURB, 0, 118, T_GND, 0, 0, -550, 0, z + 100, -300, 0, z + 100, -550, 0, z, -300, 0, z);
		wq_add(TL_CURB, 0, 118, T_GND, 0, 1, 300, 0, z + 100, 550, 0, z + 100, 300, 0, z, 550, 0, z);
	}
	/* terreno ai lati (scuro) */
	for (z = -2700; z < 2700; z += 300) {
		for (x = 550; x < 1550; x += 250) {
			qtop(0, T_GND, TL_SIDEW, 0, 74, x, z, x + 250, z + 300, 0);
			qtop(0, T_GND, TL_SIDEW, 0, 74, -x - 250, z, -x, z + 300, 0);
		}
	}
	/* edifici */
	b.x0 = -1150; b.x1 = -550; b.z0 = -900; b.z1 = -400; b.H = 420; b.dside = 1;  b.dcell = 2; b.clut = 1; b.drows = 3;
	make_building(0, b);
	b.x0 = -1150; b.x1 = -550; b.z0 = -250; b.z1 = 250;  b.H = 420; b.dside = 1;  b.dcell = 1; b.clut = 3; b.drows = 3;
	make_building(1, b);
	b.x0 = 550;   b.x1 = 1150; b.z0 = 100;  b.z1 = 700;  b.H = 580; b.dside = -1; b.dcell = 2; b.clut = 2; b.drows = 2;
	make_building(2, b);
	/* arredi: console di ricarica (edificio C=1), casse */
	make_box(1, TL_CONS, TL_ROOF, 0, -1100, -1040, -80, 80, 110);
	make_box(0, TL_CRATE, TL_CRATE, 0, -1110, -1030, -860, -780, 100);
	make_box(0, TL_CRATE, TL_CRATE, 0, -1110, -1030, -770, -690, 100);
	make_box(2, TL_CRATE, TL_CRATE, 0, 1000, 1100, 140, 240, 120);
	make_box(9, TL_CRATE, TL_CRATE, 0, 420, 500, -700, -620, 100);
	(void)j;
}

/* ------------------------------------------------------------------ */
/* collisioni                                                          */
/* ------------------------------------------------------------------ */
#define PR 26
#define PH 220

static int blocked(int x, int z, int feet) {
	int i;
	for (i = 0; i < ncb; i++) {
		const CB *b = &cb[i];
		if (feet > b->y0 + 20 && feet - PH < b->y1 &&
		    x > b->x0 - PR && x < b->x1 + PR && z > b->z0 - PR && z < b->z1 + PR) return 1;
	}
	return 0;
}
static void move_xz(int *x, int *z, int feet, int dx, int dz) {
	if (!blocked(*x + dx, *z, feet)) *x += dx;
	if (!blocked(*x, *z + dz, feet)) *z += dz;
	*x = iclamp(*x, -1480, 1480);
	*z = iclamp(*z, -1740, 1740);
}
/* supporto verticale: restituisce y del piano su cui si atterra, oppure 1 se nessuno */
static int land(int x, int z, int pf, int nf) {
	int i, best = 1;
	for (i = 0; i < ncb; i++) {
		const CB *b = &cb[i];
		if (pf <= b->y0 + 1 && nf >= b->y0 &&
		    x > b->x0 - 10 && x < b->x1 + 10 && z > b->z0 - 10 && z < b->z1 + 10) {
			if (best == 1 || b->y0 < best) best = b->y0;
		}
	}
	if (best == 1 && nf >= 0) best = 0;
	if (best != 1 && best > 0) best = 0;
	return best;
}

/* ------------------------------------------------------------------ */
/* stato di gioco                                                      */
/* ------------------------------------------------------------------ */
enum { G_TITLE, G_PLAY, G_WIN };
enum { E_IDLE, E_CHASE, E_CIRCLE, E_WIND, E_STRIKE, E_RECOV, E_HURT, E_DEAD };
enum { T_BRUTE, T_STRIKER };
enum { A_NONE, A_JAB, A_HOOK, A_SPIN, A_KICK, A_PULSE };

typedef struct {
	int x, y, z, vy, face, hp, en, grounded;
	int atk, atkt, buf, dodge, hurt, inv, jsq, landt, mantle, fallv;
	int gr, gr_a, gr_t;
	int pushx, pushz, pusht;
	int ph, glide, dead, moving, spd, turn, hitdone;
	int lock, dinv, recenter, rel;
	ANIM an;
} PL;

typedef struct {
	int x, y, z, face, hp, hpmax, type, state, t, cool, alive;
	int kbx, kbz, ph, cnt, dir, hitdone, tt, spd, atype, turn;
	ANIM an;
} EN;
typedef struct { int x, y, z, dx, dz, life; } PROJ;

#define MAXEN 2
static PL   pl;
static EN   en[MAXEN];
static int  nen;
static PROJ pu[3];
static int  gstate = G_TITLE, stage, wintimer, frame_no, hitflash, hitstop, shake, hint_t, rs_prev;

static unsigned btn, btn_prev, btn_new, pend_new;
static int      lsx, lsy, rsx;

static const int START_X = 0, START_Z = -1500;
static const int BEACON_X = 850, BEACON_Z = 400, BEACON_Y = -580;
static const int CORE_X = -840, CORE_Z = -650;
static const int CONS_X = -1000, CONS_Z = 0;

static void player_reset(void) {
	memset(&pl, 0, sizeof(pl));
	pl.x = START_X; pl.z = START_Z; pl.face = 0; pl.hp = 100; pl.en = 100; pl.grounded = 1; pl.lock = -1;
	anim_init(&pl.an, L_IDLE);
	fx = pl.x; fy = pl.y; fz = pl.z; cyaw = 0;
}
static void enemy_spawn(int i, int type, int x, int z) {
	EN *e = &en[i];
	memset(e, 0, sizeof(*e));
	e->type = type; e->x = x; e->z = z; e->alive = 1; e->state = E_IDLE; e->face = 2048;
	e->hpmax = e->hp = (type == T_BRUTE) ? 100 : 56;
	anim_init(&e->an, type == T_BRUTE ? L_BR_IDLE : L_ST_IDLE);
	if (i >= nen) nen = i + 1;
}
static void mission_reset(void) {
	player_reset();
	memset(en, 0, sizeof(en)); nen = 0;
	memset(pu, 0, sizeof(pu));
	stage = 0; wintimer = 0; hitstop = 0; shake = 0;
}

static void read_pad(void) {
	PADTYPE *pad = (PADTYPE *)&pad_buff[0][0];
	unsigned b = 0;
	lsx = 0; lsy = 0; rsx = 0;
	if (pad->stat == 0 && (pad->type == 0x4 || pad->type == 0x5 || pad->type == 0x7)) {
		b = (~pad->btn) & 0xffff;
		if (pad->type == 0x5 || pad->type == 0x7) {
			int ax = pad->ls_x - 128, ay = pad->ls_y - 128;
			if (iabs(ax) > 24) lsx = ax;
			if (iabs(ay) > 24) lsy = ay;
			if (iabs(pad->rs_x - 128) > 30) rsx = pad->rs_x - 128;
		}
	}
	btn_new = b & ~btn_prev;
	btn_prev = b;
	btn = b;
	if (b & PAD_LEFT)  lsx = -127;
	if (b & PAD_RIGHT) lsx = 127;
	if (b & PAD_UP)    lsy = -127;
	if (b & PAD_DOWN)  lsy = 127;
}

static int alive_count(void) { int i, n = 0; for (i = 0; i < nen; i++) if (en[i].alive && en[i].state != E_DEAD) n++; return n; }

static int nearest_enemy(int maxd) {
	int i, best = -1, bd = maxd * maxd;
	for (i = 0; i < nen; i++) if (en[i].alive && en[i].state != E_DEAD) {
		int dx = en[i].x - pl.x, dz = en[i].z - pl.z, d2 = dx * dx + dz * dz;
		if (d2 < bd) { bd = d2; best = i; }
	}
	return best;
}

static void damage_player(int dmg, int fromx, int fromz, int big) {
	int d;
	if (pl.inv > 0 || pl.dead) return;
	pl.hp -= dmg; pl.inv = 26; pl.dinv = 26; pl.hurt = big ? 18 : 12; pl.atk = 0; pl.buf = 0; pl.gr = 0; pl.jsq = 0; hitflash = 4;
	hitstop = big ? 5 : 3; shake = big ? 7 : 4;
	d = iatan2(pl.x - fromx, pl.z - fromz);
	pl.pushx = (sn(d) * (big ? 20 : 14)) >> 12; pl.pushz = (cs(d) * (big ? 20 : 14)) >> 12; pl.pusht = big ? 9 : 7;
	if (pl.hp <= 0) { pl.hp = 0; pl.dead = 1; }
}

static void hit_enemy(int i, int dmg, int power) {
	EN *e = &en[i];
	int d, armor;
	if (!e->alive || e->state == E_DEAD) return;
	e->hp -= dmg;
	d = iatan2(e->x - pl.x, e->z - pl.z);
	e->kbx = (sn(d) * power) >> 12; e->kbz = (cs(d) * power) >> 12;
	hitflash = 3; hitstop = (dmg >= 20) ? 5 : 3; shake = (dmg >= 20) ? 5 : 2;
	if (e->hp <= 0) {
		e->state = E_DEAD; e->t = 0;
		if (alive_count() == 0 && stage == 1) stage = 2;
		return;
	}
	/* il bruto non si interrompe durante l'attacco per colpi leggeri */
	armor = (e->type == T_BRUTE && (e->state == E_WIND || e->state == E_STRIKE) && dmg < 20);
	if (!armor) { e->state = E_HURT; e->t = 0; }
}

static int in_arc(int ex, int ez, int range, int ox, int oz, int face) {
	int dx = ex - ox, dz = ez - oz;
	int dist2 = dx * dx + dz * dz;
	int dot = dx * sn(face) + dz * cs(face);
	if (dist2 < 100 * 100) return 1;                 /* quasi addosso: colpisce comunque */
	return dist2 < range * range && dot > 0;
}

static int inside_bld(int x, int y, int z) {
	int i;
	for (i = 0; i < 3; i++)
		if (x > bl[i].x0 + WT && x < bl[i].x1 - WT && z > bl[i].z0 + WT && z < bl[i].z1 - WT && y > -(bl[i].H - 60))
			return i;
	return -1;
}

static void try_grapple(void) {
	int i, best = -1, bd = 0x7fffffff;
	for (i = 0; i < nanc; i++) {
		int dx = anc[i].x - pl.x, dy = anc[i].y - (pl.y - 120), dz = anc[i].z - pl.z;
		int d2 = (dx >> 2) * (dx >> 2) + (dy >> 2) * (dy >> 2) + (dz >> 2) * (dz >> 2);
		if (d2 < (1500 >> 2) * (1500 >> 2) && d2 < bd) { bd = d2; best = i; }
	}
	if (best >= 0) { pl.gr = 1; pl.gr_a = best; pl.gr_t = 0; pl.atk = 0; pl.buf = 0; pl.glide = 0; }
}

/* durata, frame del colpo e dati per ogni attacco del giocatore */
static int atk_dur(int a)  { static const int d[6] = {0, 13, 14, 27, 22, 16}; return d[a]; }
static int atk_hit(int a)  { static const int d[6] = {0, 5, 5, 14, 10, 6}; return d[a]; }

static void cycle_lock(void) {
	int k, n = pl.lock;
	for (k = 1; k <= nen; k++) {
		int j = (pl.lock + k) % nen;
		if (en[j].alive && en[j].state != E_DEAD) { n = j; break; }
	}
	pl.lock = n;
}

/* un nemico troppo vicino davanti: niente scatto (evita di compenetrarsi) */
static int enemy_too_close(int pad) {
	int i;
	for (i = 0; i < nen; i++) if (en[i].alive && en[i].state != E_DEAD) {
		int dx = en[i].x - pl.x, dz = en[i].z - pl.z, md = (en[i].type == T_BRUTE ? 96 : 78) + pad;
		if (dx * dx + dz * dz < md * md) return 1;
	}
	return 0;
}
static void pl_lunge(int sp) {
	if (!enemy_too_close(10)) move_xz(&pl.x, &pl.z, pl.y, (sn(pl.face) * sp) >> 12, (cs(pl.face) * sp) >> 12);
}

static void start_attack(int a) {
	int t = (pl.lock >= 0) ? pl.lock : nearest_enemy(380);
	pl.atk = a; pl.atkt = 0; pl.buf = 0; pl.hitdone = 0;
	if (t >= 0 && en[t].alive) {         /* assistenza alla mira: con l'aggancio guarda sempre il bersaglio */
		int dx = en[t].x - pl.x, dz = en[t].z - pl.z, want = iatan2(dx, dz), d2 = dx * dx + dz * dz;
		if (pl.lock >= 0 ? d2 < 900 * 900 : (d2 < 260 * 260 || iabs(angdiff(want, pl.face)) < 1400)) pl.face = want;
	}
}

static void update_player(void) {
	int mx = 0, mz = 0, moving = 0, spd = 16, ang = 0, i, oldface = pl.face, wasg;
	if (pl.dead) {
		pl.dead++;
		pl.spd = 0; pl.moving = 0;
		if (pl.dead > 90) {
			int keep = stage, k;
			EN e2[MAXEN]; int nn = nen;
			memcpy(e2, en, sizeof(en));
			player_reset();
			stage = keep;
			memcpy(en, e2, sizeof(en)); nen = nn;
			for (k = 0; k < nen; k++) if (en[k].alive) {
				en[k].state = E_IDLE; en[k].hp = imin(en[k].hpmax, en[k].hp + 20);
				en[k].x = (k ? 260 : -60); en[k].z = 600 + k * 300; anim_init(&en[k].an, en[k].type == T_BRUTE ? L_BR_IDLE : L_ST_IDLE);
			}
			memset(pu, 0, sizeof(pu));
		}
		return;
	}
	if (pl.hurt > 0) pl.hurt--;
	if (pl.inv > 0) pl.inv--;
	if (pl.dinv > 0) pl.dinv--;
	if (pl.buf > 0) pl.buf--;
	if (pl.mantle > 0) pl.mantle--;
	if (pl.landt > 0) pl.landt--;
	if (pl.en < 100 && (frame_no & 3) == 0) pl.en++;

	/* aggancio: L1 aggancia/sgancia, L2 o stick destro cambiano bersaglio */
	if (btn_new & PAD_L1) {
		if (pl.lock >= 0) pl.lock = -1;
		else { int t = nearest_enemy(1100); if (t >= 0) pl.lock = t; else pl.recenter = 16; }
	}
	if (pl.lock >= 0) {
		EN *le = &en[pl.lock];
		int ddx = le->x - pl.x, ddz = le->z - pl.z;
		if (!le->alive || le->state == E_DEAD || ddx * ddx + ddz * ddz > 1500 * 1500) pl.lock = nearest_enemy(1100);
	}
	if (pl.lock >= 0 && ((btn_new & PAD_L2) || (iabs(rsx) > 90 && !rs_prev))) cycle_lock();
	rs_prev = iabs(rsx) > 90;
	if (pl.lock >= 0) {
		int want = iatan2(en[pl.lock].x - pl.x, en[pl.lock].z - pl.z);
		cyaw += angdiff(want, cyaw) >> 3;
	} else {
		if (rsx) cyaw += rsx * 3;
		if ((btn & PAD_SELECT) && (btn & PAD_LEFT)) cyaw -= 60;
		if ((btn & PAD_SELECT) && (btn & PAD_RIGHT)) cyaw += 60;
		if (pl.recenter > 0) { pl.recenter--; cyaw += angdiff(pl.face, cyaw) >> 2; }
	}

	/* ---- rampino */
	if (pl.gr) {
		ANCHOR *a = &anc[pl.gr_a];
		int dx = a->x - pl.x, dy = (a->y + 235) - pl.y, dz = a->z - pl.z;
		int dist = isqrt(dx * dx + dy * dy + dz * dz), s;
		pl.gr_t++;
		pl.face = iatan2(dx, dz);
		s = imin(dist, 18 + pl.gr_t * 3);
		if (s > 38) s = 38;
		if (dist > 1) { pl.x += dx * s / dist; pl.y += dy * s / dist; pl.z += dz * s / dist; }
		pl.vy = 0; pl.grounded = 0; pl.spd = s / 2; pl.moving = 0;
		if (dist < 60 || pl.gr_t > 60) {
			pl.gr = 0; pl.vy = -42; pl.pushx = a->inx * 15; pl.pushz = 0; pl.pusht = 28; pl.mantle = 18;
		}
		return;
	}

	/* ---- input di movimento (relativo alla camera) */
	if (lsx || lsy) {
		int m = imax(iabs(lsx), iabs(lsy));
		ang = (cyaw + iatan2(lsx, -lsy)) & 4095;
		mx = sn(ang); mz = cs(ang);
		moving = 1;
		spd = (16 * iclamp(m * 2, 100, 256)) >> 8;
		if (spd < 8) spd = 8;
	}

	/* ---- azioni */
	if (pl.atk) {
		int a = pl.atk, t, dur = atk_dur(a), hit = atk_hit(a);
		pl.atkt++; t = pl.atkt;
		spd = 4;
		if ((btn_new & PAD_SQUARE) && a < A_SPIN) pl.buf = 14;       /* prenota il colpo successivo della combo */
		/* avanzamento durante il colpo */
		if (a == A_JAB  && t >= 3 && t <= 6)  pl_lunge(9);
		if (a == A_HOOK && t >= 3 && t <= 6)  pl_lunge(8);
		if (a == A_SPIN && t >= 7 && t <= 14) pl_lunge(11);
		if (a == A_KICK && t >= 7 && t <= 11) pl_lunge(14);
		if (t == hit) {
			if (a == A_PULSE) {
				for (i = 0; i < 3; i++) if (!pu[i].life) {
					pu[i].x = pl.x + ((sn(pl.face) * 60) >> 12);
					pu[i].z = pl.z + ((cs(pl.face) * 60) >> 12);
					pu[i].y = pl.y - 140;
					pu[i].dx = (sn(pl.face) * 34) >> 12; pu[i].dz = (cs(pl.face) * 34) >> 12;
					pu[i].life = 36;
					break;
				}
			} else {
				int dmg = a == A_JAB ? 9 : a == A_HOOK ? 11 : a == A_SPIN ? 20 : 22;
				int pw  = a == A_JAB ? 3 : a == A_HOOK ? 5 : a == A_SPIN ? 22 : 30;     /* i colpi leggeri non scaraventano via: la combo resta a portata */
				int rg  = a == A_SPIN ? 260 : (a == A_KICK ? 270 : 235);
				for (i = 0; i < nen; i++)
					if (en[i].alive && en[i].state != E_DEAD && in_arc(en[i].x, en[i].z, rg, pl.x, pl.z, pl.face)) hit_enemy(i, dmg, pw);
			}
		}
		/* fine colpo: combo, oppure ritorno al controllo */
		if (t >= dur - 2 && pl.buf > 0 && a < A_SPIN) start_attack(a + 1);
		else if (t >= dur) pl.atk = 0;
		else if (t > hit + 2 && (btn_new & PAD_CIRCLE)) { pl.atk = 0; pl.dodge = 14; pl.inv = 17; }   /* annulla in schivata */
	} else if (pl.dodge > 0) {
		pl.dodge--;
		if (moving) pl.face += iclamp(angdiff(ang, pl.face), -500, 500);
		mx = sn(pl.face); mz = cs(pl.face); moving = 1; spd = pl.dodge > 6 ? 27 : 15;
	} else if (pl.jsq > 0) {
		pl.jsq--;
		if (pl.jsq == 0) { pl.vy = -38; pl.grounded = 0; }
		spd = 6;
	} else {
		if (pl.lock >= 0 && pl.grounded) {                 /* agganciato: il corpo guarda il bersaglio e ci si sposta di lato */
			int want = iatan2(en[pl.lock].x - pl.x, en[pl.lock].z - pl.z);
			pl.face += iclamp(angdiff(want, pl.face), -560, 560);
		} else if (moving) {
			int td = angdiff(ang, pl.face);
			int tr = pl.grounded ? 460 : 280;
			pl.face += iclamp(td, -tr, tr);
		}
		if ((btn_new & PAD_CROSS) && pl.grounded) pl.jsq = 3;
		if (btn_new & PAD_SQUARE) start_attack(A_JAB);
		else if (btn_new & PAD_TRIANGLE) start_attack(A_KICK);
		else if ((btn_new & PAD_R1) && pl.en >= 25) { start_attack(A_PULSE); pl.en -= 25; }
		else if ((btn_new & PAD_CIRCLE) && pl.grounded) { pl.dodge = 14; pl.inv = 17; }
		else if (btn_new & PAD_R2) try_grapple();
	}
	pl.rel = moving ? angdiff(ang, pl.face) : 0;
	if (pl.lock >= 0 && moving && !pl.atk && !pl.dodge && pl.grounded && iabs(pl.rel) > 700) spd = (spd * 13) >> 4;

	/* ---- spostamento orizzontale */
	pl.moving = moving && pl.atk == 0 && pl.jsq == 0;
	if (moving) {
		int dx = (mx * spd) >> 12, dz = (mz * spd) >> 12;
		if (!pl.grounded && !pl.atk && !pl.dodge) { dx = (dx * 14) >> 4; dz = (dz * 14) >> 4; }
		move_xz(&pl.x, &pl.z, pl.y, dx, dz);
		pl.ph += 90 + spd * 9;
	}
	if (pl.pusht > 0) {
		pl.pusht--;
		move_xz(&pl.x, &pl.z, pl.y, pl.pushx, pl.pushz);
	}

	/* ---- planata */
	pl.glide = 0;
	if (!pl.grounded && (btn & PAD_CROSS) && pl.vy > 2 && !pl.atk) {
		pl.glide = 1;
		pl.vy = 3;
		move_xz(&pl.x, &pl.z, pl.y, (sn(pl.face) * 21) >> 12, (cs(pl.face) * 21) >> 12);
		if (moving) pl.face += iclamp(angdiff(ang, pl.face), -120, 120);
	}

	/* ---- verticale */
	wasg = pl.grounded;
	if (!pl.glide) pl.vy += 3;
	if (pl.vy > 60) pl.vy = 60;
	{
		int nf = pl.y + pl.vy, fv = pl.vy;
		int sup = land(pl.x, pl.z, pl.y, nf);
		if (sup != 1 && pl.vy >= 0) { pl.y = sup; pl.vy = 0; pl.grounded = 1; }
		else { pl.y = nf; pl.grounded = 0; if (pl.y > 0) { pl.y = 0; pl.vy = 0; pl.grounded = 1; } }
		if (pl.grounded && !wasg && fv > 22) { pl.landt = imin(8, 3 + fv / 10); shake = imax(shake, fv / 12); }
	}

	/* ---- impulsi */
	for (i = 0; i < 3; i++) if (pu[i].life) {
		int k;
		pu[i].x += pu[i].dx; pu[i].z += pu[i].dz; pu[i].life--;
		for (k = 0; k < nen; k++) if (en[k].alive && en[k].state != E_DEAD && pu[i].life) {
			int dx = pu[i].x - en[k].x, dz = pu[i].z - en[k].z;
			if (dx * dx + dz * dz < 110 * 110 && pu[i].y < 0 && pu[i].y > -250) {
				hit_enemy(k, 26, 36); pu[i].life = 0; pl.en = imin(100, pl.en + 6);
			}
		}
		if (pu[i].life && blocked(pu[i].x, pu[i].z, 0)) {
			int kk, hit = 0;
			for (kk = 0; kk < ncb; kk++) {
				const CB *b = &cb[kk];
				if (pu[i].x > b->x0 && pu[i].x < b->x1 && pu[i].z > b->z0 && pu[i].z < b->z1 && pu[i].y > b->y0 && pu[i].y < b->y1) hit = 1;
			}
			if (hit) pu[i].life = 0;
		}
	}
	pl.turn = angdiff(pl.face, oldface);
	{ int sp = pl.moving ? spd : (pl.glide ? 21 : (pl.dodge ? 26 : 0)); pl.spd += (sp - pl.spd) >> 1; }
}

/* ---------------- nemici ---------------- */
static void en_turn(EN *e, int rate) {
	int want = iatan2(pl.x - e->x, pl.z - e->z), d = iclamp(angdiff(want, e->face), -rate, rate);
	e->face += d; e->turn = d;
}
static void en_step(EN *e, int ang, int s) { move_xz(&e->x, &e->z, 0, (sn(ang) * s) >> 12, (cs(ang) * s) >> 12); }

static void update_enemy(EN *e) {
	int dx, dz, dist, aggro, k;
	if (!e->alive) return;
	e->turn = 0; e->spd = 0;
	if (e->cool > 0) e->cool--;
	dx = pl.x - e->x; dz = pl.z - e->z; dist = isqrt(dx * dx + dz * dz);
	aggro = !pl.dead && pl.y > -150;
	switch (e->state) {
	case E_IDLE:
		if (dist < 1100 && aggro) e->state = E_CHASE;
		break;
	case E_CHASE:
		if (!aggro) { e->state = E_IDLE; break; }
		if (e->type == T_BRUTE) {
			en_turn(e, 200);
			if (dist > 130) { int s = dist > 450 ? 9 : 7; en_step(e, e->face, s); e->spd = s; e->ph += 70 + s * 6; }
			if (dist < 175 && e->cool == 0) { e->state = E_WIND; e->t = 0; e->hitdone = 0; e->atype = e->cnt & 1; e->cnt++; }
		} else {
			en_turn(e, 420);
			if (dist > 430) { en_step(e, e->face, 15); e->spd = 15; e->ph += 90 + 15 * 8; }
			else { e->state = E_CIRCLE; e->t = 0; e->dir = (e->cnt & 1) ? 1 : -1; e->cnt++; e->tt = 40 + ((frame_no * 7 + e->x + 99) & 31); }
		}
		break;
	case E_CIRCLE: {              /* solo scattante: gira attorno al giocatore in guardia */
		int perp, radial;
		if (!aggro) { e->state = E_IDLE; break; }
		e->t++;
		en_turn(e, 420);
		perp = iatan2(dx, dz) + e->dir * 1024;
		en_step(e, perp, 9);
		radial = iclamp((dist - 330) / 6, -7, 7);
		en_step(e, iatan2(dx, dz), radial);
		e->spd = 9; e->ph += 90 * e->dir + 60;
		if ((e->t >= e->tt && e->cool == 0) || (dist < 150 && e->cool == 0)) { e->state = E_WIND; e->t = 0; e->hitdone = 0; }
		break; }
	case E_WIND:
		e->t++;
		if (e->type == T_BRUTE) {
			en_turn(e, 140);
			if (e->t >= (e->atype ? 15 : 24)) { e->state = E_STRIKE; e->t = 0; }
		} else {
			en_turn(e, e->t < 6 ? 400 : 60);
			if (e->t >= 9) { e->state = E_STRIKE; e->t = 0; }
		}
		break;
	case E_STRIKE:
		e->t++;
		if (e->type == T_BRUTE) {
			if (!e->atype) {                                                    /* martellata dall'alto */
				if (e->t == 3 && in_arc(pl.x, pl.z, 200, e->x, e->z, e->face)) damage_player(20, e->x, e->z, 1);
				if (e->t == 3) shake = imax(shake, 6);
				if (e->t >= 8) { e->state = E_RECOV; e->t = 0; }
			} else {                                                            /* gancio a spazzata */
				if (e->t >= 2 && e->t <= 4 && !e->hitdone && in_arc(pl.x, pl.z, 225, e->x, e->z, e->face)) { damage_player(14, e->x, e->z, 0); e->hitdone = 1; }
				if (e->t >= 7) { e->state = E_RECOV; e->t = 0; }
			}
		} else {                                                                /* balzo con calcio volante */
			if (e->t <= 10 && dist > 92) { en_step(e, e->face, 27); e->spd = 27; }
			if (!e->hitdone && dist < 140) { damage_player(13, e->x, e->z, 1); e->hitdone = 1; }
			if (e->t >= 11) { e->state = E_RECOV; e->t = 0; }
		}
		break;
	case E_RECOV:
		e->t++;
		if (e->type == T_BRUTE) {
			if (e->t >= (e->atype ? 13 : 24)) { e->state = E_CHASE; e->cool = 18; }
		} else {
			if (e->t < 8) en_step(e, e->face, 12 - e->t);                       /* scivola */
			if (e->t >= 26) { e->state = E_CHASE; e->cool = 40; }
		}
		break;
	case E_HURT:
		e->t++;
		if (e->t < 8) move_xz(&e->x, &e->z, 0, e->kbx, e->kbz);
		if (e->t >= (e->type == T_BRUTE ? 11 : 15)) { e->state = E_CHASE; e->cool = 0; }
		break;
	case E_DEAD:
		e->t++;
		if (e->t < 8) move_xz(&e->x, &e->z, 0, e->kbx, e->kbz);
		if (e->t > 70) e->alive = 0;
		break;
	}
	/* separazione tra nemici (quella dal giocatore e' in resolve_overlap) */
	for (k = 0; k < nen; k++) if (&en[k] != e && en[k].alive && e->state != E_DEAD && en[k].state != E_DEAD) {
		int ex = e->x - en[k].x, ez = e->z - en[k].z, d2 = ex * ex + ez * ez;
		if (d2 < 90 * 90 && d2 > 0) { int d = isqrt(d2); move_xz(&e->x, &e->z, 0, ex * 4 / d, ez * 4 / d); }
	}
}

/* dopo tutti i movimenti: giocatore e nemici non restano mai uno dentro l'altro */
static void resolve_overlap(void) {
	int i, it;
	if (pl.dead || pl.dodge > 0 || pl.y < -160) return;       /* la rotolata e il salto passano oltre */
	for (it = 0; it < 2; it++)
		for (i = 0; i < nen; i++) {
			EN *e = &en[i];
			int dx, dz, dist, md, push, ps, ux, uz;
			if (!e->alive || e->state == E_DEAD) continue;
			dx = pl.x - e->x; dz = pl.z - e->z;
			dist = isqrt(dx * dx + dz * dz);
			md = e->type == T_BRUTE ? 96 : 78;
			if (dist >= md) continue;
			if (dist < 6) { dx = -sn(pl.face); dz = -cs(pl.face); dist = 4096; push = md; }
			else push = md - dist;
			ps = e->type == T_BRUTE ? 55 : 70;                 /* quota di spinta sul giocatore (%) */
			ux = dx * push / dist; uz = dz * push / dist;
			move_xz(&pl.x, &pl.z, pl.y, ux * ps / 100, uz * ps / 100);
			move_xz(&e->x, &e->z, 0, -ux * (100 - ps) / 100, -uz * (100 - ps) / 100);
		}
}

static void update_mission(void) {
	if (stage == 0) {
		if (pl.y <= BEACON_Y + 12 && iabs(pl.x - BEACON_X) < 260 && iabs(pl.z - BEACON_Z) < 260) {
			stage = 1; hint_t = 330;
			enemy_spawn(0, T_BRUTE, -60, 620);
			enemy_spawn(1, T_STRIKER, 260, 980);
		}
	} else if (stage == 2) {
		if (inside_bld(pl.x, pl.y, pl.z) == 0 && iabs(pl.x - CORE_X) < 110 && iabs(pl.z - CORE_Z) < 110) {
			stage = 3; wintimer = 0; gstate = G_WIN;
		}
	}
	if (inside_bld(pl.x, pl.y, pl.z) == 1 && iabs(pl.x - CONS_X) < 190 && iabs(pl.z - CONS_Z) < 190 && !pl.dead) {
		pl.hp = imin(100, pl.hp + 2);
		pl.en = imin(100, pl.en + 4);
	}
}

/* ------------------------------------------------------------------ */
/* disegno                                                             */
/* ------------------------------------------------------------------ */
#define NEWPRIM(T, v) T *v; if (nextpri + sizeof(T) > pri_end) return; v = (T *)nextpri; nextpri += sizeof(T);

static void draw_backdrop(void) {
	int hy = CY - (PROJ_H * sn(cpit)) / cs(cpit);
	NEWPRIM(POLY_F4, p);
	if (hy < 0) hy = 0;
	if (hy > 200) hy = 200;
	setPolyF4(p);
	p->r0 = 24; p->g0 = 26; p->b0 = 38;
	p->x0 = 0; p->y0 = hy; p->x1 = SCR_W; p->y1 = hy; p->x2 = 0; p->y2 = SCR_H; p->x3 = SCR_W; p->y3 = SCR_H;
	addPrim(&db[dba].ot[OT_LEN - 1], p);
}

static void draw_world(int cutaway) {
	int i, nc, otz, z0, z1, z2, z3;
	draw_backdrop();
	MATRIX m = Rc;
	int d[3], t[3];
	d[0] = -cam[0]; d[1] = -cam[1]; d[2] = -cam[2];
	m_vec(&Rc, d, t);
	m.t[0] = t[0]; m.t[1] = t[1]; m.t[2] = t[2];
	gte_SetRotMatrix(&m);
	gte_SetTransMatrix(&m);
	for (i = 0; i < nwq; i++) {
		const WQ *q = &wq[i];
		POLY_FT4 *p;
		int u0, v0, u1, v1, sh, f, bias;
		if (cutaway >= 0 && q->bld == cutaway && (q->type == T_OUT || q->type == T_ROOF)) continue;
		if (iabs(q->cx - fx) + iabs(q->cz - fz) > 2900) continue;
		gte_ldv3(&q->v[0], &q->v[1], &q->v[2]);
		gte_rtpt();
		gte_nclip();
		gte_stopz(&nc);
		if (nc <= 0) continue;
		gte_stsz3(&z0, &z1, &z2);
		{ int nearz = (q->type == T_GND) ? 260 : 120; if (z0 < nearz || z1 < nearz || z2 < nearz) continue; }
		if (nextpri + sizeof(POLY_FT4) > pri_end) return;
		p = (POLY_FT4 *)nextpri;
		gte_stsxy0(&p->x0);
		gte_stsxy1(&p->x1);
		gte_stsxy2(&p->x2);
		gte_ldv0(&q->v[3]);
		gte_rtps();
		gte_stsxy(&p->x3);
		gte_stsz(&z3);
		if (z3 < ((q->type == T_GND) ? 260 : 120)) continue;
		if (iabs(p->x0) >= 1000 || iabs(p->x1) >= 1000 || iabs(p->x2) >= 1000 || iabs(p->x3) >= 1000 ||
		    iabs(p->y0) >= 1000 || iabs(p->y1) >= 1000 || iabs(p->y2) >= 1000 || iabs(p->y3) >= 1000) continue;
		gte_avsz4();
		gte_stotz(&otz);
		bias = (q->type == T_GND) ? 28 : 0;
		otz = (otz >> 2) + bias;
		if (otz <= 0) continue;
		if (otz >= OT_LEN) otz = OT_LEN - 1;
		if (iabs(p->x0 - p->x3) > 700 || iabs(p->y0 - p->y3) > 500 || iabs(p->x1 - p->x2) > 700) continue;
		f = fogtab[otz > OT_LEN - 1 ? OT_LEN - 1 : otz];
		sh = (q->shade * f) >> 8;
		setPolyFT4(p);
		p->r0 = sh; p->g0 = sh; p->b0 = sh;
		u0 = (q->tile & 7) * 32; v0 = (q->tile >> 3) * 32; u1 = u0 + 31; v1 = v0 + 31;
		if (q->flip) { int tmp = u0; u0 = u1; u1 = tmp; }
		p->u0 = u0; p->v0 = v0; p->u1 = u1; p->v1 = v0;
		p->u2 = u0; p->v2 = v1; p->u3 = u1; p->v3 = v1;
		p->clut = clut_city[q->clut]; p->tpage = tp_city;
		addPrim(&db[dba].ot[otz], p);
		nextpri += sizeof(POLY_FT4);
	}
}

static void draw_shadow(int x, int y, int z, int r) {
	int sx0, sy0, sx1, sy1, sx2, sy2, sx3, sy3, zz;
	int gy = 0;
	if (y < -400) gy = y;        /* ombra sul tetto non gestita: la proiettiamo al suolo reale piu' vicino */
	(void)gy;
	zz = proj(x - r, 0, z + r, &sx0, &sy0);
	if (!zz) return;
	if (!proj(x + r, 0, z + r, &sx1, &sy1)) return;
	if (!proj(x - r, 0, z - r, &sx2, &sy2)) return;
	if (!proj(x + r, 0, z - r, &sx3, &sy3)) return;
	{
		NEWPRIM(POLY_F4, p);
		setPolyF4(p);
		p->r0 = 2; p->g0 = 2; p->b0 = 6;
		/* rombo: alto, destra, sinistra, basso */
		p->x0 = (sx0 + sx1) >> 1; p->y0 = (sy0 + sy1) >> 1;
		p->x1 = (sx1 + sx3) >> 1; p->y1 = (sy1 + sy3) >> 1;
		p->x2 = (sx0 + sx2) >> 1; p->y2 = (sy0 + sy2) >> 1;
		p->x3 = (sx2 + sx3) >> 1; p->y3 = (sy2 + sy3) >> 1;
		{
			int o = (zz >> 2) + 18;
			if (o >= OT_LEN) o = OT_LEN - 1;
			addPrim(&db[dba].ot[o], p);
		}
	}
}

static void draw_diamond(int wx, int wy, int wz, int size, int r, int g, int b, int slotbias) {
	int sx, sy, z = proj(wx, wy, wz, &sx, &sy), s, o;
	if (!z) return;
	s = (size * PROJ_H) / z;
	if (s < 2) s = 2;
	if (s > 60) s = 60;
	{
		NEWPRIM(POLY_F4, p);
		setPolyF4(p);
		p->r0 = r; p->g0 = g; p->b0 = b;
		p->x0 = sx; p->y0 = sy - s; p->x1 = sx + s; p->y1 = sy;
		p->x2 = sx - s; p->y2 = sy; p->x3 = sx; p->y3 = sy + s;
		o = (z >> 2) + slotbias;
		if (o < 1) o = 1;
		if (o >= OT_LEN) o = OT_LEN - 1;
		addPrim(&db[dba].ot[o], p);
	}
}

static void draw_pillar(int wx, int wy, int wz, int hgt, int r, int g, int b) {
	int sx0, sy0, sx1, sy1, z0 = proj(wx, wy, wz, &sx0, &sy0), z1 = proj(wx, wy - hgt, wz, &sx1, &sy1), w, o;
	if (!z0 || !z1) return;
	w = (22 * PROJ_H) / z0;
	if (w < 1) w = 1;
	{
		NEWPRIM(POLY_F4, p);
		setPolyF4(p);
		p->r0 = r; p->g0 = g; p->b0 = b;
		p->x0 = sx1 - w; p->y0 = sy1; p->x1 = sx1 + w; p->y1 = sy1;
		p->x2 = sx0 - w; p->y2 = sy0; p->x3 = sx0 + w; p->y3 = sy0;
		o = (z0 >> 2) - 4;
		if (o < 1) o = 1;
		if (o >= OT_LEN) o = OT_LEN - 1;
		addPrim(&db[dba].ot[o], p);
	}
}

static void draw_rope(int x0, int y0, int z0, int x1, int y1, int z1) {
	int sx0, sy0, sx1, sy1, za, zb, o;
	za = proj(x0, y0, z0, &sx0, &sy0);
	zb = proj(x1, y1, z1, &sx1, &sy1);
	if (!za || !zb) return;
	{
		NEWPRIM(LINE_F2, p);
		setLineF2(p);
		p->r0 = 250; p->g0 = 210; p->b0 = 90;
		p->x0 = sx0; p->y0 = sy0; p->x1 = sx1; p->y1 = sy1;
		o = (imin(za, zb) >> 2) - 6;
		if (o < 1) o = 1;
		addPrim(&db[dba].ot[o], p);
	}
}

static void hud_tile(int x, int y, int w, int h, int r, int g, int b) {
	NEWPRIM(TILE, t);
	setTile(t);
	t->r0 = r; t->g0 = g; t->b0 = b;
	t->x0 = x; t->y0 = y; t->w = w; t->h = h;
	addPrim(&db[dba].ot[0], t);
}

static void draw_text(void) {
	int dist, tx, tz, sy_;
	if (gstate == G_TITLE) {
		FntPrint(font0, "\n\n        N I G H T   S E N T I N E L\n");
		FntPrint(font0, "         demo tecnica - Vesper City\n");
		FntPrint(font1, " D-pad/stick muovi   X salta (tieni = planata)\n");
		FntPrint(font1, " Quadrato pugno  Triangolo calcio  R1 impulso\n");
		FntPrint(font1, " Cerchio rotolata  R2 rampino  L1 aggancia  L2 cambia\n");
		FntPrint(font1, "\n            PREMI START\n");
	} else if (gstate == G_WIN) {
		FntPrint(font0, "\n\n       MISSIONE COMPLETATA\n");
		FntPrint(font0, "       NIGHT SENTINEL WILL RETURN.\n");
		FntPrint(font1, "\n          START per rigiocare\n");
	} else {
		FntPrint(font0, "\n       MISSIONE: PRIMO TURNO\n");
		if (stage == 0) {
			tx = BEACON_X - pl.x; tz = BEACON_Z - pl.z; dist = isqrt(tx * tx + tz * tz) / 10;
			FntPrint(font0, " Sali sul tetto del magazzino (R2 rampino)\n");
			FntPrint(font0, " distanza %d\n", dist);
		} else if (stage == 1) {
			FntPrint(font0, " Un mercenario Helios ti ha visto: sconfiggilo\n");
		} else if (stage == 2) {
			tx = CORE_X - pl.x; tz = CORE_Z - pl.z; dist = isqrt(tx * tx + tz * tz) / 10;
			FntPrint(font0, " Recupera il nucleo dati nell'edificio blu\n");
			FntPrint(font0, " distanza %d\n", dist);
		}
		if (pl.dead) FntPrint(font1, "\n\n         SEI CADUTO...\n");
		else if (stage == 1 && hint_t > 0) FntPrint(font1, "\n\n\n   L1 aggancia il nemico   L2 cambia bersaglio\n");
		else if (stage == 0 && frame_no < 360) FntPrint(font1, "\n\n\n   R2 vicino a un bordo del tetto = rampino\n");
		else if (inside_bld(pl.x, pl.y, pl.z) == 1) FntPrint(font1, "\n\n\n   console di ricarica: sei al sicuro\n");
	}
	(void)sy_;
	FntFlush(font0);
	FntFlush(font1);
}

/* ------------------------------------------------------------------ */
/* personaggi: disegno, HUD e scelta delle pose                        */
/* ------------------------------------------------------------------ */
static void draw_char(uint16_t clut, int flash, int sc, int px, int py, int pz, int cw) {
	int b;
	for (b = 0; b < NB; b++) {
		int f, f1 = bone_f0[b + 1], i, j;
		MATRIX M;
		int dd[3], t[3];
		if (bone_f0[b] == f1) continue;
		m_mul(&Rc, &bR[b], &M);
		if (sc != 4096)
			for (i = 0; i < 3; i++) for (j = 0; j < 3; j++) M.m[i][j] = (int16_t)((M.m[i][j] * sc) >> 12);
		if ((b == B_CAPE1 || b == B_CAPE2) && cw != 4096)       /* mantello piu' largo (planata) */
			for (i = 0; i < 3; i++) M.m[i][0] = (int16_t)((M.m[i][0] * cw) >> 12);
		dd[0] = px + (((bJ[b][0] - px) * sc) >> 12) - cam[0];
		dd[1] = py + (((bJ[b][1] - py) * sc) >> 12) - cam[1];
		dd[2] = pz + (((bJ[b][2] - pz) * sc) >> 12) - cam[2];
		m_vec(&Rc, dd, t);
		M.t[0] = t[0]; M.t[1] = t[1]; M.t[2] = t[2];
		gte_SetRotMatrix(&M);
		gte_SetTransMatrix(&M);
		for (f = bone_f0[b]; f < f1; f++) {
			const MFACE *F = &mfaces[f];
			POLY_FT4 *p;
			int nc, otz, sh;
			gte_ldv3(&mverts[F->i[0]], &mverts[F->i[1]], &mverts[F->i[2]]);
			gte_rtpt();
			gte_nclip();
			gte_stopz(&nc);
			if (nc <= 0) continue;
			if (nextpri + sizeof(POLY_FT4) > pri_end) return;
			p = (POLY_FT4 *)nextpri;
			gte_stsxy0(&p->x0);
			gte_stsxy1(&p->x1);
			gte_stsxy2(&p->x2);
			gte_ldv0(&mverts[F->i[3]]);
			gte_rtps();
			gte_stsxy(&p->x3);
			gte_avsz4();
			gte_stotz(&otz);
			otz >>= 2;
			if (otz <= 0 || otz >= OT_LEN) continue;
			sh = (F->shade * fogtab[otz]) >> 8;
			if (flash) sh = imin(255, sh + 90);
			setPolyFT4(p);
			p->r0 = sh; p->g0 = sh; p->b0 = sh;
			p->u0 = F->u[0]; p->v0 = F->v[0]; p->u1 = F->u[1]; p->v1 = F->v[1];
			p->u2 = F->u[2]; p->v2 = F->v[2]; p->u3 = F->u[3]; p->v3 = F->v[3];
			p->clut = clut; p->tpage = tp_chr;
			addPrim(&db[dba].ot[otz], p);
			nextpri += sizeof(POLY_FT4);
		}
	}
}

static void scr_line(int x0, int y0, int x1, int y1, int r, int g, int b) {
	NEWPRIM(LINE_F2, p);
	setLineF2(p);
	p->r0 = r; p->g0 = g; p->b0 = b;
	p->x0 = x0; p->y0 = y0; p->x1 = x1; p->y1 = y1;
	addPrim(&db[dba].ot[1], p);
}

static void draw_hud(void) {
	int i;
	hud_tile(10, 22, (pl.en * 90) / 100, 5, 40, 220, 255);
	hud_tile(8, 20, 94, 9, 0, 8, 24);
	hud_tile(10, 10, (pl.hp * 90) / 100, 5, 230, 50, 60);
	hud_tile(8, 8, 94, 9, 0, 8, 24);
	if (gstate == G_PLAY) {
		for (i = 0; i < nen; i++) if (en[i].alive && en[i].state != E_DEAD) {
			int sx, sy;
			if (proj(en[i].x, en[i].type == T_BRUTE ? -285 : -250, en[i].z, &sx, &sy)) {
				int w = en[i].type == T_BRUTE ? 40 : 30;
				hud_tile(sx - w / 2 + 1, sy, (en[i].hp * (w - 2)) / en[i].hpmax, 3, en[i].type == T_BRUTE ? 255 : 255, en[i].type == T_BRUTE ? 80 : 190, 40);
				hud_tile(sx - w / 2, sy - 1, w, 5, 0, 0, 0);
			}
		}
	}
	if (hitflash > 0) hitflash--;
}

/* ---- fasi degli attacchi del giocatore: finche' t < fine si usa quella posa ---- */
typedef struct { uint8_t t, pose, rate; } PHASE;
static const PHASE ph_jab[]  = { {  3, L_JAB_W,  150 }, {  8, L_JAB_H,  245 }, { 99, L_GUARD, 90 } };
static const PHASE ph_hook[] = { {  3, L_HOOK_W, 150 }, {  9, L_HOOK_H, 245 }, { 99, L_GUARD, 90 } };
static const PHASE ph_spin[] = { {  4, L_SPIN_W, 150 }, { 13, L_SPIN_H, 230 }, { 99, L_GUARD, 80 } };
static const PHASE ph_kick[] = { {  5, L_KICK_W, 150 }, { 13, L_KICK_H, 245 }, { 99, L_GUARD, 80 } };
static const PHASE ph_pul[]  = { {  6, L_PUL_W,  150 }, { 11, L_PUL_H,  250 }, { 99, L_GUARD, 80 } };

static int look_at(int fromx, int fromz, int face, int tx, int tz, int maxdeg8) {
	int d = angdiff(iatan2(tx - fromx, tz - fromz), face);
	return iclamp((d * 45) >> 6, -maxdeg8, maxdeg8);
}

/* spostamento di lato/indietro mentre si e' agganciati: passi in guardia */
static void strafe_pose(POSE *p, int ph, int rel, int amp) {
	int f = cs(rel), sd = sn(rel), s = sn(ph);
	int A = (44 * 8 * amp) >> 8, SA = (64 * 8 * amp) >> 8;
	int sw = (s * SA) >> 12, fw = (((s * A) >> 12) * f) >> 12;
	guard_pose(p);
	p->a[B_THIGH_L][0] += (int16_t)fw;  p->a[B_THIGH_R][0] -= (int16_t)fw;
	p->a[B_THIGH_L][2] += (int16_t)(-((sw * sd) >> 12));
	p->a[B_THIGH_R][2] += (int16_t)((sw * sd) >> 12);
	p->a[B_SHIN_L][0] -= (int16_t)((imax(0, cs(ph - 300)) * 480) >> 12);
	p->a[B_SHIN_R][0] -= (int16_t)((imax(0, cs(ph + 2048 - 300)) * 480) >> 12);
	p->root[1] += (int16_t)((iabs(s) * 10) >> 12);
	p->a[B_TORSO][1] = (int16_t)(-((s * 36) >> 12));
}

static void pose_player_target(POSE *t, int *rate) {
	int tm = frame_no, k, a;
	*rate = 150;
	if (!pl.dodge && pl.an.cur.a[B_PELVIS][0] < -1100) pl.an.cur.a[B_PELVIS][0] += 2880;   /* fine rotolata: niente giro all'indietro */
	if (pl.dead) {
		if (pl.dead < 11) { *t = lib[L_KNEEL]; *rate = 130; } else { *t = lib[L_DEAD]; *rate = 60; }
		return;
	}
	if (pl.hurt > 0) { *t = lib[L_HURT]; *rate = 200; return; }
	if (pl.gr) {
		int dx = anc[pl.gr_a].x - pl.x, dz = anc[pl.gr_a].z - pl.z, up = -(anc[pl.gr_a].y - (pl.y - 190));
		int hz = isqrt(dx * dx + dz * dz);
		int el = (iatan2(up, hz < 1 ? 1 : hz) & 4095);
		if (el > 2048) el -= 4096;
		*t = lib[L_GRAP];
		t->a[B_ARM_R][0] = (int16_t)(90 * 8 + ((el * 45) >> 6));    /* 90 gradi + elevazione */
		*rate = 140; return;
	}
	if (pl.mantle > 0) { *t = lib[L_MANT]; *rate = 130; return; }
	if (pl.atk == A_SPIN) {
		int tt = pl.atkt;
		if (tt < 4) { *t = lib[L_SPIN_W]; *rate = 160; return; }
		if (tt <= 16) {                                  /* giro completo: da +50 a -360 gradi, il calcio arriva di fronte */
			int u = iclamp(((tt - 4) * 256) / 10, 0, 256);
			int e = (u * u) >> 8;                         /* accelera */
			int mixv = iclamp((tt - 7) * 43, 0, 256);     /* gamba: raccolta -> tesa */
			pose_mix(t, &lib[L_SPIN_C], &lib[L_SPIN_H], mixv);
			t->a[B_PELVIS][1] = (int16_t)((50 * 8) - ((410 * 8 * e) >> 8));
			*rate = 255; return;
		}
		if (pl.an.cur.a[B_PELVIS][1] < -1000) pl.an.cur.a[B_PELVIS][1] += 2880;   /* 360 gradi = posizione di partenza */
		*t = lib[L_GUARD]; *rate = 70; return;
	}
	if (pl.atk) {
		const PHASE *ph = pl.atk == A_JAB ? ph_jab : pl.atk == A_HOOK ? ph_hook : pl.atk == A_KICK ? ph_kick : ph_pul;
		for (k = 0; pl.atkt >= ph[k].t; k++) ;
		*t = lib[ph[k].pose]; *rate = ph[k].rate;
		return;
	}
	if (pl.dodge > 0) {
		a = 14 - pl.dodge;                               /* 0..13 */
		if (a < 2) { *t = lib[L_ROLL_IN]; *rate = 230; return; }          /* tuffo in avanti */
		if (a >= 12) {                                   /* ci si rialza */
			if (pl.an.cur.a[B_PELVIS][0] < -1100) pl.an.cur.a[B_PELVIS][0] += 2880;
			*t = lib[L_LAND]; *rate = 200; return;
		}
		*t = lib[L_ROLL];
		{ int u = ((a - 2) * 256) / 9;                   /* 360 gradi in 10 fotogrammi, con partenza e arrivo dolci */
		  u = iclamp(u, 0, 256);
		  u = (((u * u) >> 8) * (768 - 2 * u)) >> 8;
		  t->a[B_PELVIS][0] = (int16_t)(-((2880 * u) >> 8)); }
		*rate = 255; return;
	}
	if (pl.jsq > 0) { *t = lib[L_JSQ]; *rate = 210; return; }
	if (!pl.grounded) {
		if (pl.glide) {
			*t = lib[L_GLIDE];
			t->a[B_PELVIS][2] = (int16_t)iclamp(-pl.turn * 14, -360, 360);
			t->a[B_PELVIS][0] -= (int16_t)iclamp(pl.vy * 6, 0, 120);
			*rate = 95;
		} else if (pl.vy < -6) { *t = lib[L_UP]; *rate = 140; }
		else { pose_mix(t, &lib[L_UP], &lib[L_FALL], iclamp((pl.vy + 6) * 14, 0, 256)); *rate = 120; }
		return;
	}
	if (pl.landt > 0) { *t = lib[L_LAND]; *rate = 200; return; }
	if (pl.moving) {
		int amp = iclamp((pl.spd * 256) / 16, 90, 256);
		if (pl.lock >= 0 && iabs(pl.rel) > 700) {
			strafe_pose(t, pl.ph, pl.rel, amp);
			t->a[B_HEAD][1] = (int16_t)look_at(pl.x, pl.z, pl.face, en[pl.lock].x, en[pl.lock].z, 320);
		} else gait_pose(t, pl.ph, amp, &G_RUN);
		*rate = 170; return;
	}
	k = pl.lock >= 0 ? pl.lock : nearest_enemy(700);
	if (k >= 0) {
		*t = lib[L_GUARD];
		t->root[1] += (int16_t)((sn(tm * 150) * 5) >> 12);
		t->root[0] += (int16_t)((sn(tm * 75) * 7) >> 12);
		t->a[B_THIGH_L][0] += (int16_t)((sn(tm * 150) * 40) >> 12);
		t->a[B_THIGH_R][0] -= (int16_t)((sn(tm * 150) * 40) >> 12);
		t->a[B_HEAD][1] = (int16_t)look_at(pl.x, pl.z, pl.face, en[k].x, en[k].z, 320);
		*rate = 100;
	} else {
		*t = lib[L_IDLE];
		t->a[B_TORSO][0] += (int16_t)((sn(tm * 70) * 22) >> 12);
		t->a[B_HEAD][0]  -= (int16_t)((sn(tm * 70) * 14) >> 12);
		t->a[B_ARM_L][0] += (int16_t)((sn(tm * 70 + 600) * 40) >> 12);
		t->a[B_ARM_R][0] += (int16_t)((sn(tm * 70 + 600) * 40) >> 12);
		t->a[B_PELVIS][2] = (int16_t)((sn(tm * 38) * 22) >> 12);
		t->a[B_TORSO][2] = (int16_t)(-((sn(tm * 38) * 16) >> 12));
		t->root[0] = (int16_t)((sn(tm * 38) * 6) >> 12);
		t->a[B_THIGH_L][0] += (int16_t)((sn(tm * 38) * 20) >> 12);
		*rate = 55;
	}
}

static void pose_enemy_target(EN *e, POSE *t, int *rate) {
	int tm = frame_no + e->x, s;
	*rate = 120;
	if (e->type == T_BRUTE) {
		switch (e->state) {
		case E_DEAD:
			if (e->t < 15) { *t = lib[L_BR_KNEEL]; *rate = 100; } else { *t = lib[L_BR_FALL]; *rate = 55; }
			return;
		case E_HURT: *t = lib[L_BR_HURT]; *rate = 190; return;
		case E_WIND: *t = lib[e->atype ? L_BR_HOOK_W : L_BR_SLAM_W]; *rate = e->atype ? 120 : 62; return;
		case E_STRIKE: *t = lib[e->atype ? L_BR_HOOK_H : L_BR_SLAM_H]; *rate = 250; return;
		case E_RECOV:
			if (e->t < (e->atype ? 5 : 13)) { *t = lib[e->atype ? L_BR_HOOK_H : L_BR_SLAM_H]; *rate = 200; }
			else { *t = lib[L_BR_IDLE]; *rate = 40; }
			return;
		case E_CHASE:
			gait_pose(t, e->ph, 256, &G_BRUTE); *rate = 150; return;
		default:
			*t = lib[L_BR_IDLE];
			t->a[B_TORSO][0] += (int16_t)((sn(tm * 45) * 30) >> 12);
			t->a[B_TORSO][2] = (int16_t)((sn(tm * 30) * 70) >> 12);
			t->a[B_ARM_L][2] += (int16_t)((sn(tm * 60) * 36) >> 12);
			t->a[B_ARM_R][2] -= (int16_t)((sn(tm * 60) * 36) >> 12);
			t->a[B_FORE_L][0] += (int16_t)((sn(tm * 45 + 700) * 60) >> 12);
			t->a[B_FORE_R][0] += (int16_t)((sn(tm * 45 + 700) * 60) >> 12);
			t->a[B_HEAD][1] = (int16_t)((sn(tm * 25) * 240) >> 12);
			t->root[0] = (int16_t)((sn(tm * 30) * 9) >> 12);
			*rate = 50; return;
		}
	}
	/* scattante */
	switch (e->state) {
	case E_DEAD: *t = lib[L_ST_DEAD]; *rate = (e->t < 6) ? 180 : 90; return;
	case E_HURT: *t = lib[L_ST_HURT]; *rate = 220; return;
	case E_WIND: *t = lib[L_ST_COIL]; *rate = 210; return;
	case E_STRIKE: *t = lib[L_ST_KICK]; *rate = 255; return;
	case E_RECOV:
		if (e->t < 14) { *t = lib[L_ST_SLIDE]; *rate = 150; } else { *t = lib[L_ST_IDLE]; *rate = 60; }
		return;
	case E_CHASE: gait_pose(t, e->ph, 256, &G_STRK); *rate = 170; return;
	case E_CIRCLE:
		s = sn(e->ph);
		*t = lib[L_ST_IDLE];
		t->a[B_THIGH_L][0] += (int16_t)((s * 150) >> 12); t->a[B_THIGH_R][0] -= (int16_t)((s * 150) >> 12);
		t->a[B_THIGH_L][2] += (int16_t)((s * 110) >> 12); t->a[B_THIGH_R][2] += (int16_t)((s * 110) >> 12);
		t->a[B_SHIN_L][0] -= (int16_t)(imax(0, s) * 90 >> 12); t->a[B_SHIN_R][0] -= (int16_t)(imax(0, -s) * 90 >> 12);
		t->root[1] += (int16_t)(8 + ((iabs(s) * 10) >> 12));
		t->a[B_PELVIS][1] = (int16_t)(e->dir * 90);
		t->a[B_TORSO][1] = (int16_t)(-e->dir * 90);
		*rate = 150; return;
	default:
		*t = lib[L_ST_IDLE];
		t->root[1] += (int16_t)(6 + ((sn(tm * 230) * 8) >> 12));
		t->root[0] = (int16_t)((sn(tm * 115) * 10) >> 12);
		t->a[B_THIGH_L][0] += (int16_t)((sn(tm * 230) * 70) >> 12);
		t->a[B_THIGH_R][0] -= (int16_t)((sn(tm * 230) * 70) >> 12);
		t->a[B_ARM_L][0] += (int16_t)((sn(tm * 230 + 500) * 60) >> 12);
		t->a[B_ARM_R][0] += (int16_t)((sn(tm * 230 + 1500) * 60) >> 12);
		t->a[B_HEAD][1] = (int16_t)look_at(e->x, e->z, e->face, pl.x, pl.z, 320);
		*rate = 90; return;
	}
}

static void anim_step_player(void) {
	POSE t; int rate;
	pose_player_target(&t, &rate);
	pose_ease(&pl.an.cur, &t, rate);
	anim_cape(&pl.an, pl.spd, pl.vy, pl.turn, pl.glide, pl.atk == A_SPIN && pl.atkt >= 4 && pl.atkt <= 16);
}
static void anim_step_enemy(EN *e) {
	POSE t; int rate;
	if (!e->alive) return;
	pose_enemy_target(e, &t, &rate);
	pose_ease(&e->an.cur, &t, rate);
	anim_cape(&e->an, e->spd, 0, e->turn, 0, 0);
}

/* ------------------------------------------------------------------ */
/* init / frame / display                                              */
/* ------------------------------------------------------------------ */
static void load_vram(void) {
	RECT r;
	int i;
	/* personaggio */
	setRECT(&r, CHR_X, 0, 128, 256);   LoadImage(&r, tex_data);  DrawSync(0);
	setRECT(&r, CHR_X, CLUT_Y, 256, 1); LoadImage(&r, (const uint32_t *)clut_data); DrawSync(0);
	/* palette nemico: scambio R<->B, un po' scura */
	for (i = 0; i < 256; i++) {
		unsigned c = clut_data[i];
		unsigned rr = c & 31, gg = (c >> 5) & 31, bb = (c >> 10) & 31, o;
		if (!c) { en_clut_data[i] = 0; continue; }
		o = (bb * 3 / 4) | (((gg * 3) / 4) << 5) | (rr << 10);
		if (!(o & 0x7fff)) o = 0x0421;
		en_clut_data[i] = (uint16_t)o;
	}
	setRECT(&r, CHR_X, CLUT_Y + 1, 256, 1); LoadImage(&r, (const uint32_t *)en_clut_data); DrawSync(0);
	/* palette scattante: scambio R<->G (mantello verde) */
	for (i = 0; i < 256; i++) {
		unsigned c = clut_data[i];
		unsigned rr = c & 31, gg = (c >> 5) & 31, bb = (c >> 10) & 31, o;
		if (!c) { en2_clut_data[i] = 0; continue; }
		o = gg | (rr << 5) | (bb << 10);
		if (!(o & 0x7fff)) o = 0x0421;
		en2_clut_data[i] = (uint16_t)o;
	}
	setRECT(&r, CHR_X, CLUT_Y + 3, 256, 1); LoadImage(&r, (const uint32_t *)en2_clut_data); DrawSync(0);
	/* citta' */
	setRECT(&r, CTY_X, 0, 64, 128);     LoadImage(&r, city_tex);  DrawSync(0);
	setRECT(&r, CHR_X, CLUT_Y + 2, 64, 1); LoadImage(&r, (const uint32_t *)city_clut); DrawSync(0);
	tp_chr  = getTPage(1, 0, CHR_X, 0);
	tp_city = getTPage(0, 0, CTY_X, 0);
	clut_pl = getClut(CHR_X, CLUT_Y);
	clut_en = getClut(CHR_X, CLUT_Y + 1);
	clut_en2 = getClut(CHR_X, CLUT_Y + 3);
	for (i = 0; i < 4; i++) clut_city[i] = getClut(CHR_X + 16 * i, CLUT_Y + 2);
}

static void game_init(void) {
	int i;
	ResetGraph(0);
	SetDefDispEnv(&db[0].disp, 0, 0, SCR_W, SCR_H);
	SetDefDrawEnv(&db[0].draw, SCR_W, 0, SCR_W, SCR_H);
	SetDefDispEnv(&db[1].disp, SCR_W, 0, SCR_W, SCR_H);
	SetDefDrawEnv(&db[1].draw, 0, 0, SCR_W, SCR_H);
	for (i = 0; i < 2; i++) {
		setRGB0(&db[i].draw, 10, 14, 34);
		db[i].draw.isbg = 1;
		db[i].draw.dtd = 1;
	}
	PutDrawEnv(&db[0].draw);
	ClearOTagR(db[0].ot, OT_LEN);
	ClearOTagR(db[1].ot, OT_LEN);
	dba = 0;
	nextpri = db[0].pri;
	pri_end = db[0].pri + PRI_LEN - 64;

	InitGeom();
	gte_SetGeomOffset(CX, CY);
	gte_SetGeomScreen(PROJ_H);
	GTE_SET_ZSF();

	InitPAD(&pad_buff[0][0], 34, &pad_buff[1][0], 34);
	StartPAD();
	ChangeClearPAD(0);

	FntLoad(960, 0);
	font0 = FntOpen(8, 30, 304, 70, 0, 320);
	font1 = FntOpen(8, 170, 304, 64, 0, 320);

	load_vram();
	for (i = 0; i < OT_LEN; i++) {
		int z = i * 4, f;
		if (z < 1500) f = 256; else if (z > 3700) f = 0; else f = ((3700 - z) * 256) / 2200;
		fogtab[i] = (uint8_t)imin(255, f);
	}
	fogtab[0] = 255;
	init_poses();
	build_world();
	mission_reset();
	last_v = VSync(-1);
	SetDispMask(1);
}

static void display(void) {
	int v;
	DrawSync(0);
	VSync(0);
	v = VSync(-1);
	if (v - last_v < 2) { VSync(0); v = VSync(-1); }
	last_v = v;
	dba ^= 1;
	nextpri = db[dba].pri;
	pri_end = db[dba].pri + PRI_LEN - 64;
	ClearOTagR(db[dba].ot, OT_LEN);
	PutDrawEnv(&db[dba].draw);
	PutDispEnv(&db[dba].disp);
	DrawOTag(db[1 - dba].ot + (OT_LEN - 1));
}

static void game_frame(void) {
	POSE out;
	int i, cut, dist = 640, tx, tz;
	frame_no++;
	read_pad();

	if (gstate == G_TITLE) {
		cyaw += 14;
		player_reset();
		fx = pl.x; fz = pl.z; fy = pl.y;
		if (btn_new & PAD_START) { gstate = G_PLAY; mission_reset(); frame_no = 0; }
	} else if (gstate == G_WIN) {
		wintimer++;
		cyaw += 10;
		if ((btn_new & PAD_START) && wintimer > 30) { gstate = G_PLAY; mission_reset(); frame_no = 0; }
	} else if (host_freeze) {
		/* solo test su PC: logica ferma, si animano e disegnano le pose impostate a mano */
	} else if (hitstop > 0) {
		pend_new |= btn_new;
		hitstop--;                       /* impatto: la scena si ferma un istante */
	} else {
		btn_new |= pend_new; pend_new = 0;          /* i tasti premuti durante l'impatto non si perdono */
		update_player();
		for (i = 0; i < nen; i++) update_enemy(&en[i]);
		resolve_overlap();
		update_mission();
		if (hint_t > 0) hint_t--;
	}
	if (!(gstate == G_PLAY && hitstop > 0)) {
		anim_step_player();
		for (i = 0; i < nen; i++) anim_step_enemy(&en[i]);
	}

	/* camera morbida */
	tx = pl.x; tz = pl.z;
	if (gstate == G_PLAY && pl.lock >= 0 && en[pl.lock].alive) {     /* aggancio: inquadra giocatore e bersaglio */
		tx += (en[pl.lock].x - pl.x) / 3; tz += (en[pl.lock].z - pl.z) / 3; dist = 700;
	}
	fx += (tx - fx) >> 2; fz += (tz - fz) >> 2; fy += (pl.y - fy) >> 2;
	cut = inside_bld(pl.x, pl.y, pl.z);
	if (cut >= 0) dist = 520;
	cam_update(fx, fy - 130, fz, dist);
	if (shake > 0) { cam[1] += ((frame_no & 1) ? shake : -shake) * 2; shake--; }

	/* scena */
	draw_world(cut);

	/* giocatore */
	{
		int blink = (pl.dinv > 0 && (frame_no & 2) && !pl.dead);   /* lampeggia solo dopo un colpo subito, mai nella rotolata */
		anim_out(&pl.an, &out);
		if (!blink && !host_noplayer) {
			fk(&out, pl.face, pl.x, pl.y, pl.z);
			draw_char(clut_pl, hitflash > 0 && pl.hurt > 0, 4096, pl.x, pl.y, pl.z, pl.an.cw);
		}
	}
	draw_shadow(pl.x, pl.y, pl.z, 50);

	/* nemici */
	for (i = 0; i < nen; i++) {
		EN *e = &en[i];
		if (!e->alive) continue;
		if (iabs(e->x - fx) + iabs(e->z - fz) > 3000) continue;
		anim_out(&e->an, &out);
		fk(&out, e->face, e->x, e->y, e->z);
		draw_char(e->type == T_BRUTE ? clut_en : clut_en2, e->state == E_HURT && e->t < 3,
		          e->type == T_BRUTE ? 4915 : 3900, e->x, e->y, e->z, 4096);
		draw_shadow(e->x, e->y, e->z, e->type == T_BRUTE ? 62 : 46);
	}

	/* mirino sul bersaglio agganciato */
	if (gstate == G_PLAY && pl.lock >= 0 && en[pl.lock].alive) {
		int sx, sy, r = 9 + ((sn(frame_no * 200) + 4096) >> 10);
		if (proj(en[pl.lock].x, en[pl.lock].type == T_BRUTE ? -345 : -305, en[pl.lock].z, &sx, &sy)) {
			scr_line(sx, sy - r, sx + r, sy, 255, 60, 60);
			scr_line(sx + r, sy, sx, sy + r, 255, 60, 60);
			scr_line(sx, sy + r, sx - r, sy, 255, 60, 60);
			scr_line(sx - r, sy, sx, sy - r, 255, 60, 60);
			hud_tile(sx - 1, sy - 1, 3, 3, 255, 255, 255);
		}
	}

	/* impulsi, ancore, obiettivi */
	for (i = 0; i < 3; i++) if (pu[i].life) {
		draw_diamond(pu[i].x, pu[i].y, pu[i].z, 34, 90, 230, 255, -3);
		draw_diamond(pu[i].x, pu[i].y, pu[i].z, 16, 255, 255, 255, -5);
	}
	if (gstate == G_PLAY) {
		for (i = 0; i < nanc; i++) {
			int dx = anc[i].x - pl.x, dz = anc[i].z - pl.z;
			if (dx * dx + dz * dz < 1700 * 1700 && (frame_no & 8))
				draw_diamond(anc[i].x, anc[i].y, anc[i].z, 14, 250, 210, 90, 0);
		}
		if (pl.gr) {
			const ANCHOR *a = &anc[pl.gr_a];
			draw_rope(pl.x + 10, pl.y - 190, pl.z, a->x, a->y, a->z);
		}
		if (stage == 0) {
			int pulse = 160 + (sn(frame_no * 120) >> 6);
			draw_pillar(BEACON_X, BEACON_Y, BEACON_Z, 420, 30, pulse / 2, pulse);
		} else if (stage == 2) {
			int pulse = 160 + (sn(frame_no * 120) >> 6);
			draw_pillar(CORE_X, 0, CORE_Z, 300, pulse, pulse, 40);
			draw_diamond(CORE_X, -90 + (sn(frame_no * 100) >> 8), CORE_Z, 30, 255, 220, 60, -2);
		}
	}

	draw_hud();
	draw_text();
	display();
}

#ifndef HOST_TEST
int main(void) {
	game_init();
	for (;;) game_frame();
	return 0;
}
#endif

