#include "v4l2_capture.h"
#include "x264_encoder.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <sys/select.h>

static void usage(const char* prog) {
  fprintf(stderr,
          "Usage: %s <device> <output.h264> [width] [height] [fps] [bitrate_kbps]\n"
          "  device        V4L2 video device (e.g. /dev/video0)\n"
          "  output.h264   Output file path\n"
          "  width         Frame width  (default: 1920)\n"
          "  height        Frame height (default: 1080)\n"
          "  fps           Frames per second (default: 30)\n"
          "  bitrate_kbps  Target bitrate in kbps (default: 2000)\n", prog);
  exit(1);
}

// Write raw SPS and PPS NAL units to the output file so decoders can initialize
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

int main(int argc, char** argv) {
  if (argc < 3) {
    usage(argv[0]);
  }

  const char* device     = argv[1];
  const char* outFile    = argv[2];
  uint32_t   width       = (argc > 3) ? static_cast<uint32_t>(atoi(argv[3])) : 1920;
  uint32_t   height      = (argc > 4) ? static_cast<uint32_t>(atoi(argv[4])) : 1080;
  int        fps         = (argc > 5) ? atoi(argv[5]) : 30;
  int        bitrateKbps = (argc > 6) ? atoi(argv[6]) : 2000;

  printf("Device: %s\n", device);
  printf("Output: %s\n", outFile);
  printf("Resolution: %ux%u @ %d fps, %d kbps\n", width, height, fps, bitrateKbps);

  // ---- 1. Open and init V4L2 capture ----
  V4L2Capture capture(device, width, height);
  if (!capture.init()) {
    fprintf(stderr, "Failed to initialize V4L2 capture\n");
    return 1;
  }

  uint32_t actualWidth  = capture.width();
  uint32_t actualHeight = capture.height();

  if (actualWidth != width || actualHeight != height) {
    printf("Device adjusted resolution to %ux%u\n", actualWidth, actualHeight);
  }

  // ---- 2. Open and init x264 encoder ----
  X264Encoder encoder(actualWidth, actualHeight, fps, bitrateKbps);
  if (!encoder.init()) {
    fprintf(stderr, "Failed to initialize x264 encoder\n");
    return 1;
  }

  // ---- 3. Open output file ----
  FILE* fp = fopen(outFile, "wb");
  if (!fp) {
    fprintf(stderr, "Failed to open output file %s: %s\n", outFile, strerror(errno));
    return 1;
  }

  // Write SPS/PPS headers at the beginning of the file
  if (!writeHeaders(fp, encoder)) {
    fprintf(stderr, "Failed to write headers to output file\n");
    fclose(fp);
    return 1;
  }

  // ---- 4. Start streaming ----
  if (!capture.startStreaming()) {
    fprintf(stderr, "Failed to start streaming\n");
    fclose(fp);
    return 1;
  }

  printf("Streaming started. Press Ctrl+C to stop.\n");

  const auto& buffers = capture.buffers();
  int frameCount = 0;

  // ---- 5. Capture loop ----
  while (true) {
    // Wait for the device to be readable (frame available)
    fd_set read_fds;
    FD_ZERO(&read_fds);
    FD_SET(capture.m_fd >= 0 ? capture.m_fd : -1, &read_fds);

    struct timeval timeout;
    timeout.tv_sec  = 1;
    timeout.tv_usec = 0;

    int ret = select(capture.m_fd + 1, &read_fds, nullptr, nullptr, &timeout);
    if (ret < 0) {
      if (errno == EINTR) continue;
      fprintf(stderr, "select() failed: %s\n", strerror(errno));
      break;
    }
    if (ret == 0) {
      // Timeout, no frame ready
      continue;
    }

    // Dequeue a filled buffer
    int bufIndex = -1;
    if (!capture.dequeueBuffer(bufIndex)) {
      fprintf(stderr, "Failed to dequeue buffer\n");
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
    int encSize = encoder.encode(yPlane, uPlane, vPlane, stride, encodedData);

    if (encSize < 0) {
      fprintf(stderr, "Encode failed on frame %d\n", frameCount);
      capture.enqueueBuffer(bufIndex);
      break;
    }

    // Write encoded data to file (x264 with b_annexb=1 already includes start codes)
    if (!encodedData.empty()) {
      if (fwrite(encodedData.data(), 1, encodedData.size(), fp) != encodedData.size()) {
        fprintf(stderr, "Failed to write frame %d to file\n", frameCount);
        capture.enqueueBuffer(bufIndex);
        break;
      }
    }

    // Requeue the buffer for next capture
    capture.enqueueBuffer(bufIndex);
    frameCount++;
  }

  // ---- 6. Cleanup ----
  capture.stopStreaming();
  fflush(fp);
  fclose(fp);

  printf("Encoded %d frames to %s\n", frameCount, outFile);
  return 0;
}
