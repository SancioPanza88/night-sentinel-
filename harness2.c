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
	int i, k;
	game_init();
	/* corsa: 8 fotogrammi di un ciclo */
	pl_setup(); frame_on(0, 0, 0);
	pl.moving = 1; pl.grounded = 1; pl.spd = 16;
	for (i = 0; i < 30; i++) { pl.ph += 244; pl.moving = 1; pl.spd = 16; cyaw = 1024; fx = 0; fz = 0; game_frame(); }
	for (i = 0; i < 8; i++) { for (k = 0; k < 2; k++) { pl.ph += 244; cyaw = 1024; game_frame(); } P(); }
	sheet("A_run");
	/* camminata */
	for (i = 0; i < 30; i++) { pl.ph += 120; pl.spd = 8; pl.moving = 1; cyaw = 1024; game_frame(); }
	for (i = 0; i < 8; i++) { for (k = 0; k < 2; k++) { pl.ph += 120; pl.spd = 8; cyaw = 1024; game_frame(); } P(); }
	sheet("B_walk");
	/* idle + guardia */
	pl.moving = 0; pl.spd = 0;
	for (i = 0; i < 60; i++) { cyaw = 1024; game_frame(); if (i % 8 == 7) P(); }
	sheet("C_idle");
	/* attacchi */
	{
		int atk[5] = { A_JAB, A_HOOK, A_SPIN, A_KICK, A_PULSE };
		int ts[5][6] = { {1, 3, 5, 7, 9, 12}, {1, 3, 5, 7, 9, 12}, {2, 5, 9, 12, 15, 19}, {2, 5, 8, 11, 14, 19}, {2, 5, 7, 9, 12, 15} };
		char nm[40]; int a;
		for (a = 0; a < 5; a++) {
			pl.atk = 0; pl.atkt = 0; pl.moving = 0;
			for (i = 0; i < 10; i++) { cyaw = 1024; game_frame(); }
			pl.atk = atk[a];
			for (i = 0; i < 6; i++) {
				while (pl.atkt < ts[a][i]) { pl.atkt++; cyaw = 1024; game_frame(); }
				P();
			}
			snprintf(nm, sizeof nm, "D%d_atk", a); sheet(nm);
			pl.atk = 0;
		}
	}
	/* schivata, salto, aria, planata, atterraggio */
	pl.atk = 0; for (i = 0; i < 20; i++) { cyaw = 1024; game_frame(); }
	for (i = 12; i > 0; i--) { pl.dodge = i; cyaw = 1024; game_frame(); if (i % 2 == 0 && i < 12) { P(); } }
	pl.dodge = 0; for (i = 0; i < 20; i++) { cyaw = 1024; game_frame(); }
	pl.jsq = 2; cyaw = 1024; game_frame(); game_frame(); P(); pl.jsq = 0;
	pl.grounded = 0; pl.vy = -30; pl.y = -60; game_frame(); game_frame(); P();
	pl.vy = -4; pl.y = -200; game_frame(); game_frame(); game_frame(); P();
	pl.vy = 20; pl.y = -120; game_frame(); game_frame(); game_frame(); P();
	pl.glide = 1; pl.vy = 3; for (i = 0; i < 6; i++) { pl.y = -200; game_frame(); } P();
	pl.glide = 0; pl.grounded = 1; pl.y = 0; pl.vy = 0; pl.landt = 5; for (i = 0; i < 3; i++) game_frame(); P();
	sheet("E_air");
	/* rampino, mantle, colpo subito, morte */
	pl.landt = 0; pl.y = -200; pl.grounded = 0; pl.gr = 1; pl.gr_a = 2; pl.gr_t = 5; pl.face = 1024;
	for (i = 0; i < 6; i++) { cyaw = pl.face + 1024; game_frame(); } fx = pl.x; grab(pl.x, pl.y - 110, pl.z);
	pl.gr = 0; pl.mantle = 10; for (i = 0; i < 6; i++) game_frame(); grab(pl.x, pl.y - 110, pl.z);
	pl.mantle = 0; pl.y = 0; pl.grounded = 1; pl.face = 0;
	pl.hurt = 10; for (i = 0; i < 4; i++) { cyaw = 1024; game_frame(); } P();
	pl.hurt = 0; pl.dead = 1; for (i = 0; i < 6; i++) { cyaw = 1024; game_frame(); } P();
	for (i = 0; i < 20; i++) { cyaw = 1024; game_frame(); } P();
	sheet("F_misc");
	/* ---------- nemici ---------- */
	host_noplayer = 1;
	{
		int type, st; char nm[40];
		static const int brs[][3] = { {E_IDLE,0,0},{E_CHASE,0,0},{E_WIND,10,0},{E_WIND,23,0},{E_STRIKE,4,0},{E_RECOV,10,0},{E_WIND,8,1},{E_STRIKE,3,1},{E_HURT,3,0},{E_DEAD,8,0},{E_DEAD,40,0} };
		static const int sts[][3] = { {E_IDLE,0,0},{E_CHASE,0,0},{E_CIRCLE,0,0},{E_WIND,8,0},{E_STRIKE,4,0},{E_STRIKE,9,0},{E_RECOV,6,0},{E_HURT,4,0},{E_DEAD,3,0},{E_DEAD,40,0} };
		for (type = 0; type < 2; type++) {
			int n = type == 0 ? 11 : 10, j;
			pl_setup(); host_noplayer = 1; pl.x = 0; pl.z = 0;
			enemy_spawn(0, type, 0, 0); en[0].face = 0; nen = 1;
			for (j = 0; j < n; j++) {
				const int *d = type == 0 ? brs[j] : sts[j];
				int rep = (d[0] == E_CHASE || d[0] == E_CIRCLE) ? 40 : 14;
				en[0].state = d[0]; en[0].t = d[1]; en[0].atype = d[2]; en[0].dir = 1;
				for (i = 0; i < rep; i++) {
					en[0].ph += (d[0] == E_CHASE ? (type ? 210 : 112) : 150);
					if (d[0] == E_CIRCLE) en[0].ph += 100;
					cyaw = 1024; fx = 0; fz = 0; game_frame();
				}
				grab(0, -120, 0);
			}
			snprintf(nm, sizeof nm, "G%d_enemy", type); sheet(nm);
		}
	}
	/* scenario reale: nessun crash con logica attiva */
	host_freeze = 0; host_noplayer = 0; stage = 1; mission_reset(); gstate = G_PLAY; stage = 1;
	enemy_spawn(0, T_BRUTE, -60, 400); enemy_spawn(1, T_STRIKER, 260, 700);
	for (i = 0; i < 400; i++) { setpad((i % 40) < 6 ? PAD_SQUARE : 0, 0, (i % 60) < 30 ? -127 : 0); game_frame(); }
	printf("OK hp=%d en0=%d/%d st%d en1=%d/%d st%d\n", pl.hp, en[0].alive, en[0].hp, en[0].state, en[1].alive, en[1].hp, en[1].state);
	return 0;
}
