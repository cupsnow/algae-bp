/* $Id$
 *
 * SPDX-License-Identifier: MIT
 *
 * @author joelai
 *
 * @file /algae-bp/package/tester1/tester_v4l2rtsp2.cpp
 * @brief tester_v4l2rtsp2 - serve h264 rtsp streaming from v4l2
 *
 * Combines v4l2 capture, x264 encoding, and live555 RTSP streaming.
 * Usage: tester_v4l2rtsp2 <device> [width] [height] [fps] [bitrate_kbps] [rtsp_port]
 *        device        V4L2 video device (e.g. /dev/video0)
 *        width         Frame width  (default: 1920)
 *        height        Frame height (default: 1080)
 *        fps           Frames per second (default: 30)
 *        bitrate_kbps  Target bitrate in kbps (default: 2000)
 *        rtsp_port     RTSP server port (default: 8554)
 */

#include <liveMedia.hh>
#include <BasicUsageEnvironment.hh>
#include <GroupsockHelper.hh>

#include "v4l2_capture.h"
#include "x264_encoder.h"
#include "priv.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <sys/select.h>
#include <pthread.h>
#include <queue>
#include <mutex>
#include <atomic>

// H.264 NAL unit frame from encoder
struct EncodedFrame {
	std::vector<uint8_t> data;
	uint64_t timestamp;
};

// Thread-safe queue for encoded frames
class EncodedFrameQueue {
public:
	void push(const EncodedFrame& frame) {
		std::lock_guard<std::mutex> lock(m_mutex);
		m_queue.push(frame);
	}

	bool pop(EncodedFrame& frame) {
		std::lock_guard<std::mutex> lock(m_mutex);
		if (m_queue.empty()) return false;
		frame = m_queue.front();
		m_queue.pop();
		return true;
	}

	bool empty() {
		std::lock_guard<std::mutex> lock(m_mutex);
		return m_queue.empty();
	}

private:
	std::queue<EncodedFrame> m_queue;
	std::mutex m_mutex;
};

// Global frame queue and encoder state
static EncodedFrameQueue g_frameQueue;
static std::atomic<bool> g_captureRunning(false);
static V4L2Capture* g_capture = nullptr;
static X264Encoder* g_encoder = nullptr;

// Capture thread function
static void* captureThread(void* arg) {
	if (!g_capture || !g_encoder) return nullptr;

	const auto& buffers = g_capture->buffers();
	uint32_t actualWidth = g_capture->width();
	uint32_t actualHeight = g_capture->height();
	uint64_t frameCount = 0;
	uint64_t timestampBase = 0;
	int frameInterval = 1; // for fps calculation

	log_d("Capture thread started\n");

	while (g_captureRunning) {
		// Wait for the device to be readable (frame available)
		fd_set read_fds;
		FD_ZERO(&read_fds);
		FD_SET(g_capture->m_fd, &read_fds);

		struct timeval timeout;
		timeout.tv_sec = 1;
		timeout.tv_usec = 0;

		int ret = select(g_capture->m_fd + 1, &read_fds, nullptr, nullptr, &timeout);
		if (ret < 0) {
			if (errno == EINTR) continue;
			log_e("select() failed: %s\n", strerror(errno));
			break;
		}
		if (ret == 0) {
			// Timeout, no frame ready
			continue;
		}

		// Dequeue a filled buffer
		int bufIndex = -1;
		if (!g_capture->dequeueBuffer(bufIndex)) {
			log_e("Failed to dequeue buffer\n");
			break;
		}

		const V4L2Buffer& buf = buffers[bufIndex];
		const uint8_t* data = static_cast<const uint8_t*>(buf.start);

		// YUV420 planar: Y plane, then U plane, then V plane
		uint32_t stride = actualWidth;
		const uint8_t* yPlane = data;
		const uint8_t* uPlane = data + stride * actualHeight;
		const uint8_t* vPlane = uPlane + (stride / 2) * (actualHeight / 2);

		// Encode the frame
		std::vector<uint8_t> encodedData;
		int encSize = g_encoder->encode(yPlane, uPlane, vPlane, stride, encodedData);

		if (encSize < 0) {
			log_e("Encode failed on frame %llu\n", (unsigned long long)frameCount);
		} else if (encSize > 0) {
			// Queue the encoded frame for streaming
			EncodedFrame frame;
			frame.data = encodedData;
			frame.timestamp = frameCount * frameInterval;
			g_frameQueue.push(frame);
		}

		// Requeue the buffer for next capture
		g_capture->enqueueBuffer(bufIndex);
		frameCount++;
	}

	log_d("Capture thread exiting after %llu frames\n", (unsigned long long)frameCount);
	return nullptr;
}

// Custom FramedSource for live555 that feeds H.264 data
class V4L2H264Source : public FramedSource {
public:
	static V4L2H264Source* createNew(UsageEnvironment& env) {
		return new V4L2H264Source(env);
	}

protected:
	V4L2H264Source(UsageEnvironment& env)
		: FramedSource(env), m_frameCount(0), m_sendSPS(true), m_sendPPS(false) {
		// Get SPS/PPS for initialization
		size_t spsSize = 0, ppsSize = 0;
		g_encoder->getSPS(spsSize, ppsSize);

		const uint8_t* spsData = g_encoder->getSPSData();
		const uint8_t* ppsData = g_encoder->getPPSData();

		// Store SPS/PPS for initial transmission
		if (spsSize > 0) {
			m_spsData.insert(m_spsData.end(), spsData, spsData + spsSize);
		}
		if (ppsSize > 0) {
			m_ppsData.insert(m_ppsData.end(), ppsData, ppsData + ppsSize);
		}

		// Initialize timestamp
		gettimeofday(&m_lastPresentationTime, nullptr);

		log_d("V4L2H264Source created: SPS=%zu, PPS=%zu\n", spsSize, ppsSize);
	}

	virtual ~V4L2H264Source() {
		log_d("V4L2H264Source destroyed\n");
	}

	virtual void doGetNextFrame() {
		// Try to get an encoded frame from the queue
		EncodedFrame frame;

		// Send SPS first on initial connection
		if (m_sendSPS && !m_spsData.empty()) {
			uint8_t startCode[] = {0x00, 0x00, 0x00, 0x01};
			if (m_spsData.size() + 4 <= fMaxSize) {
				memcpy(fTo, startCode, 4);
				memcpy(fTo + 4, m_spsData.data(), m_spsData.size());
				fFrameSize = m_spsData.size() + 4;
				fPresentationTime = m_lastPresentationTime;
				m_sendSPS = false;
				m_sendPPS = true;
				afterGetting(this);
				return;
			}
		}

		// Send PPS second
		if (m_sendPPS && !m_ppsData.empty()) {
			uint8_t startCode[] = {0x00, 0x00, 0x00, 0x01};
			if (m_ppsData.size() + 4 <= fMaxSize) {
				memcpy(fTo, startCode, 4);
				memcpy(fTo + 4, m_ppsData.data(), m_ppsData.size());
				fFrameSize = m_ppsData.size() + 4;
				fPresentationTime = m_lastPresentationTime;
				m_sendPPS = false;
				afterGetting(this);
				return;
			}
		}

		// Try to get next video frame from queue
		if (g_frameQueue.pop(frame)) {
			if (frame.data.size() <= fMaxSize) {
				memcpy(fTo, frame.data.data(), frame.data.size());
				fFrameSize = frame.data.size();

				// Update timestamp
				gettimeofday(&fPresentationTime, nullptr);
				m_lastPresentationTime = fPresentationTime;

				afterGetting(this);
				return;
			} else {
				log_e("Frame too large: %zu > %u\n", frame.data.size(), fMaxSize);
			}
		}

		// No frame available, reschedule to try again later (10ms for responsive streaming)
		nextTask() = envir().taskScheduler().scheduleDelayedTask(
			10000,  // 10ms delay
			(TaskFunc*)FramedSource::handleClosure, this);
	}

	virtual void doStopGettingFrames() {
		// Cleanup if needed
		FramedSource::doStopGettingFrames();
	}

private:
	std::vector<uint8_t> m_spsData;
	std::vector<uint8_t> m_ppsData;
	struct timeval m_lastPresentationTime;
	int m_frameCount;
	bool m_sendSPS;
	bool m_sendPPS;
};

// Custom H.264 ServerMediaSubsession for live V4L2 streaming
class V4L2H264ServerMediaSubsession : public ServerMediaSubsession {
public:
	static V4L2H264ServerMediaSubsession* createNew(UsageEnvironment& env) {
		return new V4L2H264ServerMediaSubsession(env);
	}

protected:
	V4L2H264ServerMediaSubsession(UsageEnvironment& env)
		: ServerMediaSubsession(env), m_estimatedBitrate(2000) { }

	virtual ~V4L2H264ServerMediaSubsession() { }

	virtual FramedSource* createNewStreamSource(unsigned /*clientSessionId*/,
			unsigned& estBitrate) {
		estBitrate = m_estimatedBitrate;
		return V4L2H264Source::createNew(envir());
	}

	virtual RTPSink* createNewRTPSink(Groupsock* rtpGroupsock,
			unsigned char rtpPayloadTypeIfDynamic,
			FramedSource* inputSource) {
		// Create an H.264 RTP sink
		return H264VideoRTPSink::createNew(envir(), rtpGroupsock, rtpPayloadTypeIfDynamic);
	}

	virtual void startStream(unsigned clientSessionId, void* streamToken,
			TaskFunc* rtcpRRHandler, void* rtcpRRHandlerClientData,
			unsigned short& rtpSeqNum, unsigned int& rtpTimestamp,
			ServerRequestAlternativeByteHandler* alternativeRequestHandler,
			void* alternativeRequestClientData) {
		ServerMediaSubsession::startStream(clientSessionId, streamToken,
			rtcpRRHandler, rtcpRRHandlerClientData,
			rtpSeqNum, rtpTimestamp,
			alternativeRequestHandler, alternativeRequestClientData);
	}

private:
	unsigned m_estimatedBitrate;
};

// Signal handlers for graceful shutdown
static std::atomic<bool> g_shutdownRequested(false);

static void signalHandler(int signum) {
	log_d("Signal %d received, shutting down...\n", signum);
	g_shutdownRequested = true;
	g_captureRunning = false;
}

static void announceURL(RTSPServer *rtspServer, ServerMediaSession *sms) {
	if (rtspServer == NULL || sms == NULL) return;

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

static void announceStream(RTSPServer *rtspServer, ServerMediaSession *sms,
		char const *streamName) {
	UsageEnvironment &env = rtspServer->envir();

	env << "\n\"" << streamName << "\" stream from V4L2\n";
	announceURL(rtspServer, sms);
}

static void usage(const char* prog) {
	fprintf(stderr,
			"Usage: %s <device> [width] [height] [fps] [bitrate_kbps] [rtsp_port]\n"
			"  device        V4L2 video device (e.g. /dev/video0)\n"
			"  width         Frame width  (default: 1920)\n"
			"  height        Frame height (default: 1080)\n"
			"  fps           Frames per second (default: 30)\n"
			"  bitrate_kbps  Target bitrate in kbps (default: 2000)\n"
			"  rtsp_port     RTSP server port (default: 8554)\n", prog);
	exit(1);
}

int main(int argc, const char **argv) {
	if (argc < 2) {
		usage(argv[0]);
	}

	dump_argv(argc, argv);

	const char* device = argv[1];
	uint32_t width = (argc > 2) ? static_cast<uint32_t>(atoi(argv[2])) : 1920;
	uint32_t height = (argc > 3) ? static_cast<uint32_t>(atoi(argv[3])) : 1080;
	int fps = (argc > 4) ? atoi(argv[4]) : 30;
	int bitrateKbps = (argc > 5) ? atoi(argv[5]) : 2000;
	int rtspPort = (argc > 6) ? atoi(argv[6]) : 8554;

	log_d("Device: %s\n", device);
	log_d("Resolution: %ux%u @ %d fps, %d kbps\n", width, height, fps, bitrateKbps);
	log_d("RTSP port: %d\n", rtspPort);

	// ---- 1. Open and init V4L2 capture ----
	V4L2Capture capture(device, width, height);
	if (!capture.init()) {
		fprintf(stderr, "Failed to initialize V4L2 capture\n");
		return 1;
	}

	uint32_t actualWidth = capture.width();
	uint32_t actualHeight = capture.height();

	if (actualWidth != width || actualHeight != height) {
		log_d("Device adjusted resolution to %ux%u\n", actualWidth, actualHeight);
	}

	// ---- 2. Open and init x264 encoder ----
	X264Encoder encoder(actualWidth, actualHeight, fps, bitrateKbps);
	if (!encoder.init()) {
		fprintf(stderr, "Failed to initialize x264 encoder\n");
		return 1;
	}

	// ---- 3. Start capture streaming ----
	if (!capture.startStreaming()) {
		fprintf(stderr, "Failed to start streaming\n");
		return 1;
	}

	// Store global pointers for capture thread
	g_capture = &capture;
	g_encoder = &encoder;
	g_captureRunning = true;

	// ---- 4. Start capture thread ----
	pthread_t captureThreadId;
	if (pthread_create(&captureThreadId, nullptr, captureThread, nullptr) != 0) {
		fprintf(stderr, "Failed to create capture thread\n");
		capture.stopStreaming();
		return 1;
	}

	// ---- 5. Setup Live555 RTSP server ----
	TaskScheduler *scheduler = BasicTaskScheduler::createNew();
	UsageEnvironment *env = BasicUsageEnvironment::createNew(*scheduler);

	RTSPServer *rtspServer = RTSPServer::createNew(*env, rtspPort, nullptr);
	if (rtspServer == NULL) {
		*env << "Failed to create RTSP server: " << env->getResultMsg() << "\n";
		g_captureRunning = false;
		pthread_join(captureThreadId, nullptr);
		capture.stopStreaming();
		return 1;
	}

	// Create a ServerMediaSession for the stream
	char const *descriptionString = "Session streamed by tester_v4l2rtsp2";
	char const *streamName = "h264v4l2stream";

	ServerMediaSession *sms = ServerMediaSession::createNew(*env,
			streamName, streamName, descriptionString);

	// Add our custom V4L2 H.264 subsession
	sms->addSubsession(V4L2H264ServerMediaSubsession::createNew(*env));

	rtspServer->addServerMediaSession(sms);
	announceStream(rtspServer, sms, streamName);

	// Setup signal handlers for graceful shutdown
	signal(SIGINT, signalHandler);
	signal(SIGTERM, signalHandler);

	log_d("RTSP server running...\n");

	// Event loop
	while (!g_shutdownRequested) {
		env->taskScheduler().doEventLoop(1);  // 1 second timeout
	}

	// ---- 6. Cleanup ----
	log_d("Shutting down...\n");

	g_captureRunning = false;
	pthread_join(captureThreadId, nullptr);

	Medium::close(rtspServer);
	env->reclaim();
	delete scheduler;

	capture.stopStreaming();

	log_d("Done\n");
	return 0;
}
