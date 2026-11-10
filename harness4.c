#define HOST_TEST
static int host_freeze, host_noplayer;
#include "../main.c"
#include <stdlib.h>
extern int host_maxprims;
static void setpad(unsigned pressed, int lx, int ly) {
	PADTYPE *p = (PADTYPE *)pad_buff[0];
	p->stat = 0; p->len = 1; p->type = 7; p->btn = (~pressed) & 0xffff; p->rs_x = 128; p->rs_y = 128; p->ls_x = 128 + lx; p->ls_y = 128 + ly;
}
static void run(int n, unsigned pressed, int lx, int ly) { int i; for (i = 0; i < n; i++) { setpad(pressed, lx, ly); game_frame(); } }
static void snap(const char *name) {
	char path[200]; FILE *f;
	snprintf(path, sizeof path, "out/%s.ppm", name);
	f = fopen(path, "wb"); fprintf(f, "P6\n320 240\n255\n"); fwrite(fb, 1, sizeof fb, f); fclose(f);
}
static int mind(int i) { int dx = pl.x - en[i].x, dz = pl.z - en[i].z; return isqrt(dx * dx + dz * dz); }
static int visible_pixels(void) {   /* pixel "da personaggio" attorno al giocatore */
	int sx, sy, x, y, n = 0;
	if (!proj(pl.x, pl.y - 90, pl.z, &sx, &sy)) return -1;
	for (y = sy - 40; y < sy + 40; y++) for (x = sx - 40; x < sx + 40; x++) {
		if (x < 0 || y < 0 || x >= 320 || y >= 240) continue;
		if (fb[y][x][0] > 110 || fb[y][x][2] > 110 || fb[y][x][1] > 110) n++;
	}
	return n;
}
int main(void) {
	int i, k, fails = 0;
	game_init();
	run(2, 0, 0, 0); run(1, PAD_START, 0, 0);
	/* ---------- lock-on ---------- */
	player_reset(); stage = 1; gstate = G_PLAY; nen = 0;
	enemy_spawn(0, T_BRUTE, 300, 500); enemy_spawn(1, T_STRIKER, -400, 650);
	en[0].state = E_IDLE; en[1].state = E_IDLE; pl.x = 0; pl.z = 0; cyaw = 0; fx = 0; fz = 0;
	run(10, 0, 0, 0); snap("L0_prima");
	run(1, PAD_L1, 0, 0); run(20, 0, 0, 0);
	printf("lock dopo L1 = %d (atteso 0 o 1)  cyaw=%d\n", pl.lock, cyaw); snap("L1_agganciato");
	{ int first = pl.lock; run(1, PAD_L2, 0, 0); run(25, 0, 0, 0); printf("lock dopo L2 = %d (era %d)\n", pl.lock, first); if (pl.lock == first) fails++; snap("L2_cambiato"); }
	/* strafe verso destra con aggancio */
	for (k = 0; k < 3; k++) { run(12, 0, 127, 0); if (k == 1) snap("L3_strafe"); }
	printf("rel=%d (strafe laterale atteso ~ +-1024)  spd=%d\n", pl.rel, pl.spd);
	run(1, PAD_L1, 0, 0); run(3, 0, 0, 0); printf("lock dopo secondo L1 = %d (atteso -1)\n", pl.lock); if (pl.lock != -1) fails++;
	/* ---------- compenetrazione ---------- */
	player_reset(); stage = 1; gstate = G_PLAY; nen = 0; pl.x = 0; pl.z = 0;
	enemy_spawn(0, T_BRUTE, 20, 10); en[0].state = E_IDLE;
	run(6, 0, 0, 0); printf("brute sovrapposto: distanza dopo 6 frame = %d (minimo 96)\n", mind(0)); if (mind(0) < 90) fails++;
	player_reset(); stage = 1; gstate = G_PLAY; nen = 0; pl.x = 0; pl.z = 0; pl.face = 0;
	enemy_spawn(0, T_STRIKER, 0, -40); en[0].state = E_IDLE; en[0].hp = 56;       /* alle spalle e quasi addosso */
	{ int hp0 = en[0].hp; run(1, PAD_SQUARE, 0, 0); run(10, 0, 0, 0); printf("jab con nemico addosso/alle spalle: hp %d -> %d (deve scendere)\n", hp0, en[0].hp); if (en[0].hp >= hp0) fails++; }
	/* combo completa su striker fermo davanti */
	player_reset(); stage = 1; gstate = G_PLAY; nen = 0; pl.x = 0; pl.z = 0; pl.face = 0;
	enemy_spawn(0, T_BRUTE, 0, 160); en[0].state = E_IDLE; en[0].hp = 100;
	{ int h0 = en[0].hp, f;                      /* bersaglio passivo: cool alto = non attacca */
	  for (f = 0; f < 70; f++) { unsigned b = (f == 0 || f == 6 || f == 19) ? PAD_SQUARE : 0; en[0].cool = 999; setpad(b, 0, 0); game_frame(); }
	  printf("combo su bruto passivo: hp %d -> %d (atteso circa 60)  dist=%d\n", h0, en[0].hp, mind(0)); if (en[0].hp > h0 - 36) fails++; }
	/* nemici attivi per un po': la distanza minima non scende mai sotto la soglia (a parte schivata) */
	player_reset(); stage = 1; gstate = G_PLAY; nen = 0; pl.x = 0; pl.z = 0;
	enemy_spawn(0, T_BRUTE, 0, 400); enemy_spawn(1, T_STRIKER, 200, 700);
	{ int worst = 9999, f; for (f = 0; f < 600; f++) { setpad((f % 30) < 3 ? PAD_SQUARE : 0, 0, (f % 80) < 40 ? -127 : 0); game_frame(); for (i = 0; i < nen; i++) if (en[i].alive && en[i].state != E_DEAD && pl.y > -160) worst = imin(worst, mind(i)); }
	  printf("distanza minima in 600 frame di combattimento = %d\n", worst); if (worst < 60) fails++; }
	/* ---------- rotolata: il personaggio non sparisce mai ---------- */
	player_reset(); stage = 5; gstate = G_PLAY; nen = 0; pl.x = 0; pl.z = 0; cyaw = 0; fx = 0; fz = 0;
	run(25, 0, 0, 0);
	{ int minpx = 99999, f; setpad(PAD_CIRCLE, 0, -127); game_frame(); for (f = 0; f < 20; f++) { setpad(0, 0, -127); game_frame(); { int v = visible_pixels(); if (v >= 0 && v < minpx) minpx = v; if (f == 4) snap("R1_roll"); if (f == 8) snap("R2_roll"); } }
	  printf("rotolata: pixel visibili minimi = %d  dinv=%d (atteso 0)\n", minpx, pl.dinv); if (minpx < 25 || pl.dinv != 0) fails++; }
	/* dopo un colpo subito il lampeggio resta */
	damage_player(5, 100, 100, 0); printf("dopo colpo: dinv=%d (atteso >0)\n", pl.dinv); if (pl.dinv <= 0) fails++;
	printf("%s: fails=%d maxprims=%d\n", fails ? "PROBLEMI" : "OK", fails, host_maxprims);
	return fails;
}
