// ┳━┓┳━┓┓━┓┳ ┳┏━┓┏┓┓
// ┣━ ┣━ ┗━┓┃━┫┃ ┃ ┃
// ┇  ┇  ━━┛┇ ┻┛━┛ ┇
// Usage: ffshot | <farbfeld sink>
// Made by vifino. ISC (C) vifino 2018

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <inttypes.h>
#include <xcb/xcb.h>
#include <xcb/xcb_image.h>
#include <errno.h>
#include <err.h>

// I know, I know, not standardized.
// But painless fallback.
#if defined(__linux__)
#include <endian.h>
#if __BYTE_ORDER != __BIG_ENDIAN
#define DOCONVERT
#endif
#else
#define DOCONVERT
#include <arpa/inet.h>
#define be32toh ntohl
#define htobe32 htonl
#define be16toh ntohs
#define htobe16 htons
#endif

#define RGB8888 1

static inline void bwrite(const unsigned char* buffer, size_t bytes) {
	if (!fwrite(buffer, bytes, 1, stdout)) {
		fprintf(stderr, "write failed.\n");
		exit(1);
	}
}

static xcb_connection_t* con;
static xcb_screen_t* scr;
static uint32_t win;

static uint16_t pos_x, pos_y;
static uint16_t width, height;
static unsigned char buf[9];

static xcb_get_geometry_cookie_t gc;
static xcb_get_geometry_reply_t* gr;
static xcb_get_image_cookie_t ic;
static xcb_get_image_reply_t* ir;

int main(int argc, char* argv[]) {
	if (!(argc == 1 || argc == 2)) { // one arg max
		printf("Usage: %s [wid]\n", argv[0]);
		return 1;
	}

	if (argc == 2)
		win = (uint32_t) strtoumax(argv[1], (char* *) NULL, 0);

	con = xcb_connect(NULL, NULL);
	if (xcb_connection_has_error(con))
		errx(2, "Unable to connect to the X server");

	scr = xcb_setup_roots_iterator(xcb_get_setup(con)).data;
	if (!scr)
		errx(2, "Unable to get screen data.");

	if (argc == 1)
		win = scr->root;

	if (!win) {
		fprintf(stderr, "Invalid window number given.\n");
		return 1;
	}

	// Get window geometry.
	gc = xcb_get_geometry(con, win);
	gr = xcb_get_geometry_reply(con, gc, NULL);
	if (!gr)
		errx(1, "0x%08x: no such window");

	pos_x = gr->x;
	pos_y = gr->y;
	width = gr->width;
	height = gr->height;
	free(gr);

	// Get image from the X server. Yuck.
	fprintf(stderr, "%08x: %ux%u to %ux%u\n", win, pos_x, pos_y, width, height);
	ic = xcb_get_image(con, XCB_IMAGE_FORMAT_Z_PIXMAP, win, pos_x, pos_y, width, height, ~0);
	ir = xcb_get_image_reply(con, ic, NULL);
	if (!ir)
		errx(2, "Failed to get Image");

	unsigned char* data = xcb_get_image_data(ir);
	if (!data)
		errx(2, "Failed to get Image data");
	uint32_t bpp = ir->depth;

	// allocate buffer.
	uint8_t* img = malloc(width * height * 4);
	if (!img)
		errx(2, "Failed to allocate buffer.");

	// Output image header
	bwrite((unsigned char*)("farbfeld"), 8);
	*(uint32_t*)buf = htobe32(width);
	*(uint32_t*)(buf + 4) = htobe32(height);
	bwrite(buf, 8);

	unsigned int hasa = 1;

	switch (bpp) {
	case 24:
		hasa = 0;
	case 32:
		break;
	default:
		errx(2, "No support for bit depths other than 24/32 bit: bit depth %i. Fix me?", bpp);
	}
	printf("bpp %d\n", bpp);

	unsigned int end = width * height;
	unsigned short r, g, b;
	uint32_t i;
	size_t p;
	size_t dst_p;
	for (i=0; i < end; i++) {
		// write out pixel
		p = i * 4;
		if(RGB8888){
			dst_p = i * 4;
		}else{
			dst_p = i * 3;
		}
		// BGRA? thanks Xorg.
		r = data[p + 2] << 8;
		g = data[p + 1] << 8;
		b = data[p + 0] << 8;

#if (1)
		//printf("htobe16(r) = %d, r = %d\n", htobe16(r), r);
		img[dst_p + 0] = data[p + 2];
		img[dst_p + 1] = data[p + 1];
		img[dst_p + 2] = data[p + 0];
		if(RGB8888){
			img[dst_p + 3] = hasa ? data[p + 0] : 0xFF;
		}
#endif
	}
	//bwrite((unsigned char*) img, width * height * 8);

    printf("dpwu w,h = %d, %d ======\n", width, height);
	if(RGB8888){
		FILE *fp_yuv=fopen("output_8888.rgba","wb+"); 
		fwrite((unsigned char*) img, 1, width * height * 4, fp_yuv);    //Y	 
	    fclose(fp_yuv);
	}else{
		FILE *fp_yuv=fopen("output_888.rgb","wb+"); 
		fwrite((unsigned char*) img, 1, width * height * 3, fp_yuv);    //Y	 
	    fclose(fp_yuv);
	}
	free(img);
	free(ir);

	xcb_disconnect(con);
	return 0;
}
