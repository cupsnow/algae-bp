#!/usr/bin/env python3
import sys, os, logging, argparse, asyncio, pathlib
import magic, pathspec

# level=logging.INFO, 
logging.basicConfig(format="[%(asctime)s][%(levelname)s][%(name)s][%(funcName)s][#%(lineno)d]%(message)s")
logger = logging.getLogger("strip")
logger_level = logging.DEBUG
logger.setLevel(logger_level)

def logger_level_verbose(inc = 1, lvl_base = None):
    if lvl_base is None:
        global logger_level
        lvl_base = logger_level
    lut = [logging.CRITICAL, logging.ERROR, logging.WARNING, logging.INFO,
            logging.DEBUG, logging.NOTSET]
    idx = lut.index(lvl_base)
    if inc + idx >= len(lut):
        return lut[-1]
    if inc + idx < 0:
        return lut[0]
    return lut[inc + idx]

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

def do_strip(*args, strip_program="strip", outdir=None, exclusion_match=None):
    done_set = []
    work_set = [*args]




    for src in args:
        if do_match(src, exclusion_match):
            continue
        if os.path.isdir(src):
            dirs.append(src)
            continue
        if os.path.isfile(src):
            do_strip_file(src, strip_program, outdir)
        if os.path.isdir(src):
            dirs.append(src)

async def main(argv):
    logger.debug(f"argv: {argv}")
    argparser = argparse.ArgumentParser(prog=argv[0], description=(f"strip executable"),
            formatter_class=argparse.ArgumentDefaultsHelpFormatter, add_help=False)
    argparser.add_argument("-h", "--help", action="store_true", help="Show this help")
    argparser.add_argument("-v", "--verbose", action="count", default=0, help="More message output")
    argparser.add_argument("--strip-program-args", default="-g", help="Strip executable cli args")
    argparser.add_argument("--strip-program", help="Program used to strip executable")
    argparser.add_argument("--exclude", nargs="?", help="GitIgnoreSpec for exclusion")
    argparser.add_argument("input", nargs="?", help="Input path")

    args = argparser.parse_args(argv[1:])

    if args.verbose != 0:
        logger.setLevel(logger_level_verbose(args.verbose))

    if args.help:
        argparser.print_help()
        return 1

    spec_ex = None
    if args.exclude is not None:
        with open(args.exclude, 'r') as f:
            spec_ex = pathspec.PathSpec.from_lines('gitignore', f)

    strip_program = args.strip_program or "strip"
    logger.debug(f"strip_program: {strip_program}")

    if args.input is None:
        logger.error("No input")
        return 1

    for dirfile in args.input:


        p = pathlib.Path(dirfile)
        if p.is_symlink():
            if not args.dereference:
                continue
        pr = p.resolve()


        pr = p.resolve()
        if p.is_symlink():
            pr = p.resolve()
        if os.path.islink(dirfile):
            if os.path.isfile(dirfile):

        if os.path.isfile(dir):

        do_strip(args.input, strip_program=strip_program)

if __name__ == "__main__":
    asyncio.run(main(sys.argv))
