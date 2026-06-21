#include "v4l2_capture.h"
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <cstring>
#include <cstdio>

V4L2Capture::V4L2Capture(const char* device, uint32_t width, uint32_t height)
  : m_width(width), m_height(height)
{
  openDevice(device);
}

V4L2Capture::~V4L2Capture()
{
  if (m_fd >= 0) {
    stopStreaming();
    for (auto& buf : m_buffers) {
      if (buf.start) munmap(buf.start, buf.length);
    }
    close(m_fd);
  }
}

bool V4L2Capture::openDevice(const char* device)
{
  m_fd = ::open(device, O_RDWR | O_NONBLOCK, 0);
  if (m_fd < 0) {
    fprintf(stderr, "Failed to open device %s\n", device);
  }
  return m_fd >= 0;
}

bool V4L2Capture::init()
{
  if (!queryCapabilities()) {
    fprintf(stderr, "Device does not support video capture\n");
    return false;
  }
  if (!configureFormat(m_width, m_height)) {
    fprintf(stderr, "Failed to configure YU12 format %ux%u\n", m_width, m_height);
    return false;
  }
  if (!allocateBuffers()) {
    fprintf(stderr, "Failed to allocate buffers\n");
    return false;
  }
  if (!initMMap()) {
    fprintf(stderr, "Failed to init mmap buffers\n");
    return false;
  }
  return true;
}

bool V4L2Capture::queryCapabilities()
{
  struct v4l2_capability cap;
  memset(&cap, 0, sizeof(cap));
  if (ioctl(m_fd, VIDIOC_QUERYCAP, &cap) < 0) return false;
  return (cap.capabilities & V4L2_CAP_VIDEO_CAPTURE) &&
         (cap.capabilities & V4L2_CAP_STREAMING);
}

bool V4L2Capture::configureFormat(uint32_t width, uint32_t height)
{
  struct v4l2_format fmt;
  memset(&fmt, 0, sizeof(fmt));
  fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  fmt.fmt.pix.width = width;
  fmt.fmt.pix.height = height;
  fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUV420; // YU12 = YUV420 planar
  fmt.fmt.pix.field = V4L2_FIELD_NONE;

  if (ioctl(m_fd, VIDIOC_S_FMT, &fmt) < 0) {
    fprintf(stderr, "VIDIOC_S_FMT failed\n");
    return false;
  }

  ioctl(m_fd, VIDIOC_G_FMT, &fmt);
  m_width = fmt.fmt.pix.width;
  m_height = fmt.fmt.pix.height;

  printf("Format: YU12 (YUV420P) %ux%u, bytesperline=%u, sizeimage=%u\n",
         m_width, m_height, fmt.fmt.pix.bytesperline, fmt.fmt.pix.sizeimage);
  return true;
}

bool V4L2Capture::allocateBuffers(uint32_t count)
{
  struct v4l2_requestbuffers req;
  memset(&req, 0, sizeof(req));
  req.count = count;
  req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  req.memory = V4L2_MEMORY_MMAP;
  if (ioctl(m_fd, VIDIOC_REQBUFS, &req) < 0) return false;

  m_buffers.resize(req.count);
  return true;
}

bool V4L2Capture::initMMap()
{
  for (auto& buf : m_buffers) {
    struct v4l2_buffer b;
    memset(&b, 0, sizeof(b));
    b.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    b.memory = V4L2_MEMORY_MMAP;
    b.index = &buf - &m_buffers[0];
    if (ioctl(m_fd, VIDIOC_QUERYBUF, &b) < 0) return false;

    buf.length = b.length;
    buf.start = mmap(nullptr, b.length, PROT_READ|PROT_WRITE, MAP_SHARED, m_fd, b.m.offset);
    if (buf.start == MAP_FAILED) return false;

    if (ioctl(m_fd, VIDIOC_QBUF, &b) < 0) return false;
  }
  return true;
}

bool V4L2Capture::startStreaming()
{
  enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  return ioctl(m_fd, VIDIOC_STREAMON, &type) == 0;
}

void V4L2Capture::stopStreaming()
{
  enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  ioctl(m_fd, VIDIOC_STREAMOFF, &type);
}

bool V4L2Capture::dequeueBuffer(int& index)
{
  struct v4l2_buffer buf;
  memset(&buf, 0, sizeof(buf));
  buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  buf.memory = V4L2_MEMORY_MMAP;
  if (ioctl(m_fd, VIDIOC_DQBUF, &buf) < 0) return false;
  index = buf.index;
  return true;
}

bool V4L2Capture::enqueueBuffer(int index)
{
  struct v4l2_buffer buf;
  memset(&buf, 0, sizeof(buf));
  buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  buf.memory = V4L2_MEMORY_MMAP;
  buf.index = index;
  return ioctl(m_fd, VIDIOC_QBUF, &buf) == 0;
}