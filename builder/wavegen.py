#!/usr/bin/env python3
import sys, os, logging, datetime, math, asyncio
import typing
import matplotlib.pyplot as plt
from priv import *

logger_init(f"{os.path.splitext(__file__)[0]}.log")

logger = logger_get("wavegen")

def sin_generator_init(sg: dict, mag, freq, rate):
    # angular frequency:  cycles/sec / (samp/sec) * rad/cycle = rad/samp
    w = freq / rate * 2 * math.pi
    if (freq >= rate / 2):
        return -1
    sg.update({
        "phasor_real": math.cos(w),
        "phasor_imag": math.sin(w),
        "state_real": 0.0,
        "state_imag": mag,
        "magnitude": mag,
        "frequency": freq,
        "sampling_rate": rate
    })
    return 0

def sin_generator_next_sample(sg: dict):
	# get shorthand to pointers
	pr = sg["phasor_real"]
	pi = sg["phasor_imag"]
	sr = sg["state_real"]
	si = sg["state_imag"]
	# step the phasor -- complex multiply
	sg["state_real"] = sr * pr - si * pi
	sg["state_imag"] = sr * pi + pr * si
	# return the input value so sine wave starts at exactly 0.0
	return sr

def sin_generator_vfill(sg: dict, buf: list):
    for i in range(0, len(buf)):
        buf[i] = sin_generator_next_sample(sg)

def generate_sine_wave(sg: dict, freq, rate, frames: list):
    if sg.get("frequency", 0) != freq:
        sin_generator_init(sg, 1.0, freq, rate)
    sin_generator_vfill(sg, frames)

async def main_async():
    rate = 44100
    freq = 200
    x = [x for x in range(0, rate)]
    sg = {}
    y = [0] * len(x)
    generate_sine_wave(sg, freq, rate, y)

    # fig, ax = plt.subplots()
    lmt = int(rate / freq * 2)
    plt.plot(x[0:lmt], y[0:lmt])
    plt.show()

if __name__ == "__main__":
    asyncio.run(main_async())

