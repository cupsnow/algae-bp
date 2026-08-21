/* $Id$
 *
 * SPDX-License-Identifier: MIT
 *
 * @author joelai
 *
 * @file /algae-bp/package/tester1/tester_v4l2rtsp2.cpp
 * @brief Capture V4L2 video, encode to H.264, serve RTSP via discrete NAL framer
 *
 * Same as tester_v4l2rtsp but uses H264VideoStreamDiscreteFramer.
 * X264Encoder Annex-B output is split into raw NAL units (no start codes).
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <vector>
#include <chrono>

#include <getopt.h>

#include <liveMedia.hh>
#include <BasicUsageEnvironment.hh>
#include <GroupsockHelper.hh>

#include <aloe/util_img.h>

#include "v4l2_capture.h"
#include "x264_encoder.h"
#include "priv.h"

#define VERBOSE_LEVEL_NONE 0
#define VERBOSE_LEVEL_ERR 1
#define VERBOSE_LEVEL_INFO 2
#define VERBOSE_LEVEL_DEBUG 3
#define VERBOSE_LEVEL_VERBOSE 4

#define RG10_RGB_DOWNSCALE 4


static const uint32_t fourcc_yu12 = aloe_fourcc_val("YU12"); // YU12 = YUV420 planar
static const uint32_t fourcc_rg10 = aloe_fourcc_val("RG10");

static const char *vcap_device = "/dev/video0";
static int vcap_width = 1920;
static int vcap_height = 1080;
static int vcap_fps = 30;
static uint32_t vcap_pixelformat = fourcc_yu12;
static int venc_kbps = 2000;
static int rtsp_port = 8554;
static int verbose_level = VERBOSE_LEVEL_INFO;


// ---------------------------------------------------------------------------
// Annex-B helpers (for discrete framer input)
// ---------------------------------------------------------------------------

static size_t annexBStartCodeSize(const uint8_t *p, size_t size, size_t pos) {
	if (pos + 4 <= size && p[pos] == 0 && p[pos + 1] == 0 &&
			p[pos + 2] == 0 && p[pos + 3] == 1) {
		return 4;
	}
	if (pos + 3 <= size && p[pos] == 0 && p[pos + 1] == 0 && p[pos + 2] == 1) {
		return 3;
	}
	return 0;
}

static void splitAnnexBNals(const std::vector<uint8_t> &annexB,
		std::vector<std::vector<uint8_t>> &nalUnits) {
	size_t i = 0;
	while (i < annexB.size()) {
		size_t sc = annexBStartCodeSize(annexB.data(), annexB.size(), i);
		if (sc == 0) {
			++i;
			continue;
		}
		i += sc;
		size_t nalStart = i;
		while (i < annexB.size()) {
			if (annexBStartCodeSize(annexB.data(), annexB.size(), i) > 0)
				break;
			++i;
		}
		if (nalStart < i) {
			nalUnits.emplace_back(annexB.begin() + nalStart,
					annexB.begin() + i);
		}
	}
}

// ---------------------------------------------------------------------------
// Shared V4L2 + x264 capture pipeline
// ---------------------------------------------------------------------------

struct V4l2RtspPipeline {
	V4L2Capture capture;
	X264Encoder *encoder = nullptr;
	int fps;
	unsigned frameDurationUs;
	int bitrateKbps;
	bool streaming = false;
	int m_venc_w = 0;
	int m_venc_h = 0;

	std::deque<std::vector<uint8_t>> nalQueue;

	V4l2RtspPipeline(const char *device, uint32_t width, uint32_t height,
			int fpsIn, int bitrateIn, uint32_t pixelformat = 0) :
			capture(device, width, height, pixelformat), fps(fpsIn),
			frameDurationUs(static_cast<unsigned>(1000000 / (fpsIn > 0 ? fpsIn : 30))),
			bitrateKbps(bitrateIn) { }

	~V4l2RtspPipeline() { delete encoder; }

	bool init(double venc_scale = 0.0) {
		if (!capture.init()) {
			log_e("Failed init capture\n");
			return false;
		}

		if (venc_scale != 0.0) {
			this->m_venc_w = (int)(venc_scale * capture.width());
			this->m_venc_h = (int)(venc_scale * capture.height());
		} else {
			this->m_venc_w = capture.width();
			this->m_venc_h = capture.height();
		}
		log_d("venc %ux%u (venc_scale %f)\n", this->m_venc_w, this->m_venc_h,
				venc_scale);

		encoder = new X264Encoder(this->m_venc_w, this->m_venc_h, fps,
				bitrateKbps);
		if (!encoder->init()) {
			log_e("Failed init encoder\n");
			delete encoder;
			encoder = nullptr;
			return false;
		}
		if (!capture.startStreaming()) {
			log_e("Failed start capture: %s\n", strerror(errno));
			return false;
		}
		streaming = true;
		return true;
	}

	void stop() {
		if (streaming) {
			capture.stopStreaming();
			streaming = false;
		}
	}
};

static V4l2RtspPipeline* gPipeline = nullptr;

// ---------------------------------------------------------------------------
// Live555 FramedSource: one discrete NAL unit per delivered frame
// ---------------------------------------------------------------------------

class V4l2H264FramedSource : public FramedSource {
public:
	size_t maxNalSize = 0;

	static V4l2H264FramedSource* createNew(UsageEnvironment &env) {
		return new V4l2H264FramedSource(env);
	}

protected:
	V4l2H264FramedSource(UsageEnvironment &env) : FramedSource(env),
			fHaveStartedReading(False) {
		gettimeofday(&fNextPresentTime, nullptr);
	}

	virtual ~V4l2H264FramedSource() {
		envir().taskScheduler().turnOffBackgroundReadHandling(
				gPipeline->capture.m_fd);
	}

	virtual unsigned maxFrameSize() const {
		return 500000;
	}

private:
	virtual void doGetNextFrame() {
		if (!gPipeline || !gPipeline->streaming) {
			handleClosure();
			return;
		}

		if (!fHaveStartedReading) {
			envir().taskScheduler().turnOnBackgroundReadHandling(
					gPipeline->capture.m_fd,
					(TaskScheduler::BackgroundHandlerProc*)&v4l2ReadableHandler,
					this);
			fHaveStartedReading = True;
		}

		if (!deliverQueuedNal()) {
			// Wait for v4l2ReadableHandler to call doGetNextFrame again.
		}
	}

	std::vector<uint8_t> i420_buf;

	static void v4l2ReadableHandler(V4l2H264FramedSource *source,
			int /*mask*/) {
		if (!gPipeline || !gPipeline->streaming) return;

		int bufIndex = -1;
		if (!gPipeline->capture.dequeueBuffer(bufIndex)) return;

		const auto &buffers = gPipeline->capture.buffers();
		const V4L2Buffer &buf = buffers[bufIndex];
		const uint8_t *data = static_cast<const uint8_t*>(buf.start);

		uint32_t width = gPipeline->capture.width();
		uint32_t height = gPipeline->capture.height();
		uint32_t stride = width * 2;

		uint32_t encWidth = gPipeline->m_venc_w;
		uint32_t encHeight = gPipeline->m_venc_h;
		uint32_t encWh = encWidth * encHeight;
		size_t i420_sz = encWh * 3 / 2;

		log_d("get data %d, %ux%u -> %ux%u\n",
				(int )buf.length, width, height, encWidth, encHeight);

		static int cnt = 1;
		do {
			const char *filepath = "/media/dw/imx219.raw";
			int len;

			if (cnt <= 0) break;
			log_d("writing file (%s)\n", filepath);
			cnt--;
			if ((len = aloe_bio_write_fn(filepath, data, (size_t)buf.length,
					O_WRONLY | O_CREAT | O_APPEND)) != buf.length) {
				log_e("incomplete write: %d/%d\n", len, (int )buf.length);
				cnt = 0;
				break;
			}
			log_d("written file, %d more\n", cnt);
		} while (0);

		std::chrono::steady_clock::time_point t1;
		std::chrono::milliseconds td1;

#if 1
		if (gPipeline->capture.m_pixelformat == V4L2_PIX_FMT_SRGGB10) {
			size_t srcsz = height * stride;
			if (buf.length < srcsz) {
				log_e("unexpect size\n");
				gPipeline->capture.enqueueBuffer(bufIndex);
				return;
			}
			std::vector<uint8_t> &i420_buf = source->i420_buf;
			if (i420_buf.size() < i420_sz) {
				log_d("resize i420_buf %zu -> %zu\n", i420_buf.size(), i420_sz);
				i420_buf.resize(i420_sz);
			}
#  if 1
			const uint16_t *rg10 = static_cast<const uint16_t*>(buf.start);
			t1 = std::chrono::steady_clock::now();
			if (encWidth == width / 4 && encHeight == height / 4) {
				aloe_rg10_rgb8_i420_v5(width, height, stride, rg10, NULL,
						i420_buf.data());
			} else if (encWidth == width / 2 && encHeight == height / 2) {
				aloe_rg10_rgb8_i420_v4(width, height, stride, rg10, NULL,
						i420_buf.data());
			}
			td1 = std::chrono::duration_cast<std::chrono::milliseconds>(
					std::chrono::steady_clock::now() - t1);
			log_d("rggb10 to i420 cost %llu milliseconds\n",
					(unsigned long long )td1.count());
#  endif
			data = i420_buf.data();
		}
#endif

		bool do_encode = true;

		// do_encode = false;

		if (do_encode) {
			const uint8_t *yPlane = data;
			const uint8_t *uPlane = data + encWh;
			const uint8_t *vPlane = uPlane + encWh / 4;

			std::vector<uint8_t> annexB;
			t1 = std::chrono::steady_clock::now();
			int encSize = gPipeline->encoder->encode(yPlane, uPlane, vPlane,
					encWidth, annexB);
			td1 = std::chrono::duration_cast<std::chrono::milliseconds>(
					std::chrono::steady_clock::now() - t1);
			log_d("x264 encode cost %llu milliseconds\n",
					(unsigned long long )td1.count());

			gPipeline->capture.enqueueBuffer(bufIndex);

			if (encSize > 0 && !annexB.empty()) {
				std::vector<std::vector<uint8_t>> nalUnits;
				splitAnnexBNals(annexB, nalUnits);
				for (auto &nal : nalUnits) {
					if (nal.size() > source->maxNalSize) source->maxNalSize =
							nal.size();
					gPipeline->nalQueue.push_back(std::move(nal));
				}
				int drain_cnt = 0;
				while (gPipeline->nalQueue.size() > 120) {
					drain_cnt++;
					gPipeline->nalQueue.pop_front();
				}
				if (drain_cnt > 0) {
					log_d("drain_cnt: %d\n", drain_cnt);
				}
			}
		} else {
			gPipeline->capture.enqueueBuffer(bufIndex);
		}

		if (source->isCurrentlyAwaitingData()) {
			source->doGetNextFrame();
		}
	}

	Boolean deliverQueuedNal() {
		if (gPipeline->nalQueue.empty()) return False;

		std::vector<uint8_t> nal = std::move(gPipeline->nalQueue.front());
		gPipeline->nalQueue.pop_front();

		if (nal.size() > fMaxSize) {
			fFrameSize = fMaxSize;
			fNumTruncatedBytes = static_cast<unsigned>(nal.size() - fMaxSize);
			log_e("truncated NAL %u bytes (nal %zu, max %u)\n",
					fNumTruncatedBytes, nal.size(), fMaxSize);
		} else {
			fFrameSize = static_cast<unsigned>(nal.size());
			fNumTruncatedBytes = 0;
		}

		memmove(fTo, nal.data(), fFrameSize);

		fPresentationTime = fNextPresentTime;
		fDurationInMicroseconds = gPipeline->frameDurationUs;
		fNextPresentTime.tv_usec += gPipeline->frameDurationUs;
		fNextPresentTime.tv_sec += fNextPresentTime.tv_usec / 1000000;
		fNextPresentTime.tv_usec %= 1000000;

		FramedSource::afterGetting(this);
		return True;
	}

private:
	Boolean fHaveStartedReading;
	struct timeval fNextPresentTime;
};

// ---------------------------------------------------------------------------
// RTSP ServerMediaSubsession (discrete framer)
// ---------------------------------------------------------------------------

class H264V4l2ServerMediaSubsession: public OnDemandServerMediaSubsession {
public:
	static H264V4l2ServerMediaSubsession* createNew(UsageEnvironment &env,
			Boolean reuseFirstSource,
			unsigned bitrateKbps) {
		return new H264V4l2ServerMediaSubsession(env, reuseFirstSource,
				bitrateKbps);
	}

protected:
	H264V4l2ServerMediaSubsession(UsageEnvironment &env,
			Boolean reuseFirstSource,
			unsigned bitrateKbps)
	: OnDemandServerMediaSubsession(env, reuseFirstSource),
			fBitrateKbps(bitrateKbps),
			fAuxSDPLine(nullptr) {
	}

	virtual ~H264V4l2ServerMediaSubsession() {
		delete[] fAuxSDPLine;
	}

	virtual FramedSource* createNewStreamSource(unsigned /*clientSessionId*/,
			unsigned &estBitrate) {
		estBitrate = fBitrateKbps;

		FramedSource *source = V4l2H264FramedSource::createNew(envir());
		return H264VideoStreamDiscreteFramer::createNew(envir(), source);
	}

	virtual RTPSink* createNewRTPSink(Groupsock *rtpGroupsock,
			unsigned char rtpPayloadTypeIfDynamic,
			FramedSource* /*inputSource*/) {
		size_t spsSize = 0, ppsSize = 0;
		gPipeline->encoder->getSPS(spsSize, ppsSize);
		return H264VideoRTPSink::createNew(
				envir(), rtpGroupsock, rtpPayloadTypeIfDynamic,
				gPipeline->encoder->getSPSData(),
				static_cast<unsigned>(spsSize),
				gPipeline->encoder->getPPSData(),
				static_cast<unsigned>(ppsSize));
	}

	virtual char const* getAuxSDPLine(RTPSink *rtpSink,
			FramedSource* /*inputSource*/) {
		if (fAuxSDPLine != nullptr) return fAuxSDPLine;
		if (rtpSink == nullptr) return nullptr;

		char const *dasl = rtpSink->auxSDPLine();
		if (dasl != nullptr) fAuxSDPLine = strDup(dasl);
		return fAuxSDPLine;
	}

private:
	unsigned fBitrateKbps;
	char *fAuxSDPLine;
};

// ---------------------------------------------------------------------------
// RTSP server helpers
// ---------------------------------------------------------------------------

static void announceURL(RTSPServer *rtspServer, ServerMediaSession *sms) {
	if (rtspServer == nullptr || sms == nullptr) return;

	UsageEnvironment &env = rtspServer->envir();

	env << "Play this stream using the URL ";
	if (weHaveAnIPv4Address(env)) {
		char *url = rtspServer->ipv4rtspURL(sms);
		env << "\"" << url << "\"";
		delete[] url;
		if (weHaveAnIPv6Address(env)) env << " or ";
	}
	if (weHaveAnIPv6Address(env)) {
		char *url = rtspServer->ipv6rtspURL(sms);
		env << "\"" << url << "\"";
		delete[] url;
	}
	env << "\n";
}

static void usage(const char *prog) {
	fprintf(stderr,
"Usage: %s <device> [width] [height] [fps] [bitrate_kbps] [rtsp_port]\n"
"  device        V4L2 video device (e.g. /dev/video0)\n"
"  width         Frame width  (default: 1920)\n"
"  height        Frame height (default: 1080)\n"
"  fps           Frames per second (default: 30)\n"
"  bitrate_kbps  Target bitrate in kbps (default: 2000)\n"
"  rtsp_port     RTSP server port (default: 8554)\n",
			prog);
	exit(1);
}

static const char opt_short[] = "hv";
enum {
	opt_key_reflags = 0x201,
	opt_key_help,
	opt_key_vcap_device,
	opt_key_vcap_width,
	opt_key_vcap_height,
	opt_key_vcap_pixelformat,
	opt_key_vcap_fps,
	opt_key_venc_kbps,
	opt_key_rtsp_port,
	opt_key_app_preset,
	opt_key_max
};

static struct option opt_long[] = {
	{"help", no_argument, NULL, 'h'},
	{"verbose", no_argument, NULL, 'v'},
	{"device", required_argument, NULL, opt_key_vcap_device},
	{"width", required_argument, NULL, opt_key_vcap_width},
	{"height", required_argument, NULL, opt_key_vcap_height},
	{"pixelformat", required_argument, NULL, opt_key_vcap_pixelformat},
	{"fps", required_argument, NULL, opt_key_vcap_fps},
	{"kbps", required_argument, NULL, opt_key_venc_kbps},
	{"port", required_argument, NULL, opt_key_rtsp_port},
	{"preset", required_argument, NULL, opt_key_app_preset},
	{0},
};

static void help(int argc, const char **argv) {
	int i;
	char fourcc_str[5];

//	dump_argv(argc, argv)
	fprintf(stdout,
"COMMAND\n"
"    %s [OPTIONS] [APPLET]\n"
"\n"
"OPTIONS\n"
"    -h, --help          Show help\n"
"    -v, --verbose       Verbose output (default mimic debug and more)\n"
"    --device=<DEV>      Video capture device (default: %s)\n"
"    --width=<WIDTH>     Video capture width (default: %d)\n"
"    --height=<HEIGHT>   Video capture height (default: %d)\n"
"    --fps=<FPS>         Video capture fps (default: %d)\n"
"    --pixelformat=<4CC> Video capture pixel format (default: %s)\n"
"    --kbps=<BITRATE>    Video encoder kbps (default: %d)\n"
"    --port=<PORT>       RTSP port (default: %d)\n"
"    --preset=<PRESET>   Preset (default: None)\n"
"\n", ((argc > 0) && argv && argv[0] ? argv[0] : "Program"),
			vcap_device, vcap_width, vcap_height, vcap_fps,
			aloe_fourcc_str(fourcc_str, sizeof(fourcc_str), vcap_pixelformat),
			venc_kbps, rtsp_port);

	if (verbose_level >= VERBOSE_LEVEL_DEBUG) {
		fprintf(stdout,
"Description:\n"
"    Preset: imx219\n"
"      --width=3280 --height=2464 --pixelformat=RG10 --fps=15\n"
"\n"
				);
	}
}

static int live555_main(int argc, char** argv) {
	int ret = -1, opt_op, opt_idx, i, opt_exit = 0;
	struct {
		unsigned opt_help: 1;
	} opts = {};
	char pixelformat_str[2][16];

	optind = 0;
	while ((opt_op = getopt_long(argc, (char* const*)argv, opt_short, opt_long,
			&opt_idx)) != -1) {
		if (opt_op == 'h') {
			opts.opt_help = 1;
			continue;
		}
		if (opt_op == 'v') {
			if (verbose_level < VERBOSE_LEVEL_VERBOSE) verbose_level++;
			continue;
		}
		if (opt_op == opt_key_app_preset) {
			if (strcasecmp(optarg, "imx219") == 0) {
				vcap_width=3280;
				vcap_height=2464;
				vcap_pixelformat=aloe_fourcc_val("RG10");
				vcap_fps=15;
				log_d("Preset imx219: width: %d, height %d, pixelformat: %s, fps: %d\n",
						vcap_width, vcap_height,
						aloe_fourcc_str(pixelformat_str[0],
								sizeof(pixelformat_str[0]), vcap_pixelformat),
						vcap_fps);
				continue;
			}
			log_e("Invalid prefix: %s\n", optarg);
			return 1;
		}
		if (opt_op == opt_key_vcap_device) {
			vcap_device = optarg;
			continue;
		}
		if (opt_op == opt_key_vcap_width) {
			vcap_width = strtol(optarg, NULL, 0);
			continue;
		}
		if (opt_op == opt_key_vcap_height) {
			vcap_height = strtol(optarg, NULL, 0);
			continue;
		}
		if (opt_op == opt_key_vcap_fps) {
			vcap_fps = strtol(optarg, NULL, 0);
			continue;
		}
		if (opt_op == opt_key_vcap_pixelformat) {
			vcap_pixelformat = aloe_fourcc_val(optarg);
			continue;
		}
		if (opt_op == opt_key_venc_kbps) {
			venc_kbps = strtol(optarg, NULL, 0);
			continue;
		}
		if (opt_op == opt_key_rtsp_port) {
			rtsp_port = strtol(optarg, NULL, 0);
			continue;
		}
	}

//	if (optind < argc) dump_argv(argc - optind, &argv[optind]);
	if (opts.opt_help) {
		help(argc, (const char**)argv);
		return 1;
	}
	const char *device = vcap_device;
	uint32_t width = (uint32_t)vcap_width;
	uint32_t height = (uint32_t)vcap_height;
	int fps = vcap_fps;
	int bitrateKbps = venc_kbps;
	portNumBits rtspPort = (portNumBits)rtsp_port;
	uint32_t pixelformat = vcap_pixelformat;

	printf("Device: %s\n", device);
	printf("Resolution: %ux%u @ %d fps, %d kbps\n", width, height, fps,
			bitrateKbps);
	printf("Pixel format: %s\n", aloe_fourcc_str(pixelformat_str[0],
			sizeof(pixelformat_str[0]), pixelformat));

#if defined(RG10_RGB_DOWNSCALE)
	printf("RG10_RGB_DOWNSCALE: %d\n", RG10_RGB_DOWNSCALE);
#endif
	printf("RTSP port: %u\n", rtspPort);

	V4l2RtspPipeline pipeline(device, width, height, fps, bitrateKbps,
			pixelformat);
	double venc_scale = 0.0;
#if defined(RG10_RGB_DOWNSCALE) && RG10_RGB_DOWNSCALE
	if (pixelformat == fourcc_rg10) venc_scale = 1.0 / RG10_RGB_DOWNSCALE;
#endif
	if (!pipeline.init(venc_scale)) {
		fprintf(stderr, "Failed to initialize V4L2/x264 pipeline\n");
		return 1;
	}

	uint32_t actualWidth = pipeline.capture.width();
	uint32_t actualHeight = pipeline.capture.height();
	if (actualWidth != width || actualHeight != height) {
		printf("Device adjusted resolution to %ux%u\n", actualWidth,
				actualHeight);
	}

	actualWidth = pipeline.m_venc_w;
	actualHeight = pipeline.m_venc_h;
	if (actualWidth != width || actualHeight != height) {
		printf("Encoder input resolution is %ux%u\n", actualWidth,
				actualHeight);
	}

	gPipeline = &pipeline;

	OutPacketBuffer::increaseMaxSizeTo(600000);
	OutPacketBuffer::increaseMaxSizeTo(1800000);

	TaskScheduler *scheduler = BasicTaskScheduler::createNew();
	UsageEnvironment *env = BasicUsageEnvironment::createNew(*scheduler);

	RTSPServer *rtspServer = RTSPServer::createNew(*env, rtspPort);
	if (rtspServer == nullptr) {
		*env << "Failed to create RTSP server: " << env->getResultMsg() << "\n";
		pipeline.stop();
		return 1;
	}

	char const *streamName = "h264";
	char const *descriptionString =
			"Session streamed by \"tester_v4l2rtsp2\" (live V4L2 H.264, discrete NAL)";

	ServerMediaSession *sms = ServerMediaSession::createNew(*env, streamName,
			streamName, descriptionString);
	sms->addSubsession(H264V4l2ServerMediaSubsession::createNew(*env, True,
			static_cast<unsigned>(bitrateKbps)));
	rtspServer->addServerMediaSession(sms);

	*env << "\n\"" << streamName << "\" stream, live from V4L2 device \""
			<< device << "\" (H264VideoStreamDiscreteFramer)\n";
	announceURL(rtspServer, sms);

	char const *httpProtocolStr = "HTTP";
	if (rtspServer->setUpTunnelingOverHTTP(80)
			|| rtspServer->setUpTunnelingOverHTTP(8000)
			|| rtspServer->setUpTunnelingOverHTTP(8080)) {
		*env << "\n(We use port " << rtspServer->httpServerPortNum()
				<< " for optional RTSP-over-" << httpProtocolStr
				<< " tunneling.)\n";
	} else {
		*env << "\n(RTSP-over-" << httpProtocolStr
				<< " tunneling is not available.)\n";
	}

	*env << "RTSP streaming started. Press Ctrl+C to stop.\n";
	env->taskScheduler().doEventLoop();

	pipeline.stop();
	return 0;
}

int main(int argc, char **argv) {
//  dump_argv(argc, argv);
	return live555_main(argc, argv);
}
