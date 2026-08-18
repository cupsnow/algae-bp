#!/usr/bin/env python3

import argparse
import cv2
import numpy as np

def i420_to_bgr(i420):
    """
    Convert a I420 image to BGR8.

    Input is uint8 with valid data in bits [7:0].
    OpenCV expects BGR for imwrite.
    """

    return cv2.cvtColor(i420, cv2.COLOR_YUV2BGR_I420)

def rggb16_to_bgr(bayer):
    """
    Convert a 16-bit-per-pixel RGGB Bayer image to BGR8.

    Input is uint16 with valid data in bits [9:0].
    OpenCV expects BGR for imwrite.
    """

    #
    # Keep only the lower 10 bits
    #
    bayer = (bayer & 0x03FF).astype(np.uint16)

    #
    # RGGB pattern
    #
    # R G R G
    # G B G B
    #
    # OpenCV COLOR_BayerRG2BGR: first pixel is R
    # Demosaic in 10-bit, then scale to 8-bit.
    #
    bgr16 = cv2.cvtColor(bayer, cv2.COLOR_BayerRG2BGR)

    #
    # Convert 10-bit to 8-bit
    #
    # 1023 -> 255
    #
    return ((bgr16.astype(np.uint32) * 255 + 511) // 1023).astype(np.uint8)

MODE_I420_BMP = "i420_bmp"
MODE_RGGB16_BMP = "rggb16_bmp"

def main(args=None):
    parser = argparse.ArgumentParser()

    parser.add_argument("width", type=int)
    parser.add_argument("height", type=int)
    parser.add_argument("input")
    parser.add_argument("output")
    parser.add_argument("--mode", choices=[MODE_I420_BMP, MODE_RGGB16_BMP], default=MODE_I420_BMP)

    if args:
        args = parser.parse_args(args)
    else:
        args = parser.parse_args()

    w = args.width
    h = args.height
    mode = args.mode

    if (w & 1) or (h & 1):
        raise RuntimeError("Image width and height must be even.")

    if mode == MODE_I420_BMP:
        expected = w * h * 3 // 2
        size_desc = "%dx%d I420" % (w, h)
    elif mode == MODE_RGGB16_BMP:
        expected = w * h * 2
        size_desc = "%dx%d uint16" % (w, h)

    with open(args.input, "rb") as f:
        data = f.read()

    if len(data) != expected:
        raise RuntimeError(
            "Unexpected file size: got %d bytes, expect %d (%s)"
            % (len(data), expected, size_desc)
        )
    if mode == MODE_I420_BMP:
        # I420 is planar 4:2:0: Y (h x w) + U (h/2 x w/2) + V (h/2 x w/2)
        # OpenCV COLOR_YUV2BGR_I420 expects shape (h * 3 // 2, w)
        i420 = np.frombuffer(data, dtype=np.uint8).reshape(h * 3 // 2, w)
        bgr = i420_to_bgr(i420)
    elif mode == MODE_RGGB16_BMP:
        bayer = np.frombuffer(data, dtype=np.uint16).reshape(h, w)
        bgr = rggb16_to_bgr(bayer)

    if not cv2.imwrite(args.output, bgr):
        raise RuntimeError("Cannot write image")

    print("Input :", args.input)
    print("Output:", args.output)
    print("Size  :", w, "x", h)
    if mode == MODE_I420_BMP:
        print("Format: I420 -> BMP")
    elif mode == MODE_RGGB16_BMP:
        print("Format: RGGB RAW10 (uint16 low 10 bits) -> BMP")
    print("Bytes :", expected)


if __name__ == "__main__":
    # main("3280 2464 sample-rg10.raw sample-rg10.bmp".split())
    main()
