#!/usr/bin/env python3
import sys, os, logging, datetime, typing

try:
    import netifaces
except ImportError:
    netifaces = None

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
    # For capture all levels to file
    logger.setLevel(logging.DEBUG)
    
    if not logger.handlers:
        console_handler = logging.StreamHandler()
        console_handler.setLevel(level)
        if format:
            console_formatter = logging.Formatter(format)
            console_handler.setFormatter(console_formatter)
        logger.addHandler(console_handler)
        # retrive consoleHandler to apply level latter
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
    """
    Adjust the verbosity of a logger by a discrete number of steps.

    Parameters
    ----
    logger : logging.Logger
        The logger whose level will be adjusted. If the logger has an attached
        console handler (added by logger_get), that handler's level is adjusted;
        otherwise the logger's level is adjusted directly.
    inc : int
        Number of steps to change verbosity. Positive values increase verbosity
        (move toward DEBUG); negative values decrease verbosity
        (move toward CRITICAL).

    Returns
    ----
    logging.Logger
        The same logger instance after its level has been updated.

    Behavior
    ----
    The function uses the ordered list [CRITICAL, ERROR, WARNING, INFO, DEBUG]
    to represent increasing verbosity. It determines the logger's current
    effective level (console handler level if present, else logger.level), adds
    inc to the index, clamps the index to the valid range, and applies the new
    level via logger_setLevel.

    Examples
    ----
    # make logger more verbose (e.g., INFO -> DEBUG)
    logger_verbose(my_logger, 1)

    # make logger less verbose (e.g., INFO -> WARNING)
    logger_verbose(my_logger, -1)
    """
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

logger = logger_get("priv")
logger_setLevel(logger, logging.DEBUG)

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

def int_fixed(num):
    if num == int(num):
        return int(num)
    return num

def int_iec(num: typing.Union[int, float]) -> typing.Tuple[typing.Union[int, float], str]:
    iec_lut = ["", "k" , "M", "G", "T", "P", "E"]
    if True:
        iec_idx = 0
        while num > 1024.0:
            if iec_idx >= len(iec_lut) - 1:
                break
            iec_idx += 1
            num /= 1024.0
        return int_fixed(num), iec_lut[iec_idx]
    else:
        # old implementation
        num_prev=0
        for iec in iec_lut:
            if num < 1024.0:
                return int_fixed(num), iec
            num_prev = num
            num /= 1024.0
        return int_fixed(num_prev), iec_lut[-1]

def find_host_ip(prefix="192.168."):
    if not netifaces:
        logger.error("netifaces module not available")
        return None
        
    prefix_list = []
    if isinstance(prefix, str):
        prefix_list.append(prefix)
    elif isinstance(prefix, list):
        prefix_list.extend(prefix)
    else:
        raise ValueError(f"Invalid prefix type: {type(prefix)}")

    def check_prefix(ip_addr):
        for iter in prefix_list:
            if ip_addr.startswith(iter):
                return ip_addr

    for iface in netifaces.interfaces():
        logger.debug(f"iface: {iface}")
        addrs = netifaces.ifaddresses(iface)
        inet_addrs = addrs.get(netifaces.AF_INET, [])
        for addr in inet_addrs:
            ip_addr = addr.get("addr", "")
            ret_addr = check_prefix(ip_addr)
            if ret_addr:
                return ret_addr

webappResponseListerer = [
    # {"resource": "*", "callback": None},
]

def webappResponseListerer_add(resource, callback):
    global webappResponseListerer
    for item in webappResponseListerer:
        if item["callback"] == callback:
            logger.warning(f"webappResponseListerer_add: {resource} already exists")
            return 0
    webappResponseListerer.append({"resource": resource, "callback": callback})
    return 1

def webappResponseListerer_del(callback, once=False):
    global webappResponseListerer

    def del_once():
        for item in webappResponseListerer:
            if item["callback"] == callback:
                webappResponseListerer.remove(item)
                return 1
        return 0

    cnt = 0
    while del_once() != 0:
        cnt += 1
        if once:
            break
    return cnt

def webappResponseListerer_post(resource, msg=None):
    global webappResponseListerer
    for item in webappResponseListerer:
        if item["resource"] == resource or item["resource"] == "*":
            if item["callback"]:
                item["callback"](msg)

