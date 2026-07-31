#!/usr/bin/env python3

import argparse
import cv2
import numpy as np


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


def main(args=None):

    parser = argparse.ArgumentParser()

    parser.add_argument("width", type=int)
    parser.add_argument("height", type=int)
    parser.add_argument("input")
    parser.add_argument("output")

    if args:
        args = parser.parse_args(args)
    else:
        args = parser.parse_args()

    w = args.width
    h = args.height

    if (w & 1) or (h & 1):
        raise RuntimeError("Image width and height must be even.")

    expected = w * h * 2

    with open(args.input, "rb") as f:
        data = f.read()

    if len(data) != expected:
        raise RuntimeError(
            "Unexpected file size: got %d bytes, expect %d (%dx%d uint16)"
            % (len(data), expected, w, h)
        )

    bayer = np.frombuffer(data, dtype=np.uint16).reshape(h, w)

    bgr = rggb16_to_bgr(bayer)

    if not cv2.imwrite(args.output, bgr):
        raise RuntimeError("Cannot write image")

    print("Input :", args.input)
    print("Output:", args.output)
    print("Size  :", w, "x", h)
    print("Format: RGGB RAW10 (uint16 low 10 bits) -> BMP")
    print("Bytes :", expected)


if __name__ == "__main__":
    # main("3280 2464 sample-rg10.raw sample-rg10.bmp".split())
    main()
