#define HOST_TEST
static int host_freeze, host_noplayer;
#include "../main.c"
#include <stdlib.h>
#define TW 130
static uint8_t tiles[40][TW][TW][3];
static int ntile;
static void setpad(unsigned pressed, int lx, int ly) {
	PADTYPE *p = (PADTYPE *)pad_buff[0];
	p->stat = 0; p->len = 1; p->type = 7; p->btn = (~pressed) & 0xffff; p->rs_x = 128; p->rs_y = 128; p->ls_x = 128 + lx; p->ls_y = 128 + ly;
}
static void grab(int wx, int wy, int wz) {
	int sx, sy, x, y;
	proj(wx, wy, wz, &sx, &sy);
	for (y = 0; y < TW; y++) for (x = 0; x < TW; x++) {
		int X = sx - TW / 2 + x, Y = sy - TW / 2 + y;
		uint8_t *d = tiles[ntile][y][x];
		if (X < 0 || Y < 0 || X >= 320 || Y >= 240) { d[0] = d[1] = d[2] = 0; } else { d[0] = fb[Y][X][0]; d[1] = fb[Y][X][1]; d[2] = fb[Y][X][2]; }
	}
	ntile++;
}
static void sheet(const char *name) {
	char path[200]; FILE *f; int i, y, x;
	snprintf(path, sizeof path, "out/%s.ppm", name);
	f = fopen(path, "wb"); fprintf(f, "P6\n%d %d\n255\n", TW * ntile, TW);
	for (y = 0; y < TW; y++) for (i = 0; i < ntile; i++) for (x = 0; x < TW; x++) fwrite(tiles[i][y][x], 1, 3, f);
	fclose(f); ntile = 0;
}
/* camera laterale ravvicinata sul personaggio */
static void frame_on(int wx, int wz, int face) {
	cyaw = face + 1024; fx = wx; fz = wz; fy = 0;
	game_frame(); game_frame();
}
/* ---------- giocatore ---------- */
static void pl_setup(void) { host_freeze = 1; gstate = G_PLAY; mission_reset(); pl.x = 0; pl.z = 0; pl.face = 0; stage = 5; nen = 0; }
static void P(void) { grab(pl.x + ((sn(cyaw + 1024) * 0) >> 12), pl.y - 110, pl.z); }
int main(void) {
	int i, k, views[4] = {1024, 0, 512, 2048};
	game_init();
	pl_setup(); frame_on(0, 0, 0);
	/* planata: 4 viste */
	pl.grounded = 0; pl.glide = 1; pl.vy = 3; pl.y = -200; pl.spd = 21; pl.turn = 0;
	for (k = 0; k < 4; k++) {
		for (i = 0; i < 14; i++) { cyaw = views[k]; fx = 0; fz = 0; pl.y = -200; game_frame(); }
		grab(pl.x, pl.y - 100, pl.z);
	}
	pl.turn = 25;                                  /* in curva */
	for (k = 0; k < 2; k++) { for (i = 0; i < 10; i++) { cyaw = views[k ? 1 : 0]; pl.y = -200; game_frame(); } grab(pl.x, pl.y - 100, pl.z); }
	sheet("H_glide");
	/* planata con picchiata */
	pl.turn = 0; pl.vy = 14;
	for (k = 0; k < 2; k++) { for (i = 0; i < 10; i++) { cyaw = views[k ? 2 : 0]; pl.y = -200; game_frame(); } grab(pl.x, pl.y - 100, pl.z); }
	sheet("H2_dive");
	pl.glide = 0; pl.grounded = 1; pl.y = 0; pl.vy = 0; pl.spd = 0;
	for (i = 0; i < 30; i++) { cyaw = 1024; game_frame(); }
	/* calcio rotante */
	for (k = 0; k < 2; k++) {
		static const int ts[] = { 1, 3, 4, 6, 8, 10, 11, 12, 13, 14, 15, 16, 17, 18, 20, 25 };
		pl.atk = 0; pl.atkt = 0; for (i = 0; i < 12; i++) { cyaw = views[k == 0 ? 0 : 1]; game_frame(); }
		pl.atk = A_SPIN; pl.atkt = 0;
		for (i = 0; i < 16; i++) {
			while (pl.atkt < ts[i]) { pl.atkt++; cyaw = views[k == 0 ? 0 : 1]; game_frame(); }
			grab(pl.x, pl.y - 110, pl.z);
		}
		pl.atk = 0;
		sheet(k == 0 ? "I0_spin_side" : "I1_spin_back");
	}
	/* rotolata a terra */
	for (k = 0; k < 2; k++) {
		pl.atk = 0; pl.dodge = 0; for (i = 0; i < 20; i++) { cyaw = views[k == 0 ? 0 : 1]; game_frame(); }
		for (i = 14; i >= 1; i--) { pl.dodge = i; cyaw = views[k == 0 ? 0 : 1]; game_frame(); grab(pl.x, pl.y - 70, pl.z); }
		pl.dodge = 0; for (i = 0; i < 3; i++) { cyaw = views[k == 0 ? 0 : 1]; game_frame(); }
		grab(pl.x, pl.y - 100, pl.z);
		sheet(k == 0 ? "J0_roll_side" : "J1_roll_back");
	}
	printf("OK\n");
	return 0;
}
