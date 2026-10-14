#!/usr/bin/env python3
import sys, os, logging, argparse, asyncio, pathlib, subprocess
import fnmatch

# level=logging.INFO,
logger_fmt = '[%(asctime)s][%(levelname)s][%(funcName)s][#%(lineno)d]%(message)s'

def logger_init(filename="privpriv1234566.log", level=logging.DEBUG):
    if filename == "privpriv1234566.log":
        filename = f"{os.path.splitext(__file__)[0]}.log"
    logging.basicConfig(level=level, filename=filename, format=logger_fmt)

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
    lut = [logging.CRITICAL, logging.ERROR, logging.WARNING, logging.INFO,
            logging.DEBUG]
    logger_level_index = logger_getLevel(logger)
    lut_idx = lut.index(logger_level_index)
    lut_idx += inc
    if lut_idx < 0:
        lut_idx = 0
    elif lut_idx >= len(lut):
        lut_idx = len(lut) - 1
    logger_setLevel(logger, lut[lut_idx])
    return logger

logger_init()
logger = logger_get("elfstrip", level=logging.DEBUG)

app_cfg = {
    "dest": [],
    "du_yield": 0
}

def filesave(data, fn, wmod="w"):
    '''
    save string or bytes to file
    '''
    if isinstance(data, (str)):
        mode = f"{wmod}"
        unit = "char"
    else:
        mode = f"{wmod}b"
        unit = "bytes"
    fdir, _ = os.path.split(fn)
    # logger.debug(f"fdir: {fdir}")
    if (fdir is not None
            and fdir != ""
            and not os.path.exists(fdir)):
        os.makedirs(fdir)
    with open(f"{fn}", mode) as f:
        f.write(data)
    # logger.debug(f"written {fn} {len(data)} {unit}")

def do_strip_log(msg):
    if "logfp" not in app_cfg:
        return 0
    logfp = app_cfg["logfp"]
    logger.debug(msg)
    if logfp.write(f"{msg}\n") > 0:
        logfp.flush()

def do_strip_elf(p):
    elfstrip = None
    if p.suffix == ".ko":
        elfstrip = app_cfg["kostrip"]
    else:
        elfstrip = app_cfg["elfstrip"]
    try:
        r = subprocess.run(f"{elfstrip} {p}", shell=True,
                capture_output=True, check=True)
    except subprocess.CalledProcessError as err:
        do_strip_log(f"Failed Strip {p}\n  {err.stderr}")
        raise err
        return 1
    do_strip_log(f"Strip {p}")
    return 0

def do_test_elf(p):
    _ELF_MAGIC = b'\x7fELF'

    if p.stat().st_size <= len(_ELF_MAGIC):
        return False

    with open(p, "rb") as f:
        val = f.read(4)
        if val == _ELF_MAGIC:
            return True
    return False

def do_test_bound(p, bound):
    parent_path = pathlib.Path(bound).resolve()
    child_path = pathlib.Path(child_path).resolve()

def do_test_exclude(p, exclude):
    if not exclude:
        return False
    if not isinstance(exclude, (list, tuple)):
        exclude = [exclude]
    for ex1 in exclude:
        if fnmatch.fnmatch(p, ex1):
            return True
    return False

def do_strip(tgt, symlink=False):
    if not isinstance(tgt, (list, tuple)):
        tgt = [tgt]
    dest = app_cfg["dest"]
    bound = app_cfg["bound"]
    exclude = app_cfg["cli_args"].exclude

    defer_dir = []
    du_yield = 0
    for tgt1 in tgt:
        p = pathlib.Path(tgt1)
        if p.is_symlink() and not symlink:
            do_strip_log(f"Skip symlink: {tgt1}")
            continue
        p = p.resolve()
        if p != tgt1:
            do_strip_log(f"Resolve: {tgt1}\n  to {p}")
            tgt1 = p
        if not p.exists():
            do_strip_log(f"Skip absent: {tgt1}")
            continue
        if (p.resolve() != bound
                and not bound in p.resolve().parents):
            do_strip_log(f"Skip out of bound: {tgt1}")
            continue
        if (p.is_socket() or p.is_fifo() or p.is_char_device()
                or p.is_block_device()):
            do_strip_log(f"Skip dev: {tgt1}")
            continue
        if p.is_dir():
            do_strip_log(f"Defer dir: {tgt1}")
            defer_dir.append(tgt1)
            continue
        if not do_test_elf(p):
            do_strip_log(f"Skip non-elf: {tgt1}")
            continue
        if do_test_exclude(p, exclude):
            do_strip_log(f"Skip exclusion: {tgt1}")
            continue
        sz0 = p.stat().st_size
        do_strip_elf(p)
        du_yield += sz0 - p.stat().st_size

    app_cfg["du_yield"] += du_yield

    for tgt1 in defer_dir:
        p = pathlib.Path(tgt1)
        do_strip_log(f"Enter dir: {tgt1}")
        do_strip([x for x in p.iterdir()])

async def main(argv):
    logger.debug(f"argv: {argv}")
    logger_setLevel(logger, logging.INFO)
    argparser = argparse.ArgumentParser(prog=argv[0], description=(f"strip executable"),
            formatter_class=argparse.ArgumentDefaultsHelpFormatter, add_help=False)
    argparser.add_argument("-h", "--help", action="store_true", help="Show this help")
    argparser.add_argument("-v", "--verbose", action="count", default=0, help="More message output")
    argparser.add_argument("-l", "--log", help="Log file path")
    argparser.add_argument("--strip", default="strip", help="Program used to strip executable")
    argparser.add_argument("--kostrip", help="Program used to strip kernel module (default: apply -g on the strip executable)")
    argparser.add_argument("--bound", help="Strip target must under the directory")
    argparser.add_argument("--exclude", action="append", help="Exclude glob")
    argparser.add_argument("input", nargs="*", help="Input path")

    cli_args = argparser.parse_args(argv[1:])
    app_cfg.update({"cli_args": cli_args})

    if cli_args.verbose != 0:
        logger_verbose(logger, cli_args.verbose)

    if cli_args.help:
        argparser.print_help()
        return 1

    logger.debug(f"strip cli: {cli_args.strip}, ko: {cli_args.kostrip}")

    app_cfg.update({
        "elfstrip": cli_args.strip or "strip"
    })
    app_cfg.update({
        "kostrip": cli_args.kostrip or f"{app_cfg['elfstrip']} -g"
    })
    app_cfg.update({
        "bound": pathlib.Path(cli_args.bound or f"{pathlib.Path.cwd()}").resolve()
    })

    if cli_args.input is None:
        logger.error("No input")
        return 1

    if cli_args.log is not None:
        app_cfg.update({"logfp": open(cli_args.log, "w")})

    do_strip(cli_args.input, symlink=True)

    du_yield = app_cfg["du_yield"]
    do_strip_log(f"Total size save: {du_yield}")

    if "logfp" in app_cfg:
        app_cfg["logfp"].close()

if __name__ == "__main__":
    if True:
        asyncio.run(main(sys.argv))
    elif True:
        asyncio.run(main(["elfstrip.py",
                *"-v -l elfstrip.log".split(),
                *"--strip=/home/joelai/02_dev/eveplay-ws/air192/tool/arm-gnu-toolchain-15.2.rel1-x86_64-arm-none-linux-gnueabihf/bin/arm-none-linux-gnueabihf-strip".split(),
                *"--bound=/home/joelai/02_dev/algae-ws/build/sysroot-air192".split(),
                *"/home/joelai/02_dev/algae-ws/build/sysroot-air192".split()]))
