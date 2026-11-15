#!/usr/bin/env python3
import sys, os, logging, argparse, asyncio, pathlib, subprocess
import fnmatch

# level=logging.INFO,
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

logger_init()
logger = logger_get("elfstrip")
logger_setLevel(logger, logging.DEBUG)

app_cfg = {
    "dest": [],
    "du_yield": 0
}

# Architecture name: (EI_CLASS, e_machine).
ELF_ARCHES = {
    "x86": (1, 3),
    "x86_64": (2, 62),
    "arm": (1, 40),
    "aarch64": (2, 183),
    "riscv32": (1, 243),
    "riscv64": (2, 243),
}

def get_elf_arch(p):
    with open(p, "rb") as f:
        header = f.read(20)
    # header[6] is elf header version, currently always 1
    if (len(header) < 20 or header[:4] != b'\x7fELF'
            or header[4] not in (1, 2) or header[5] not in (1, 2)
            or header[6] != 1):
        return None
    elfclass = 64 if header[4] == 2 else 32
    byteorder = "little" if header[5] == 1 else "big"
    machine = int.from_bytes(header[18:20], byteorder)
    for arch, identity in ELF_ARCHES.items():
        if identity == (header[4], machine):
            return arch
    return None

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
    logger.debug(msg)
    if "logfp" not in app_cfg:
        return 0
    logfp = app_cfg["logfp"]
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
    do_strip_log(f"Stripped {p}")
    return 0

def test_elf(p):
    _ELF_MAGIC = b'\x7fELF'

    if p.stat().st_size <= len(_ELF_MAGIC):
        return False

    with open(p, "rb") as f:
        val = f.read(4)
        if val == _ELF_MAGIC:
            return True
    return False

def test_bound(p, bound):
    if not isinstance(bound, (list, tuple)):
        bound = [bound]
    child_path = pathlib.Path(p).resolve()
    for parent in bound:
        parent_path = pathlib.Path(parent).resolve()
        if child_path == parent_path or parent_path in child_path.parents:
            return True
    return False

def test_exclude(p, exclude):
    if not exclude:
        return False
    if not isinstance(exclude, (list, tuple)):
        exclude = [exclude]
    for ex1 in exclude:
        if fnmatch.fnmatch(p, ex1):
            return True
    return False

def do_strip(tgts, allow_symlink=False):
    if not isinstance(tgts, (list, tuple)):
        tgts = [tgts]
    dest = app_cfg["dest"]
    bound = app_cfg["bound"]
    exclude = app_cfg["cli_args"].exclude
    args_arch = app_cfg["cli_args"].arch

    defer_dir = []
    du_yield = 0
    for tgt1 in tgts:
        p = pathlib.Path(tgt1)
        if p.is_symlink() and not allow_symlink:
            do_strip_log(f"Skip for symlink: {tgt1}")
            continue
        p = p.resolve()
        if p != tgt1:
            do_strip_log(f"Resolve: {tgt1}\n  to {p}")
        if not p.exists():
            do_strip_log(f"Skip for absent: {p}")
            continue
        if test_exclude(p, exclude):
            do_strip_log(f"Skip for exclusion: {p}")
            continue
        if not test_bound(p, bound):
            do_strip_log(f"Skip for out of bound: {p}")
            continue
        if (p.is_socket() or p.is_fifo() or p.is_char_device()
                or p.is_block_device()):
            do_strip_log(f"Skip for device: {p}")
            continue
        if p.is_dir():
            do_strip_log(f"Defer dir: {p}")
            defer_dir.append(tgt1)
            continue
        if not test_elf(p):
            do_strip_log(f"Skip for non-elf: {p}")
            continue
        if args_arch:
            arch = get_elf_arch(p)
            if arch != args_arch:
                do_strip_log(f"Skip for arch {arch or "unknown"}: {p}")
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
    # logger_setLevel(logger, logging.INFO)
    argparser = argparse.ArgumentParser(prog=argv[0], description=(f"strip executable"),
            formatter_class=argparse.ArgumentDefaultsHelpFormatter, add_help=False)
    argparser.add_argument("-h", "--help", action="store_true", help="Show this help")
    argparser.add_argument("-v", "--verbose", action="count", default=0, help="More message output")
    argparser.add_argument("-l", "--log", help="Log file path")
    argparser.add_argument("--strip", default="strip", help="Program used to strip executable")
    argparser.add_argument("--kostrip", help="Program used to strip kernel module (default: apply -g on the strip executable)")
    argparser.add_argument("--bound", action="append",
            help="Allow targets within this directory (repeat for multiple bounds; default: current directory)")
    argparser.add_argument("--exclude", action="append", help="Exclude glob")
    argparser.add_argument("--arch", "--check-arch", choices=ELF_ARCHES,
            help="Strip only ELFs matching this architecture; skip and log mismatches")
    argparser.add_argument("input", nargs="*", help="Input path")

    cli_args = argparser.parse_args(argv[1:])
    app_cfg.update({"cli_args": cli_args})

    if cli_args.verbose != 0:
        logger_verbose(logger, cli_args.verbose)

    if cli_args.help:
        argparser.print_help()
        print(f'''
BOUND
  - Resolve to realpath before use, to solve the following mistake:

      Bound: /work/rootfs
      /work/rootfs/usr/lib -> /host/lib

      Strip /work/rootfs/usr/lib/libfoo.so should not strip /host/lib/libfoo.so

  - For symbolic link first level, take care about the bound

      /work/rootfs -> /work2/rootfs

      Strip will not work while current and default bound directory is /work
      Maybe add --bound=/work2


input
  Only the first level accept symbolic link.
  The symbolic link path will be resolved to realpath before use.

''')
        return 1

    if not cli_args.input:
        logger.error("No input")
        return 1

    app_cfg.update({
        "elfstrip": cli_args.strip or "strip",
        "kostrip": cli_args.kostrip or f"{cli_args.strip or 'strip'} -g",
        "bound": [pathlib.Path(bound).resolve()
                for bound in (cli_args.bound or [pathlib.Path.cwd()])]
    })

    if cli_args.log:
        app_cfg.update({"logfp": open(cli_args.log, "w")})

    do_strip(cli_args.input, allow_symlink=True)

    du_yield = app_cfg["du_yield"]
    do_strip_log(f"Total size save: {du_yield}")

    if "logfp" in app_cfg:
        app_cfg["logfp"].close()

if __name__ == "__main__":
    if False:
        asyncio.run(main(sys.argv))
    elif True:
        asyncio.run(main(["elfstrip.py",
            *"-v -l elfstrip.log2 --arch=x86_64 --bound=.. tool/gcc-aarch64".split(),
            ]))
    elif True:
        asyncio.run(main(["elfstrip.py",
                *"-v -l elfstrip.log2".split(),
                *"tmp/genimage".split()]))
    elif True:
        asyncio.run(main(["elfstrip.py",
                *"-v -l elfstrip.log2".split(),
                *"--strip=/home/joelai/02_dev/eveplay-ws/air192/tool/arm-gnu-toolchain-15.2.rel1-x86_64-arm-none-linux-gnueabihf/bin/arm-none-linux-gnueabihf-strip".split(),
                *"--bound=/home/joelai/02_dev/algae-ws/build/sysroot-air192".split(),
                *"/home/joelai/02_dev/algae-ws/build/sysroot-air192".split()]))
