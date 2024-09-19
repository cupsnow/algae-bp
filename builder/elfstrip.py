#!/usr/bin/env python3
import sys, os, logging, argparse, asyncio, pathlib, subprocess

# level=logging.INFO,
logging.basicConfig(format="[%(asctime)s][%(levelname)s][%(name)s][%(funcName)s][#%(lineno)d]%(message)s")

logger = logging.getLogger("elfstrip")
logger_level = logging.INFO
logger.setLevel(logger_level)

app_cfg = {
    "dest": [],
}

def logger_level_verbose(noiser=1, base=None):
    global logger_level
    lvl = base or logger_level
    lut = [logging.NOTSET, logging.DEBUG, logging.INFO, logging.WARNING,
            logging.ERROR, logging.CRITICAL]
    idx = lut.index(lvl) - noiser
    if idx < 0:
        lvl = lut[0]
    elif idx >= len(lut):
        lvl = lut[-1]
    else:
        lvl = lut[idx]
    if base is None:
        logger_level = lvl
        logger.setLevel(logger_level)
    return lvl

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
    # global logger_level
    # if logger_level >= logging.INFO:
    #     logger.info(f"Strip {p}")
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

def do_strip(tgt, symlink=False):
    if not isinstance(tgt, (list, tuple)):
        tgt = [tgt]
    dest = app_cfg["dest"]
    bound = app_cfg["bound"]
    defer_dir = []
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
        do_strip_elf(p)

    for tgt1 in defer_dir:
        p = pathlib.Path(tgt1)
        do_strip_log(f"Enter dir: {tgt1}")
        do_strip([x for x in p.iterdir()])


async def main(argv):
    logger.debug(f"argv: {argv}")
    argparser = argparse.ArgumentParser(prog=argv[0], description=(f"strip executable"),
            formatter_class=argparse.ArgumentDefaultsHelpFormatter, add_help=False)
    argparser.add_argument("-h", "--help", action="store_true", help="Show this help")
    argparser.add_argument("-v", "--verbose", action="count", default=0, help="More message output")
    argparser.add_argument("-l", "--log", help="Log file path")
    argparser.add_argument("--strip", default="strip", help="Program used to strip executable")
    argparser.add_argument("--kostrip", help="Program used to strip kernel module")
    argparser.add_argument("--bound", help="Strip target must under the directory")
    argparser.add_argument("input", nargs="*", help="Input path")

    cli_args = argparser.parse_args(argv[1:])
    app_cfg.update({"cli_args": cli_args})

    if cli_args.verbose != 0:
        logger_level_verbose(cli_args.verbose)

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

    if "logfp" in app_cfg:
        app_cfg["logfp"].close()

if __name__ == "__main__":
    if True:
        asyncio.run(main(sys.argv))
    elif True:
        asyncio.run(main(["elfstrip.py", 
                *"-v -l elfstrip.log".split(),
                *"--strip=algae-bp/tool/gcc-aarch64/bin/aarch64-unknown-linux-gnu-strip".split(),
                *"--bound=algae-bp/destdir/bp/rootfs".split(),
                "algae-bp/tic",
                "algae-bp/destdir/bp/rootfs", 
                "algae-bp/destdir/bp/rootfs"]))
