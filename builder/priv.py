#!/usr/bin/env python3
import sys, os, logging, datetime, typing

# self_path = os.path.abspath(__file__)
# self_dirname = os.path.dirname(self_path)
# self_basename = os.path.basename(self_path)
# self_mainname = os.path.splitext(self_basename)[0]

# logging.basicConfig(level=logging.DEBUG, filename=f"{self_dirname}/{self_mainname}.log", format='[%(asctime)s][%(levelname)s][%(funcName)s][#%(lineno)d]%(message)s')
logger_fmt = '[%(asctime)s][%(levelname)s][%(funcName)s][#%(lineno)d]%(message)s'

def logger_init(filename="privpriv1234566.log"):
    if filename == "privpriv1234566.log":
        filename = f"{os.path.splitext(__file__)[0]}.log"
    logging.basicConfig(level=logging.DEBUG, filename=filename, format=logger_fmt)

def logger_get(name, level=logging.INFO, format=logger_fmt):
    logger = logging.getLogger(name)
    logger.setLevel(logging.DEBUG) # For capture all levels to file

    if not logger.handlers:
        console_handler = logging.StreamHandler()
        console_handler.setLevel(level)
        if format:
            console_formatter = logging.Formatter(format)
            console_handler.setFormatter(console_formatter)
        logger.addHandler(console_handler)
        logger.consoleHandler = console_handler
    return logger

def logger_getLevel(logger):
    if hasattr(logger, "consoleHandler"):
        return logger.consoleHandler.level
    return logger.level

def logger_setLevel(logger, level):
    if hasattr(logger, "consoleHandler"):
        logger.consoleHandler.setLevel(level)
    else:
        logger.setLevel(level)
    return logger

def logger_verbose(logger, inc):
    lut = [logging.CRITICAL, logging.ERROR, logging.WARNING, logging.INFO,
            logging.DEBUG]

    lut_idx = lut.index(logger_getLevel(logger))
    lut_idx += inc
    if lut_idx < 0:
        lut_idx = 0
    elif lut_idx >= len(lut):
        lut_idx = len(lut) - 1
    logger_setLevel(logger, lut[lut_idx])
    return logger

def ts_dt(ts):
    # Automatically detect if ts is in seconds or milliseconds
    # assuming anything > 10^10 is in milliseconds
    if ts > 1e10:
        dt = datetime.fromtimestamp(ts / 1000.0)
    else:
        dt = datetime.fromtimestamp(ts)
    return dt

def ts_str(ts):
    """
    Convert a numeric timestamp to a readable datetime string.
    Detects if the timestamp is in milliseconds or seconds.
    """
    try:
        # Assume timestamps > 1e10 are in milliseconds
        if ts > 1e10:
            dt = datetime.fromtimestamp(ts / 1000.0)
        else:
            dt = datetime.fromtimestamp(ts)
        return dt.isoformat(sep=' ')
    except Exception:
        return None

def arr_str(arr, sep=" ", fmt="{:02x}"):
    msg = sep.join(fmt.format(x) for x in arr)
    return msg

def modbus_crc(msg: str) -> int:
    crc = 0xFFFF
    for n in range(len(msg)):
        crc ^= msg[n]
        for i in range(8):
            if crc & 1:
                crc >>= 1
                crc ^= 0xA001
            else:
                crc >>= 1
    return crc

# 0xc W95 FAT32 (LBA)
PARTCODE_W95_FAT32 = 0xc
PARTCODE_FAT16 = 0x6
