#ifndef V4L2_CAPTURE_H
#define V4L2_CAPTURE_H

#include <linux/videodev2.h>
#include <cstddef>
#include <cstdint>
#include <vector>

#define aloe_fourcc_valid1(_v) ( \
		((_v) >= 'A' && (_v) <= 'Z') \
		|| ((_v) >= 'a' && (_v) <= 'z') \
		|| ((_v) >= '0' && (_v) <= '9') \
		|| ((_v) == ' ') \
)

#define aloe_fourcc_valid(_v) ( \
		aloe_fourcc_valid1   (((_v) >>  0) & 0xff) \
		&& aloe_fourcc_valid1(((_v) >>  8) & 0xff) \
		&& aloe_fourcc_valid1(((_v) >> 16) & 0xff) \
		&& aloe_fourcc_valid1(((_v) >> 24) & 0xff) \
)

struct V4L2Buffer {
  void* start;
  size_t length;
};

class V4L2Capture {
public:
  V4L2Capture(const char* device, uint32_t width, uint32_t height,
		  uint32_t pixelformat = 0);
  ~V4L2Capture();

  bool init();
  bool startStreaming();
  void stopStreaming();
  bool dequeueBuffer(int& index);
  bool enqueueBuffer(int index);

  uint32_t width()  const { return m_width;  }
  uint32_t height() const { return m_height; }
  const std::vector<V4L2Buffer>& buffers() const { return m_buffers; }

public:
  bool openDevice(const char* device);
  bool queryCapabilities();
  bool configureFormat2(uint32_t width, uint32_t height, uint32_t pixelformat);
  bool configureFormat(uint32_t width, uint32_t height);
  bool allocateBuffers(uint32_t count = 4);
  bool initMMap();

  int m_fd = -1;
  uint32_t m_width;
  uint32_t m_height;
  std::vector<V4L2Buffer> m_buffers;
  uint32_t m_pixelformat;
};

#endif // V4L2_CAPTURE_H
