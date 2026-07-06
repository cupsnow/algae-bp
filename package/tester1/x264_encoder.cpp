#include "x264_encoder.h"

#include <cstring>

X264Encoder::X264Encoder(uint32_t width,
                         uint32_t height,
                         int fps,
                         int bitrateKbps)
    : m_width(width),
      m_height(height),
      m_fps(fps),
      m_bitrateKbps(bitrateKbps)
{
    memset(&m_param, 0, sizeof(m_param));
    memset(&m_pic, 0, sizeof(m_pic));
}

X264Encoder::~X264Encoder()
{
    if (m_encoder) {
        x264_encoder_close(m_encoder);
        m_encoder = nullptr;
    }

    x264_picture_clean(&m_pic);
}

bool X264Encoder::configureParams()
{
    if (x264_param_default_preset(&m_param,
            "veryfast",
            "zerolatency") < 0) {
        return false;
    }

    m_param.i_width  = m_width;
    m_param.i_height = m_height;

    m_param.i_fps_num = m_fps;
    m_param.i_fps_den = 1;

    m_param.i_keyint_max = m_fps;
    m_param.i_keyint_min = m_fps;

    m_param.i_threads = 1;

    m_param.i_csp = X264_CSP_I420;

    m_param.b_repeat_headers = 0;
    m_param.b_annexb = 1;

    m_param.rc.i_rc_method = X264_RC_ABR;
    m_param.rc.i_bitrate = m_bitrateKbps;

    m_param.b_vfr_input = 0;

    m_param.i_log_level = X264_LOG_WARNING;

    if (x264_param_apply_profile(&m_param, "baseline") < 0)
        return false;

    return true;
}

bool X264Encoder::init()
{
    if (!configureParams())
        return false;

    if (x264_picture_alloc(&m_pic,
                           X264_CSP_I420,
                           m_width,
                           m_height) < 0)
        return false;

    m_encoder = x264_encoder_open(&m_param);
    if (!m_encoder)
        return false;

    x264_nal_t *nals = nullptr;
    int nnal = 0;

    if (x264_encoder_headers(m_encoder, &nals, &nnal) < 0)
        return false;

    m_sps.clear();
    m_pps.clear();

    for (int i = 0; i < nnal; i++) {
        const uint8_t *payload = nals[i].p_payload;
        int size = nals[i].i_payload;

        // Skip Annex-B start code
        int offset = 0;

        if (size >= 4 &&
            payload[0] == 0 &&
            payload[1] == 0 &&
            payload[2] == 0 &&
            payload[3] == 1) {
            offset = 4;
        }
        else if (size >= 3 &&
                 payload[0] == 0 &&
                 payload[1] == 0 &&
                 payload[2] == 1) {
            offset = 3;
        }

        switch (nals[i].i_type) {
        case NAL_SPS:
            m_sps.assign(payload + offset,
                         payload + size);
            break;

        case NAL_PPS:
            m_pps.assign(payload + offset,
                         payload + size);
            break;

        default:
            break;
        }
    }

    return true;
}

int X264Encoder::encode(const uint8_t *y,
                        const uint8_t *u,
                        const uint8_t *v,
                        uint32_t stride,
                        std::vector<uint8_t> &output)
{
    if (!m_encoder)
        return -1;

    m_pic.img.plane[0] = const_cast<uint8_t *>(y);
    m_pic.img.plane[1] = const_cast<uint8_t *>(u);
    m_pic.img.plane[2] = const_cast<uint8_t *>(v);

    m_pic.img.i_stride[0] = stride;
    m_pic.img.i_stride[1] = stride / 2;
    m_pic.img.i_stride[2] = stride / 2;

    x264_picture_t pic_out;

    x264_nal_t *nals = nullptr;
    int nnal = 0;

    int frameSize = x264_encoder_encode(
        m_encoder,
        &nals,
        &nnal,
        &m_pic,
        &pic_out);

    if (frameSize < 0)
        return -1;

    output.clear();
    output.reserve(frameSize);

    for (int i = 0; i < nnal; i++) {
        output.insert(output.end(),
                      nals[i].p_payload,
                      nals[i].p_payload + nals[i].i_payload);
    }

    return static_cast<int>(output.size());
}
