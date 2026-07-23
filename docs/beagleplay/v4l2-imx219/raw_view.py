import cv2
import numpy as np

width = 3280
height = 2464

with open("dw/imx219.raw", "rb") as f:
    raw = np.fromfile(f, dtype=np.uint16)

# One frame only
raw = raw[:width * height]
raw = raw.reshape((height, width))

# RG10 is stored in a 16-bit container.
# The actual 10-bit pixel data is typically in the lower bits.
raw10 = raw & 0x03ff

# Convert 10-bit to 8-bit for display
raw8 = (raw10 >> 2).astype(np.uint8)

# IMX219 format is SRGGB10
# rgb = cv2.cvtColor(raw8, cv2.COLOR_BayerRG2BGR)
rgb = cv2.cvtColor(raw8, cv2.COLOR_BayerBG2BGR)

cv2.imwrite("imx219.png", rgb)
