#ifndef X264_ENCODER_H
#define X264_ENCODER_H

#include <cstdint>
#include <vector>
#include <x264.h>

class X264Encoder {
public:
  X264Encoder(uint32_t width, uint32_t height, int fps, int bitrateKbps);
  ~X264Encoder();

  bool init();

  // Encode a YU12 frame, returns size of output data or -1 on error
  int encode(const uint8_t* y, const uint8_t* u, const uint8_t* v,
             uint32_t stride, std::vector<uint8_t>& output);

  // Get SPS/PPS (needed for Live555)
  size_t getSPS(size_t& outSPS, size_t& outPPS) const {
    outSPS = m_sps.size();
    outPPS = m_pps.size();
    return outSPS + outPPS;
  }
  const uint8_t* getSPSData() const { return m_sps.data(); }
  const uint8_t* getPPSData() const { return m_pps.data(); }

  uint32_t width()  const { return m_width;  }
  uint32_t height() const { return m_height; }

private:
  bool configureParams();

  uint32_t m_width;
  uint32_t m_height;
  int m_fps;
  int m_bitrateKbps;

  x264_param_t m_param;
  x264_t* m_encoder = nullptr;
  x264_picture_t m_pic;

  std::vector<uint8_t> m_sps;
  std::vector<uint8_t> m_pps;
};

#endif // X264_ENCODER_H