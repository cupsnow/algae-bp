#!/usr/bin/env python3
import sys, os, logging, argparse, asyncio, pathlib
import magic, pathspec

# level=logging.INFO,
logging.basicConfig(format="[%(asctime)s][%(levelname)s][%(name)s][%(funcName)s][#%(lineno)d]%(message)s")

logger = logging.getLogger("elfstrip")
logger_level = logging.DEBUG
logger.setLevel(logger_level)

app_cfg = {
    "path_list": [],
}

def logger_level_verbose(noiser=1, apply=None):
    global logger_level
    lvl = apply or logger_level
    lut = [logging.NOTSET, logging.DEBUG, logging.INFO, logging.WARNING,
            logging.ERROR, logging.CRITICAL]
    idx = lut.index(lvl) - noiser
    if idx < 0:
        lvl = lut[0]
    elif idx >= len(lut):
        lvl = lut[-1]
    else:
        lvl = lut[idx]
    if apply is None:
        logger_level = lvl
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

def do_strip(tgt, root=None):
    p = pathlib.Path(tgt)
    if p.is_symlink:
        logger.debug(f"Skip symlink: {tgt}")
        return 0
    if p.is_dir():
        path

async def main(argv):
    logger.debug(f"argv: {argv}")
    argparser = argparse.ArgumentParser(prog=argv[0], description=(f"strip executable"),
            formatter_class=argparse.ArgumentDefaultsHelpFormatter, add_help=False)
    argparser.add_argument("-h", "--help", action="store_true", help="Show this help")
    argparser.add_argument("-v", "--verbose", action="count", default=0, help="More message output")
    argparser.add_argument("-L", "--dereference", action="store_true", help="Dereference the input")
    argparser.add_argument("--strip", default="strip", help="Program used to strip executable")
    argparser.add_argument("input", nargs="?", help="Input path")

    cli_args = argparser.parse_args(argv[1:])
    app_cfg.update({"cli_args": cli_args})

    if cli_args.verbose != 0:
        logger_level_verbose(cli_args.verbose)

    if cli_args.help:
        argparser.print_help()
        return 1

    logger.debug(f"strip cli: {cli_args.strip}")

    if cli_args.input is None:
        logger.error("No input")
        return 1

    for tgt in cli_args.input:
        do_strip(tgt)

if __name__ == "__main__":
    asyncio.run(main(sys.argv))
