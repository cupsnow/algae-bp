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
	int ret = -1, vmidx;

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
	int qbuf_idx, r;
	v4l2_t *v4l2 = (v4l2_t*)cbarg;

	if (v4l2->evconn.fd != fd) {
		log_e("sanity check mismatch fd\n");
		return;
	}
	if (!(ev & ALOE_EVB2_FLAG_READ)) return;

	for (;;) {
		if (v4l2_streamcap_deq(v4l2->evconn.fd, &qbuf_idx) != 0) {
			r = errno;
			if (r == EAGAIN
#ifdef EWOULDBLOCK
					|| r == EWOULDBLOCK
#endif
					|| r == EINTR) {
				return;
			}
			log_e("Failed VIDIOC_DQBUF: %s\n", strerror(r));
			return;
		}
//		log_d("deq %d\n", qbuf_idx);

#ifdef USE_X264
		if (v4l2->encoder && v4l2->x264_fp) {
			const uint8_t *data = (uint8_t*)v4l2->v4l2_mem[qbuf_idx].start;

			 // YUV420 planar: Y plane, then U plane, then V plane
			uint32_t stride = v4l2->v4l2_fmt.width;
			const uint8_t *yPlane = data;
			const uint8_t *uPlane = data + stride * v4l2->v4l2_fmt.height;
			const uint8_t *vPlane = uPlane + (stride / 2) * (v4l2->v4l2_fmt.height / 2);

			std::vector<uint8_t> encodedData;
			int encSize = v4l2->encoder->encode(yPlane, uPlane, vPlane, stride,
				encodedData);

			if (encSize > 0) {
				if (fwrite(encodedData.data(), 1, encodedData.size(),
						v4l2->x264_fp) != encodedData.size()) {
					log_e("failed x264\n");
				}
			}
		}
#endif

		if (v4l2_streamcap_enq(v4l2->evconn.fd, qbuf_idx) != 0) {
			r = errno;
			log_e("Failed VIDIOC_QBUF: %s\n", strerror(r));
			return;
		}
	}
}

static int v4l2_queue_all(v4l2_t *v4l2) {
	unsigned i;

	for (i = 0; i < v4l2->v4l2_mem_cnt; i++) {
		if (v4l2_streamcap_enq(v4l2->evconn.fd, (int)i) != 0) {
			int r = errno;
			log_e("Failed VIDIOC_QBUF[%u]: %s\n", i, strerror(r));
			return -1;
		}
	}
	return 0;
}

static int v4l2_stop(v4l2_t *v4l2) {
	if (!v4l2) return -1;
	if (v4l2->state != V4L2_STATE_START) return 0;

	evconn_cancel(&v4l2->evconn);
	if (v4l2->evconn.fd >= 0) {
		if (v4l2_streamcap_stop(v4l2->evconn.fd) != 0) {
			int r = errno;
			log_e("Failed VIDIOC_STREAMOFF: %s\n", strerror(r));
		}
	}
	v4l2->state = V4L2_STATE_OPEN;
	log_d("v4l2 stopped\n");
	return 0;
}

static int v4l2_close(v4l2_t *v4l2) {
	unsigned i;

	if (!v4l2) return -1;

	v4l2_stop(v4l2);

#ifdef USE_X264
	if (v4l2->encoder) {
		delete v4l2->encoder;
		v4l2->encoder = NULL;
	}
	if (v4l2->x264_fp) {
		fclose(v4l2->x264_fp);
		v4l2->x264_fp = NULL;
	}
#endif

	if (v4l2->v4l2_mem) {
		for (i = 0; i < v4l2->v4l2_mem_cnt; i++) {
			if (v4l2->v4l2_mem[i].start && v4l2->v4l2_mem[i].start != MAP_FAILED) {
				munmap(v4l2->v4l2_mem[i].start, v4l2->v4l2_mem[i].length);
			}
		}
		aloe_free(v4l2->v4l2_mem);
		v4l2->v4l2_mem = NULL;
		v4l2->v4l2_mem_cnt = 0;
	}

	if (v4l2->evconn.fd >= 0) {
		unsigned zcnt = 0;
		v4l2_request_devbuf(v4l2->evconn.fd, &zcnt);
		close(v4l2->evconn.fd);
		v4l2->evconn.fd = -1;
	}
	v4l2->state = V4L2_STATE_RESET;
	return 0;
}

static int v4l2_open(v4l2_t *v4l2, const char *path, unsigned width,
		unsigned height) {
	int fd = -1, ret = -1;
	unsigned vmem_cnt = 5, pixfmt = V4L2_PIX_FMT_YUV420;
	v4l2_mem_t *vmem = NULL;

	if (!v4l2 || !path) {
		log_e("Invalid argument\n");
		return -1;
	}
	if (v4l2->state != V4L2_STATE_RESET) {
		v4l2_close(v4l2);
	}

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
		for (int i = 0; i < (int)vmem_cnt; i++) {
			if (vmem[i].start && vmem[i].start != MAP_FAILED) {
				munmap(vmem[i].start, vmem[i].length);
			}
		}
		aloe_free(vmem);
	}
	if (fd != -1) close(fd);
	return ret;
}

#ifdef USE_X264
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
#endif

static int v4l2_start(v4l2_t *v4l2) {
	int ret = -1, r;
	int fps = 30, kbps = 2000;

	if (!v4l2 || v4l2->evconn.fd < 0) {
		log_e("v4l2 not open\n");
		return -1;
	}
	if (v4l2->state == V4L2_STATE_START) return 0;
	if (v4l2->state != V4L2_STATE_OPEN) {
		log_e("v4l2 unexpected state %d\n", v4l2->state);
		return -1;
	}

#ifdef USE_X264
	if (!v4l2->encoder) {
		if ((v4l2->encoder = new X264Encoder(v4l2->v4l2_fmt.width,
				v4l2->v4l2_fmt.height, fps, kbps)) == NULL) {
			log_e("failed x264\n");
			goto finally;
		}
		if (!v4l2->encoder->init()) {
			log_e("failed x264\n");
			goto finally;
		}
	}
	if (!v4l2->x264_fp) {
		if ((v4l2->x264_fp = fopen("xxxxx.x264", "wb")) == NULL) {
			log_e("failed x264\n");
			goto finally;
		}
		if (!writeHeaders(v4l2->x264_fp, *v4l2->encoder)) {
			log_e("failed x264\n");
			goto finally;
		}
	}
#endif
	if (v4l2_queue_all(v4l2) != 0) {
		goto finally;
	}
	if (v4l2_streamcap_start(v4l2->evconn.fd) != 0) {
		r = errno;
		log_e("Failed VIDIOC_STREAMON: %s\n", strerror(r));
		goto finally;
	}
	if (evconn_add_read(&v4l2->evconn, &v4l2_streamcap_cb, v4l2) == NULL) {
		log_e("Failure aloe_evb2_add_fd\n");
		v4l2_streamcap_stop(v4l2->evconn.fd);
		goto finally;
	}
	v4l2->state = V4L2_STATE_START;
	log_d("v4l2 started\n");
	ret = 0;
finally:
	return ret;
}

void* v4l2_init(void *evctx, const char *path) {
	int ret = -1;
	v4l2_t *v4l2 = NULL;

	if (!path || !path[0]) path = "/dev/video10";

	if ((v4l2 = (v4l2_t*)aloe_calloc(1, sizeof(*v4l2))) == NULL) {
		log_e("failed alloc v4l2\n");
		goto finally;
	}
	v4l2->evconn.fd = -1;
	v4l2->evconn.ev_ctx = evctx;

	if (v4l2_open(v4l2, path, 1920, 1080) != 0) {
		goto finally;
	}
	if (v4l2_start(v4l2) != 0) {
		goto finally;
	}
	ret = 0;
finally:
	if (ret != 0 && v4l2) {
		v4l2_destroy(v4l2);
		v4l2 = NULL;
	}
	return v4l2;
}

void v4l2_destroy(void *_v4l2) {
	v4l2_t *v4l2 = (v4l2_t*)_v4l2;

	if (!v4l2) return;
	v4l2_close(v4l2);
	aloe_free(v4l2);
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

	if (strcasecmp(argv[1], "start") == 0) {
		return v4l2_start(v4l2);
	}
	if (strcasecmp(argv[1], "stop") == 0) {
		return v4l2_stop(v4l2);
	}
	log_e("Unknown command %s\n", argv[1]);
	return -1;
}
