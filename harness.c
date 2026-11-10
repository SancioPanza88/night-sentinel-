#ifndef HOST_TEST
#define HOST_TEST
#endif
static int host_freeze, host_noplayer;
#include "../main.c"
#include <stdlib.h>
static void setpad(unsigned pressed, int lx, int ly) {
	PADTYPE *p = (PADTYPE *)pad_buff[0];
	p->stat = 0; p->len = 1; p->type = 7; p->btn = (~pressed) & 0xffff;
	p->rs_x = 128; p->rs_y = 128; p->ls_x = 128 + lx; p->ls_y = 128 + ly;
}
static void run(int n, unsigned pressed, int lx, int ly) { int i; for (i = 0; i < n; i++) { setpad(pressed, lx, ly); game_frame(); } }
extern int host_maxprims;
static void snap(const char *name) {
	char path[200]; FILE *f;
	snprintf(path, sizeof path, "out/%s.ppm", name);
	f = fopen(path, "wb"); fprintf(f, "P6\n320 240\n255\n"); fwrite(fb, 1, sizeof fb, f); fclose(f);
	printf("[%s] pl=(%d,%d,%d) hp=%d en=%d stage=%d | enemy alive=%d hp=%d st=%d | text0: %.60s\n", name, pl.x, pl.y, pl.z, pl.hp, pl.en, stage, en[0].alive, en[0].hp, en[0].state, host_text[0] + 1);
}
int main(void) {
	int i, fr;
	game_init();
	run(3, 0, 0, 0); snap("01_title");
	run(1, PAD_START, 0, 0); run(25, 0, 0, 0); snap("02_idle");
	run(30, 0, 0, -127); snap("03_run");
	/* attacchi senza nemico: pose */
	run(1, PAD_SQUARE, 0, 0); run(6, 0, 0, 0); snap("04_punch");
	run(20, 0, 0, 0);
	run(1, PAD_TRIANGLE, 0, 0); run(10, 0, 0, 0); snap("05_kick");
	run(20, 0, 0, 0);
	run(1, PAD_R1, 0, 0); run(8, 0, 0, 0); snap("06_pulse");
	run(20, 0, 0, 0);
	run(1, PAD_CROSS, 0, 0); run(12, 0, 0, 0); snap("07_jump");
	run(40, 0, 0, 0);
	/* edificio A: entra dalla porta */
	pl.x = -400; pl.y = 0; pl.z = -587; pl.face = 3072; cyaw = 3072; fx = pl.x; fz = pl.z; fy = 0;
	run(30, 0, 0, -127); snap("08_door_A");
	run(20, 0, 0, -127); snap("09_inside_A");
	printf("inside_bld=%d\n", inside_bld(pl.x, pl.y, pl.z));
	/* console edificio C */
	pl.x = -400; pl.y = 0; pl.z = -60; pl.face = 3072; cyaw = 3072; pl.hp = 50;
	run(60, 0, 0, -127); snap("10_inside_C");
	/* rampino su B */
	player_reset(); pl.x = 150; pl.z = 400; pl.face = 1024; cyaw = 1024; fx = pl.x; fz = pl.z;
	run(5, 0, 0, 0); snap("11_before_grapple");
	run(1, PAD_R2, 0, 0); run(8, 0, 0, 0); snap("12_grapple_mid");
	for (fr = 0; fr < 80; fr++) { run(1, 0, 0, 0); if (pl.grounded && !pl.gr) break; }
	snap("13_after_grapple"); printf("grapple frames=%d  stage=%d\n", fr, stage);
	run(30, 0, 0, 0); snap("14_stage1");
	/* combattimento */
	player_reset(); stage = 1; pl.x = 0; pl.z = 0; enemy_spawn(0, T_BRUTE, 0, 500); enemy_spawn(1, T_STRIKER, 250, 800); gstate = G_PLAY; cyaw = 0; fx = 0; fz = 0;
	run(40, 0, 0, 0); snap("15_enemy_chase");
	run(30, 0, 0, 0); snap("16_enemy_close");
	for (i = 0; i < 30; i++) { run(1, PAD_SQUARE, 0, 0); run(4, 0, 0, 0); run(1, PAD_SQUARE, 0, 0); run(5, 0, 0, 0); run(1, PAD_SQUARE, 0, 0); run(18, 0, 0, 0); if (alive_count() == 0) break; }
	snap("17_after_punches");
	run(30, 0, 0, 0); snap("18_enemy_dead_or_alive"); printf("stage after fight=%d\n", stage);
	/* pickup */
	pl.x = -840; pl.z = -650; pl.y = 0; stage = 2; run(3, 0, 0, 0); snap("19_core"); printf("stage=%d gstate=%d\n", stage, gstate);
	run(5, 0, 0, 0); snap("20_win");
	printf("OK: wq=%d cb=%d anchors=%d maxprims=%d (%d KB)\n", nwq, ncb, nanc, host_maxprims, host_maxprims*48/1024);
	return 0;
}
