/* Host-only smoke test:  cc figlet.c figlet_test.c -o t && ./t <font.flf> TEXT */
#include <stdio.h>
#include <stdlib.h>
#include "figlet.h"

int
main(int argc, char **argv)
{
	if (argc < 3) {
		fprintf(stderr, "usage: %s font.flf TEXT\n", argv[0]);
		return 2;
	}
	struct flf_font *f = flf_load(argv[1]);
	if (!f) {
		fprintf(stderr, "failed to load %s\n", argv[1]);
		return 1;
	}
	printf("height=%d baseline=%d maxlen=%d hardblank='%c'\n", f->height, f->baseline, f->max_len, f->hardblank);

	struct flf_render *r = flf_render_text(f, argv[2]);
	if (!r) {
		fprintf(stderr, "render failed\n");
		return 1;
	}
	printf("render %d rows x %d cols:\n", r->rows, r->cols);
	for (int y = 0; y < r->rows; y++) {
		for (int x = 0; x < r->cols; x++)
			putchar(r->cell[y * r->cols + x] ? '#' : ' ');
		putchar('\n');
	}
	flf_render_free(r);
	flf_free(f);
	return 0;
}
