#!/usr/bin/env python3

# pip install numpy opencv-python

import argparse
import cv2
import numpy as np


def rgb_to_rggb16(img):
    """
    Convert an RGB image to a 16-bit-per-pixel RGGB Bayer image.

    OpenCV loads images as BGR.
    Output is uint16 with valid data in bits [9:0].
    """

    h, w = img.shape[:2]

    # OpenCV uses BGR order
    b = img[:, :, 0].astype(np.uint16)
    g = img[:, :, 1].astype(np.uint16)
    r = img[:, :, 2].astype(np.uint16)

    bayer = np.zeros((h, w), dtype=np.uint16)

    #
    # RGGB pattern
    #
    # R G R G
    # G B G B
    #

    bayer[0::2, 0::2] = r[0::2, 0::2]
    bayer[0::2, 1::2] = g[0::2, 1::2]
    bayer[1::2, 0::2] = g[1::2, 0::2]
    bayer[1::2, 1::2] = b[1::2, 1::2]

    #
    # Convert 8-bit to 10-bit
    #
    # 255 -> 1023
    #

    bayer = (bayer.astype(np.uint32) * 1023 + 127) // 255

    return bayer.astype(np.uint16)


def bgr_to_i420(img):
    """
    Convert a BGR8 image to planar I420.

    OpenCV loads images as BGR.
    Output is uint8 I420: Y (h x w) + U (h/2 x w/2) + V (h/2 x w/2).
    OpenCV COLOR_BGR2YUV_I420 matches COLOR_YUV2BGR_I420 in i420_bmp.py.
    """

    return cv2.cvtColor(img, cv2.COLOR_BGR2YUV_I420)


MODE_BMP_I420 = "bmp_i420"
MODE_BMP_RGGB16 = "bmp_rggb16"

def main(args=None):

    parser = argparse.ArgumentParser()

    parser.add_argument("input")
    parser.add_argument("output")
    parser.add_argument("--mode", choices=[MODE_BMP_I420, MODE_BMP_RGGB16], default=MODE_BMP_RGGB16)

    if args:
        args = parser.parse_args(args)
    else:
        args = parser.parse_args()

    mode = args.mode

    img = cv2.imread(args.input, cv2.IMREAD_COLOR)

    if img is None:
        raise RuntimeError("Cannot open image")

    h, w = img.shape[:2]

    if (w & 1) or (h & 1):
        raise RuntimeError("Image width and height must be even.")

    if mode == MODE_BMP_RGGB16:
        data = rgb_to_rggb16(img)
        fmt = "BMP -> RGGB RAW10 stored as uint16"
    elif mode == MODE_BMP_I420:
        data = bgr_to_i420(img)
        fmt = "BMP -> I420"

    data.tofile(args.output)

    print("Input :", args.input)
    print("Output:", args.output)
    print("Size  :", w, "x", h)
    print("Format:", fmt)
    print("Bytes :", data.nbytes)


if __name__ == "__main__":
    # main("sample.bmp sample-rg10.raw".split())
    main()
