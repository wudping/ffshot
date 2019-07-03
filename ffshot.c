// ┳━┓┳━┓┓━┓┳ ┳┏━┓┏┓┓
// ┣━ ┣━ ┗━┓┃━┫┃ ┃ ┃
// ┇  ┇  ━━┛┇ ┻┛━┛ ┇
// Usage: ffshot | <farbfeld sink>
// Made by vifino. ISC (C) vifino 2018

#define CONFIG_LIBXCB_SHM 1

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <inttypes.h>
#include <xcb/xcb.h>
#include <xcb/xcb_image.h>
#include <errno.h>
#include <err.h>
#include <sys/time.h> //for "struct timeval"
#include <time.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#if CONFIG_LIBXCB_SHM
#include <sys/shm.h>
#include <xcb/shm.h>
#endif

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

#define RGB8888 0
#define FRAME_NUM 3000

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

#define AV_INPUT_BUFFER_PADDING_SIZE 64

typedef struct {
    char *data;
    long size;
} image_dpwu_t;

typedef struct XCBGrabContext {
    uint8_t *buffer;

    xcb_connection_t *conn;
    xcb_screen_t *screen;
    xcb_window_t window;
#if CONFIG_LIBXCB_SHM
    xcb_shm_seg_t segment;
#endif
    int64_t time_frame;

    int x, y;
    int width, height;
    int frame_size;
    int bpp;

    int draw_mouse;
    int follow_mouse;
    int show_region;
    int region_border;
    int centered;

    const char *video_size;
    const char *framerate;

    int has_shm;
} XCBGrabContext;

XCBGrabContext c_dt;



#if (0)
static int check_shm(xcb_connection_t *conn)
{
    xcb_shm_query_version_cookie_t cookie = xcb_shm_query_version(conn);
    xcb_shm_query_version_reply_t *reply;

    reply = xcb_shm_query_version_reply(conn, cookie, NULL);
    if (reply) {
        free(reply);
        return 1;
    }

    return 0;
}

static int allocate_shm(AVFormatContext *s)
{
    XCBGrabContext *c = s->priv_data;
    int size = c->frame_size + AV_INPUT_BUFFER_PADDING_SIZE;
    uint8_t *data;
    int id;

    if (c->buffer)
        return 0;
    id = shmget(IPC_PRIVATE, size, IPC_CREAT | 0777);
    if (id == -1) {
        char errbuf[1024];
        int err = AVERROR(errno);
        //av_strerror(err, errbuf, sizeof(errbuf));
        //av_log(s, AV_LOG_ERROR, "Cannot get %d bytes of shared memory: %s.\n",
               size, errbuf);
        return err;
    }
    xcb_shm_attach(c->conn, c->segment, id, 0);
    data = shmat(id, NULL, 0);
    shmctl(id, IPC_RMID, 0);
    if ((intptr_t)data == -1 || !data)
        return AVERROR(errno);
    c->buffer = data;
    return 0;
}

static int xcbgrab_frame_shm(AVFormatContext *s, AVPacket *pkt)
{
    XCBGrabContext *c = s->priv_data;
    xcb_shm_get_image_cookie_t iq;
    xcb_shm_get_image_reply_t *img;
    xcb_drawable_t drawable = c->screen->root;
    xcb_generic_error_t *e = NULL;
    int ret;
	printf("dpwu %s %s %d====\n", __FILE__, __FUNCTION__, __LINE__);

    ret = allocate_shm(s);
    if (ret < 0)
        return ret;

    iq = xcb_shm_get_image(c->conn, drawable,
                           c->x, c->y, c->width, c->height, ~0,
                           XCB_IMAGE_FORMAT_Z_PIXMAP, c->segment, 0);
    img = xcb_shm_get_image_reply(c->conn, iq, &e);

    xcb_flush(c->conn);

    if (e) {
        av_log(s, AV_LOG_ERROR,
               "Cannot get the image data "
               "event_error: response_type:%u error_code:%u "
               "sequence:%u resource_id:%u minor_code:%u major_code:%u.\n",
               e->response_type, e->error_code,
               e->sequence, e->resource_id, e->minor_code, e->major_code);

        return AVERROR(EACCES);
    }

    free(img);

    pkt->data = c->buffer;
    pkt->size = c->frame_size;

    return 0;
}
#endif /* CONFIG_LIBXCB_SHM */

#if CONFIG_LIBXCB_SHM
static int check_shm(xcb_connection_t *conn)
{
    xcb_shm_query_version_cookie_t cookie;
	cookie = xcb_shm_query_version(conn);
    xcb_shm_query_version_reply_t *reply;

    reply = xcb_shm_query_version_reply(conn, cookie, NULL);
    if (reply) {
        free(reply);
        return 1;
    }

    return 0;
}

static int allocate_shm(XCBGrabContext *c)
{
    //XCBGrabContext *c = s->priv_data;
    int size = c->frame_size + AV_INPUT_BUFFER_PADDING_SIZE;
    uint8_t *data;
    int id;

    if (c->buffer)
        return 0;
    id = shmget(IPC_PRIVATE, size, IPC_CREAT | 0777);
    if (id == -1) {
        char errbuf[1024];
        //int err = AVERROR(errno);
        //av_strerror(err, errbuf, sizeof(errbuf));
        //av_log(s, AV_LOG_ERROR, "Cannot get %d bytes of shared memory: %s.\n",  size, errbuf);
        //return err;
        return -1;
    }
    xcb_shm_attach(c->conn, c->segment, id, 0);
    data = shmat(id, NULL, 0);
    shmctl(id, IPC_RMID, 0);
    if ((intptr_t)data == -1 || !data)
        return AVERROR(errno);
    c->buffer = data;
    return 0;
}

static int xcbgrab_frame_shm(XCBGrabContext *c, image_dpwu_t *pkt)
{
    //XCBGrabContext *c = s->priv_data;
    xcb_shm_get_image_cookie_t iq;
    xcb_shm_get_image_reply_t *img;
    xcb_drawable_t drawable = c->screen->root;
    xcb_generic_error_t *e = NULL;
    int ret;
	printf("dpwu %s %s %d====\n", __FILE__, __FUNCTION__, __LINE__);

    ret = allocate_shm(c);
    if (ret < 0)
        return ret;

    iq = xcb_shm_get_image(c->conn, drawable,
                           c->x, c->y, c->width, c->height, ~0,
                           XCB_IMAGE_FORMAT_Z_PIXMAP, c->segment, 0);
    img = xcb_shm_get_image_reply(c->conn, iq, &e);

    xcb_flush(c->conn);

    if (e) {
		/*
        av_log(s, AV_LOG_ERROR,
               "Cannot get the image data "
               "event_error: response_type:%u error_code:%u "
               "sequence:%u resource_id:%u minor_code:%u major_code:%u.\n",
               e->response_type, e->error_code,
               e->sequence, e->resource_id, e->minor_code, e->major_code);
		return AVERROR(EACCES);
		*/
		return -1;
    }

    free(img);

    pkt->data = c->buffer;
    pkt->size = c->frame_size;

    return 0;
}
#endif /* CONFIG_LIBXCB_SHM */



int main(int argc, char* argv[]) {
	if (!(argc == 1 || argc == 2)) { // one arg max
		printf("Usage: %s [wid]\n", argv[0]);
		return 1;
	}

	if (argc == 2)
		win = (uint32_t) strtoumax(argv[1], (char* *) NULL, 0);
	char fileName[512];
	int i_dpwu = 0;
	XCBGrabContext *c = &c_dt;
	c->has_shm = 1;

	con = xcb_connect(NULL, NULL);
	if (xcb_connection_has_error(con))
		errx(2, "Unable to connect to the X server");


	scr = xcb_setup_roots_iterator(xcb_get_setup(con)).data;
	if (!scr){
		errx(2, "Unable to get screen data.");
	}
	if (argc == 1){
		win = scr->root;
	}

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

	// allocate buffer.
	uint8_t* img = malloc(width * height * 4);
	if (!img){
		errx(2, "Failed to allocate buffer.");
	}
#if (1)
	struct tm *t;
	time_t tt;
	time(&tt);
	t = localtime(&tt);
	printf("pthread_self() = %lu, %4d%02d%02d %02d:%02d:%02d xxxxxffff ====\n", pthread_self(), t->tm_year + 1900, t->tm_mon + 1, t->tm_mday, t->tm_hour, t->tm_min, t->tm_sec);
#endif



#if (0)	
#if (CONFIG_LIBXCB_SHM)
	if (c->has_shm = check_shm(c->conn)){
		c->segment = xcb_generate_id(c->conn);
	}
			
#endif
#endif


	while(1){
		if(i_dpwu++ > FRAME_NUM){
			break;
		}

		usleep(100 * 1000);

		// Get image from the X server. Yuck.
		fprintf(stderr, "%08x: %ux%u to %ux%u\n", win, pos_x, pos_y, width, height);
		ic = xcb_get_image(con, XCB_IMAGE_FORMAT_Z_PIXMAP, win, pos_x, pos_y, width, height, ~0);
		ir = xcb_get_image_reply(con, ic, NULL);
		if (!ir)
			errx(2, "Failed to get Image");

		unsigned char* data = xcb_get_image_data(ir);
		if (!data){
			errx(2, "Failed to get Image data");
		}
		uint32_t bpp = ir->depth;
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
#if (1)
			//printf("htobe16(r) = %d, r = %d\n", htobe16(r), r);
			img[dst_p + 0] = data[p + 2]; //r
			img[dst_p + 1] = data[p + 1]; //g
			img[dst_p + 2] = data[p + 0]; //b
			if(RGB8888){
				img[dst_p + 3] = hasa ? data[p + 0] : 0xFF;
			}
#endif
		}
		//bwrite((unsigned char*) img, width * height * 8);
	    printf("dpwu w,h = %d, %d, i_dpwu = %d ======\n", width, height, i_dpwu);
		#if (0)
		if(RGB8888){
			sprintf(fileName, "output_%03d_%dx%d_8888.rgba", i_dpwu, width, height);
			FILE *fp_yuv=fopen(fileName,"wb+"); 
			fwrite((unsigned char*) img, 1, width * height * 4, fp_yuv);    //Y	 
		    fclose(fp_yuv);
		}else{
			sprintf(fileName, "output_%03d_%dx%d_888.rgb", i_dpwu, width, height);
			FILE *fp_yuv=fopen(fileName,"wb+");
			fwrite((unsigned char*) img, 1, width * height * 3, fp_yuv);    //Y	 
		    fclose(fp_yuv);
		}
		
		printf("fileName %s\n", fileName);
		#endif
	}
#if (1)
		time(&tt);
		t = localtime(&tt);
		printf("after pthread_self() = %lu, %4d%02d%02d %02d:%02d:%02d xxxxxffff ====\n", pthread_self(), t->tm_year + 1900, t->tm_mon + 1, t->tm_mday, t->tm_hour, t->tm_min, t->tm_sec);
#endif
	free(img);
	free(ir);

	xcb_disconnect(con);
	return 0;
}

#if (0)

    #include <stdio.h>
    #include <xcb/xcb.h>
    #include <inttypes.h>

    int 
    main ()
    {
        /* Open the connection to the X server. Use the DISPLAY environment variable */

        int i, screenNum;
        xcb_connection_t *connection = xcb_connect (NULL, &screenNum);


        /* Get the screen whose number is screenNum */

        const xcb_setup_t *setup = xcb_get_setup (connection);
        xcb_screen_iterator_t iter = xcb_setup_roots_iterator (setup);  

        // we want the screen at index screenNum of the iterator
        for (i = 0; i < screenNum; ++i) {
            xcb_screen_next (&iter);
        }

        xcb_screen_t *screen = iter.data;


        /* report */

        printf ("\n");
        printf ("Informations of screen %"PRIu32":\n", screen->root);
        printf ("  width.........: %"PRIu16"\n", screen->width_in_pixels);
        printf ("  height........: %"PRIu16"\n", screen->height_in_pixels);
        printf ("  white pixel...: %"PRIu32"\n", screen->white_pixel);
        printf ("  black pixel...: %"PRIu32"\n", screen->black_pixel);
        printf ("\n");

        return 0;
    }

#endif

