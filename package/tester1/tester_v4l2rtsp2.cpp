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

#include <liveMedia.hh>
#include <BasicUsageEnvironment.hh>
#include <GroupsockHelper.hh>

#include "v4l2_capture.h"
#include "x264_encoder.h"
#include "priv.h"

// ---------------------------------------------------------------------------
// Annex-B helpers (for discrete framer input)
// ---------------------------------------------------------------------------

static size_t annexBStartCodeSize(const uint8_t* p, size_t size, size_t pos)
{
  if (pos + 4 <= size && p[pos] == 0 && p[pos + 1] == 0 &&
      p[pos + 2] == 0 && p[pos + 3] == 1) {
    return 4;
  }
  if (pos + 3 <= size && p[pos] == 0 && p[pos + 1] == 0 && p[pos + 2] == 1) {
    return 3;
  }
  return 0;
}

static void splitAnnexBNals(const std::vector<uint8_t>& annexB,
                            std::vector<std::vector<uint8_t>>& nalUnits)
{
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
      nalUnits.emplace_back(annexB.begin() + nalStart, annexB.begin() + i);
    }
  }
}

// ---------------------------------------------------------------------------
// Shared V4L2 + x264 capture pipeline
// ---------------------------------------------------------------------------

struct V4l2RtspPipeline {
  V4L2Capture capture;
  X264Encoder* encoder = nullptr;
  int fps;
  unsigned frameDurationUs;
  int bitrateKbps;
  bool streaming = false;

  std::deque<std::vector<uint8_t>> nalQueue;

  V4l2RtspPipeline(const char* device, uint32_t width, uint32_t height,
                   int fpsIn, int bitrateIn)
      : capture(device, width, height),
        fps(fpsIn),
        frameDurationUs(static_cast<unsigned>(1000000 / (fpsIn > 0 ? fpsIn : 30))),
        bitrateKbps(bitrateIn) {}

  ~V4l2RtspPipeline() { delete encoder; }

  bool init() {
    if (!capture.init()) return false;

    uint32_t w = capture.width();
    uint32_t h = capture.height();
    encoder = new X264Encoder(w, h, fps, bitrateKbps);
    if (!encoder->init()) {
      delete encoder;
      encoder = nullptr;
      return false;
    }
    if (!capture.startStreaming()) return false;
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

  static V4l2H264FramedSource* createNew(UsageEnvironment& env) {
    return new V4l2H264FramedSource(env);
  }

protected:
  V4l2H264FramedSource(UsageEnvironment& env)
      : FramedSource(env), fHaveStartedReading(False) {
    gettimeofday(&fNextPresentTime, nullptr);
  }

  virtual ~V4l2H264FramedSource() {
    envir().taskScheduler().turnOffBackgroundReadHandling(gPipeline->capture.m_fd);
  }

  virtual unsigned maxFrameSize() const { return 500000; }

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

  static void v4l2ReadableHandler(V4l2H264FramedSource* source, int /*mask*/) {
    if (!gPipeline || !gPipeline->streaming) return;

    int bufIndex = -1;
    if (!gPipeline->capture.dequeueBuffer(bufIndex)) return;

    const auto& buffers = gPipeline->capture.buffers();
    const V4L2Buffer& buf = buffers[bufIndex];
    const uint8_t* data = static_cast<const uint8_t*>(buf.start);

    uint32_t width = gPipeline->capture.width();
    uint32_t height = gPipeline->capture.height();
    uint32_t stride = width;

    const uint8_t* yPlane = data;
    const uint8_t* uPlane = data + stride * height;
    const uint8_t* vPlane = uPlane + (stride / 2) * (height / 2);

    std::vector<uint8_t> annexB;
    int encSize = gPipeline->encoder->encode(yPlane, uPlane, vPlane, stride, annexB);

    gPipeline->capture.enqueueBuffer(bufIndex);

    if (encSize > 0 && !annexB.empty()) {
      std::vector<std::vector<uint8_t>> nalUnits;
      splitAnnexBNals(annexB, nalUnits);
      for (auto& nal : nalUnits) {
        if (nal.size() > source->maxNalSize) source->maxNalSize = nal.size();
        gPipeline->nalQueue.push_back(std::move(nal));
      }
      while (gPipeline->nalQueue.size() > 120) {
        gPipeline->nalQueue.pop_front();
      }
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

class H264V4l2ServerMediaSubsession : public OnDemandServerMediaSubsession {
public:
  static H264V4l2ServerMediaSubsession* createNew(UsageEnvironment& env,
                                                   Boolean reuseFirstSource,
                                                   unsigned bitrateKbps) {
    return new H264V4l2ServerMediaSubsession(env, reuseFirstSource, bitrateKbps);
  }

protected:
  H264V4l2ServerMediaSubsession(UsageEnvironment& env, Boolean reuseFirstSource,
                                unsigned bitrateKbps)
      : OnDemandServerMediaSubsession(env, reuseFirstSource),
        fBitrateKbps(bitrateKbps),
        fAuxSDPLine(nullptr) {}

  virtual ~H264V4l2ServerMediaSubsession() { delete[] fAuxSDPLine; }

  virtual FramedSource* createNewStreamSource(unsigned /*clientSessionId*/,
                                              unsigned& estBitrate) {
    estBitrate = fBitrateKbps;

    FramedSource* source = V4l2H264FramedSource::createNew(envir());
    return H264VideoStreamDiscreteFramer::createNew(envir(), source);
  }

  virtual RTPSink* createNewRTPSink(Groupsock* rtpGroupsock,
                                    unsigned char rtpPayloadTypeIfDynamic,
                                    FramedSource* /*inputSource*/) {
    size_t spsSize = 0, ppsSize = 0;
    gPipeline->encoder->getSPS(spsSize, ppsSize);
    return H264VideoRTPSink::createNew(
        envir(), rtpGroupsock, rtpPayloadTypeIfDynamic,
        gPipeline->encoder->getSPSData(), static_cast<unsigned>(spsSize),
        gPipeline->encoder->getPPSData(), static_cast<unsigned>(ppsSize));
  }

  virtual char const* getAuxSDPLine(RTPSink* rtpSink,
                                    FramedSource* /*inputSource*/) {
    if (fAuxSDPLine != nullptr) return fAuxSDPLine;
    if (rtpSink == nullptr) return nullptr;

    char const* dasl = rtpSink->auxSDPLine();
    if (dasl != nullptr) fAuxSDPLine = strDup(dasl);
    return fAuxSDPLine;
  }

private:
  unsigned fBitrateKbps;
  char* fAuxSDPLine;
};

// ---------------------------------------------------------------------------
// RTSP server helpers
// ---------------------------------------------------------------------------

static void announceURL(RTSPServer* rtspServer, ServerMediaSession* sms) {
  if (rtspServer == nullptr || sms == nullptr) return;

  UsageEnvironment& env = rtspServer->envir();

  env << "Play this stream using the URL ";
  if (weHaveAnIPv4Address(env)) {
    char* url = rtspServer->ipv4rtspURL(sms);
    env << "\"" << url << "\"";
    delete[] url;
    if (weHaveAnIPv6Address(env)) env << " or ";
  }
  if (weHaveAnIPv6Address(env)) {
    char* url = rtspServer->ipv6rtspURL(sms);
    env << "\"" << url << "\"";
    delete[] url;
  }
  env << "\n";
}

static void usage(const char* prog) {
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

static int live555_main(int argc, char** argv) {
  if (argc < 2) usage(argv[0]);

  const char* device = argv[1];
  uint32_t width = (argc > 2) ? static_cast<uint32_t>(atoi(argv[2])) : 1920;
  uint32_t height = (argc > 3) ? static_cast<uint32_t>(atoi(argv[3])) : 1080;
  int fps = (argc > 4) ? atoi(argv[4]) : 30;
  int bitrateKbps = (argc > 5) ? atoi(argv[5]) : 2000;
  portNumBits rtspPort = (argc > 6) ? static_cast<portNumBits>(atoi(argv[6])) : 8554;

  printf("Device: %s\n", device);
  printf("Resolution: %ux%u @ %d fps, %d kbps\n", width, height, fps, bitrateKbps);
  printf("RTSP port: %u\n", rtspPort);

  V4l2RtspPipeline pipeline(device, width, height, fps, bitrateKbps);
  if (!pipeline.init()) {
    fprintf(stderr, "Failed to initialize V4L2/x264 pipeline\n");
    return 1;
  }

  uint32_t actualWidth = pipeline.capture.width();
  uint32_t actualHeight = pipeline.capture.height();
  if (actualWidth != width || actualHeight != height) {
    printf("Device adjusted resolution to %ux%u\n", actualWidth, actualHeight);
  }

  gPipeline = &pipeline;

  OutPacketBuffer::increaseMaxSizeTo(600000);

  TaskScheduler* scheduler = BasicTaskScheduler::createNew();
  UsageEnvironment* env = BasicUsageEnvironment::createNew(*scheduler);

  RTSPServer* rtspServer = RTSPServer::createNew(*env, rtspPort);
  if (rtspServer == nullptr) {
    *env << "Failed to create RTSP server: " << env->getResultMsg() << "\n";
    pipeline.stop();
    return 1;
  }

  char const* streamName = "h264";
  char const* descriptionString =
      "Session streamed by \"tester_v4l2rtsp2\" (live V4L2 H.264, discrete NAL)";

  ServerMediaSession* sms =
      ServerMediaSession::createNew(*env, streamName, streamName, descriptionString);
  sms->addSubsession(
      H264V4l2ServerMediaSubsession::createNew(*env, True,
                                               static_cast<unsigned>(bitrateKbps)));
  rtspServer->addServerMediaSession(sms);

  *env << "\n\"" << streamName << "\" stream, live from V4L2 device \""
      << device << "\" (H264VideoStreamDiscreteFramer)\n";
  announceURL(rtspServer, sms);

  char const* httpProtocolStr = "HTTP";
  if (rtspServer->setUpTunnelingOverHTTP(80) ||
      rtspServer->setUpTunnelingOverHTTP(8000) ||
      rtspServer->setUpTunnelingOverHTTP(8080)) {
    *env << "\n(We use port " << rtspServer->httpServerPortNum()
        << " for optional RTSP-over-" << httpProtocolStr << " tunneling.)\n";
  } else {
    *env << "\n(RTSP-over-" << httpProtocolStr << " tunneling is not available.)\n";
  }

  *env << "RTSP streaming started. Press Ctrl+C to stop.\n";
  env->taskScheduler().doEventLoop();

  pipeline.stop();
  return 0;
}

int main(int argc, char** argv) {
  dump_argv(argc, argv);
  return live555_main(argc, argv);
}
