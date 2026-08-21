/* $Id$
 *
 * @author joelai
 *
 * @file noname
 * @brief noname
 */

#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <netinet/tcp.h> // For TCP_KEEPIDLE, TCP_KEEPINTVL, etc. (Linux)
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/videodev2.h>

#include "priv.h"
#include "v4l2.h"
#ifdef USE_X264
#  include <vector>
#  include "x264_encoder.h"
#endif

typedef struct {
	void* start;
	size_t length;
} v4l2_mem_t;

typedef struct {
	unsigned width, height, pixelformat;
} v4l2_fmt_t;

typedef enum {
	V4L2_STATE_RESET = 0,
	V4L2_STATE_OPEN,
	V4L2_STATE_START,
} v4l2_state_t;

typedef struct {
	evconn_t evconn;
	v4l2_fmt_t v4l2_fmt;
	v4l2_mem_t *v4l2_mem;
	unsigned v4l2_mem_cnt;
	int state;
#ifdef USE_X264
	X264Encoder *encoder;
	FILE *x264_fp;
#endif
} v4l2_t;

extern "C" {
void *v4l2_global;
}

static int v4l2_open_streamcap(const char *path) {
	int fd = -1, r, ret = -1;
	struct v4l2_capability cap = {};

	if ((fd = open(path, O_RDWR | O_NONBLOCK, 0)) == -1) {
		r = errno;
		log_e("Failed open %s; %s\n", path, strerror(r));
		goto finally;
	}

	if (ioctl(fd, VIDIOC_QUERYCAP, &cap) < 0) {
		r = errno;
		log_e("Failed VIDIOC_QUERYCAP\n");
		goto finally;
	}
	if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE) ||
			!(cap.capabilities & V4L2_CAP_STREAMING)) {
		log_e("Unexpect capabilities\n");
		goto finally;
	}
	ret = fd;
	fd = -1;
finally:
	if (fd != -1) close(fd);
	return ret;
}

static int v4l2_config_fmt(int fd, unsigned pixfmt, unsigned *width,
		unsigned *height) {
	int ret = -1, r;
	struct v4l2_format vfmt = {};

	vfmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	vfmt.fmt.pix.width = *width;
	vfmt.fmt.pix.height = *height;
	vfmt.fmt.pix.pixelformat = pixfmt; // V4L2_PIX_FMT_YUV420; // YU12 = YUV420 planar
	vfmt.fmt.pix.field = V4L2_FIELD_NONE;

	if (ioctl(fd, VIDIOC_S_FMT, &vfmt) < 0) {
		r = errno;
		log_e("Failed VIDIOC_S_FMT: %s\n", strerror(r));
		goto finally;
	}

	ioctl(fd, VIDIOC_G_FMT, &vfmt);
	*width = vfmt.fmt.pix.width;
	*height = vfmt.fmt.pix.height;

	log_d("Format: YU12 (YUV420P) %ux%u, bytesperline=%u, sizeimage=%u\n",
			vfmt.fmt.pix.width, vfmt.fmt.pix.height, vfmt.fmt.pix.bytesperline, vfmt.fmt.pix.sizeimage);
	ret = 0;
finally:
	return ret;
}

static int v4l2_request_devbuf(int fd, unsigned *count) {
	int ret = -1, r;
	struct v4l2_requestbuffers reqbuf = {};

	reqbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	reqbuf.memory = V4L2_MEMORY_MMAP;
	reqbuf.count = count ? *count : 0;
	if (ioctl(fd, VIDIOC_REQBUFS, &reqbuf) < 0) {
		r = errno;
		log_e("Failed VIDIOC_REQBUFS: %s\n", strerror(r));
		goto finally;
	}
	if (count) *count = reqbuf.count;
	ret = 0;
finally:
	return ret;
}

int v4l2_streamcap_deq(int fd, int *index) {
	int ret = -1;
	struct v4l2_buffer qbuf = {};

	qbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	qbuf.memory = V4L2_MEMORY_MMAP;
	if (ioctl(fd, VIDIOC_DQBUF, &qbuf) < 0) {
		goto finally;
	}
	*index = qbuf.index;
	ret = 0;
finally:
	return ret;
}

int v4l2_streamcap_enq(int fd, int index) {
	struct v4l2_buffer qbuf = {};

	qbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	qbuf.memory = V4L2_MEMORY_MMAP;
	qbuf.index = index;
	return ioctl(fd, VIDIOC_QBUF, &qbuf);
}

static int v4l2_map_devbuf(int fd, unsigned count, v4l2_mem_t *vmem) {
	int ret = -1, r, vmidx;

	for (vmidx = 0; vmidx < count; vmidx++) {
		struct v4l2_buffer qbuf = {};

		qbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		qbuf.memory = V4L2_MEMORY_MMAP;
		qbuf.index = vmidx;
		if (ioctl(fd, VIDIOC_QUERYBUF, &qbuf) < 0) {
			goto finally;
		}
		vmem[vmidx].length = qbuf.length;
		if ((vmem[vmidx].start = mmap(nullptr, qbuf.length,
				PROT_READ | PROT_WRITE, MAP_SHARED, fd,
				qbuf.m.offset)) == MAP_FAILED) {
			vmem[vmidx].start = NULL;
			goto finally;
		}
#if 1
		if (v4l2_streamcap_enq(fd, vmidx) != 0) {
			goto finally;
		}
#else
		if (ioctl(fd, VIDIOC_QBUF, &qbuf) < 0) {
			goto finally;
		}
#endif
	}
	ret = 0;
finally:
	return ret;
}

static int v4l2_streamcap_start(int fd) {
	enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

	return ioctl(fd, VIDIOC_STREAMON, &type);
}

static int v4l2_streamcap_stop(int fd) {
	enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

	return ioctl(fd, VIDIOC_STREAMOFF, &type);
}

static void v4l2_streamcap_cb(int fd, unsigned ev, void *cbarg) {
	int r, qbuf_idx;
	v4l2_t *v4l2= (v4l2_t*)cbarg;

	v4l2->evconn.ev = NULL;
	if (v4l2_streamcap_deq(v4l2->evconn.fd, &qbuf_idx) != 0) {
		goto finally;
	}
//	log_d("deq %d\n", qbuf_idx);

#ifdef USE_X264
	{
	const uint8_t *data = (uint8_t*)v4l2->v4l2_mem[qbuf_idx].start;

	 // YUV420 planar: Y plane, then U plane, then V plane
	uint32_t stride = v4l2->v4l2_fmt.width;
	const uint8_t *yPlane = data;
	const uint8_t *uPlane = data + stride * v4l2->v4l2_fmt.height;
	const uint8_t *vPlane = uPlane + (stride / 2) * (v4l2->v4l2_fmt.height / 2);

	 // Encode the frame
	std::vector<uint8_t> encodedData;
	int encSize = v4l2->encoder->encode(yPlane, uPlane, vPlane, stride,
		encodedData);

	if (encSize > 0) {
		if (fwrite(encodedData.data(), 1, encodedData.size(),
				v4l2->x264_fp) != encodedData.size()) {
			log_e("failed x264\n");
		}
	}

	} //USE_X264
#endif

	v4l2_streamcap_enq(v4l2->evconn.fd, qbuf_idx);
finally:
	// keep listen
	if ((v4l2->evconn.ev = aloe_ev_put(v4l2->evconn.ev_ctx,
			v4l2->evconn.fd, &v4l2_streamcap_cb, v4l2, aloe_ev_flag_read,
			ALOE_EV_INFINITE, 0)) == NULL) {
		log_e("Failure aloe_ev_put\n");
	}
}

int v4l2_close(v4l2_t *v4l2) {

}

int v4l2_open(v4l2_t *v4l2, const char *path, unsigned width,
		unsigned height) {
	int fd = -1, r, ret = -1;
	unsigned vmem_cnt = 5, pixfmt = V4L2_PIX_FMT_YUV420;
	v4l2_mem_t *vmem = NULL;

	if ((fd = v4l2_open_streamcap(path)) == -1) {
		goto finally;
	}
	if (v4l2_config_fmt(fd, pixfmt, &width, &height) != 0) {
		goto finally;
	}
	if (v4l2_request_devbuf(fd, &vmem_cnt) != 0) {
		goto finally;
	}
	if ((vmem = (v4l2_mem_t*)aloe_calloc(1, sizeof(*vmem) * vmem_cnt)) == NULL) {
		goto finally;
	}
	if (v4l2_map_devbuf(fd, vmem_cnt, vmem) != 0) {
		goto finally;
	}
	v4l2->evconn.fd = fd;
	v4l2->v4l2_mem_cnt = vmem_cnt;
	v4l2->v4l2_mem = vmem;
	v4l2->v4l2_fmt.pixelformat = pixfmt;
	v4l2->v4l2_fmt.width = width;
	v4l2->v4l2_fmt.height = height;
	v4l2->state = V4L2_STATE_OPEN;

	log_d("%s %d x %d\n", path, width, height);

	fd = -1;
	vmem = NULL;
	ret = 0;
finally:
	if (vmem) {
		for (int i = 0; i < vmem_cnt; i++) {
			if (vmem[i].start && vmem[i].start != MAP_FAILED) {
				munmap(vmem[i].start, vmem[i].length);
			}
		}
		aloe_free(vmem);
	}
	if (fd != -1) close(fd);
	return ret;
}

static bool writeHeaders(FILE* fp, const X264Encoder& enc) {
  size_t spsSize = 0, ppsSize = 0;
  enc.getSPS(spsSize, ppsSize);

  const uint8_t* spsData = enc.getSPSData();
  const uint8_t* ppsData = enc.getPPSData();

  // Write SPS with 4-byte start code 0x00000001
  static const uint8_t startCode4[] = {0x00, 0x00, 0x00, 0x01};
  if (fwrite(startCode4, 1, 4, fp) != 4) return false;
  if (spsSize > 0 && fwrite(spsData, 1, spsSize, fp) != spsSize) return false;

  // Write PPS with 4-byte start code
  if (fwrite(startCode4, 1, 4, fp) != 4) return false;
  if (ppsSize > 0 && fwrite(ppsData, 1, ppsSize, fp) != ppsSize) return false;

  return true;
}

int v4l2_start(v4l2_t *v4l2) {
	int ret = -1;
	int fps = 30, kbps = 2000;

	if (v4l2->state == V4L2_STATE_OPEN) {
#ifdef USE_X264
		if ((v4l2->encoder = new X264Encoder(v4l2->v4l2_fmt.width,
				v4l2->v4l2_fmt.height, fps, kbps)) == NULL) {
			log_e("failed x264\n");
			goto finally;
		}
		if (!v4l2->encoder->init()) {
			log_e("failed x264\n");
			goto finally;
		}
		if ((v4l2->x264_fp = fopen("xxxxx.x264", "wb")) == NULL) {
			log_e("failed x264\n");
			goto finally;
		}
		if (!writeHeaders(v4l2->x264_fp, *v4l2->encoder)) {
			log_e("failed x264\n");
			goto finally;
		}
#endif
		if (v4l2_streamcap_start(v4l2->evconn.fd) != 0) {
			goto finally;
		}
		v4l2->state = V4L2_STATE_START;

		if ((v4l2->evconn.ev = aloe_ev_put(v4l2->evconn.ev_ctx,
				v4l2->evconn.fd, &v4l2_streamcap_cb, v4l2, aloe_ev_flag_read,
				ALOE_EV_INFINITE, 0)) == NULL) {
			log_e("Failure aloe_ev_put\n");
			goto finally;
		}
	}
	if (v4l2->state == V4L2_STATE_START) {
		ret = 0;
	}
finally:
	if (ret != 0) {
		if (v4l2->evconn.ev) {
			aloe_ev_cancel(v4l2->evconn.ev_ctx, v4l2->evconn.ev);
			v4l2->evconn.ev = NULL;
		}
		if (v4l2->state == V4L2_STATE_START) {
			v4l2_streamcap_stop(v4l2->evconn.fd);
			v4l2->state == V4L2_STATE_OPEN;
		}
	}
	return ret;
}

int v4l2_stop(v4l2_t *v4l2) {
}


void* v4l2_init(void *evctx, const char *path) {
	int ret = -1;
	v4l2_t *v4l2 = NULL;

	if ((v4l2 = (v4l2_t*)aloe_calloc(1, sizeof(*v4l2))) == NULL) {
		goto finally;
	}
	v4l2->evconn.ev_ctx = evctx;

	if (v4l2_open(v4l2, "/dev/video10", 1920, 1080) != 0) {
		goto finally;
	}
	v4l2_start(v4l2);
	ret = 0;
finally:
	if (ret != 0) {

	}
	return v4l2;
}

void v4l2_destroy(void *_v4l2ctx) {
}

int v4l2_cli(void *_v4l2, int argc, const char **argv) {
	v4l2_t *v4l2 = (v4l2_t*)_v4l2;

	// wifi abc -> argv[1/2]: wifi; argv[2/2]: abc
//	dump_argv(argc, argv);

	if (argc < 2 || strcasecmp(argv[1], "help") == 0) {
		FILE *fout = stdout;

		fprintf(fout,
"COMMAND\n"
"    start    - Start capture\n"
"    stop     - Stop capture\n"
		);
		fflush(fout);
		return 0;
	}

	if (!v4l2) {
		log_e("v4l2 absent\n");
		return 1;
	}
}
