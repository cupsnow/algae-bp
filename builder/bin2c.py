#!/usr/bin/env python3
import sys, os, logging, argparse, asyncio, time
import importlib.util

logging.basicConfig(format="[%(asctime)s][%(levelname)s][%(name)s][%(funcName)s][#%(lineno)d]%(message)s")

logger = logging.getLogger("bin2c")
logger_level = logging.INFO
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

async def main(argv):
    # logger.debug(f"argv: {argv}")
    argparser = argparse.ArgumentParser(prog=argv[0], description=(f"An OTA tool"),
            formatter_class=argparse.ArgumentDefaultsHelpFormatter, add_help=False)
    argparser.add_argument("-h", "--help", action="store_true", help="Show this help")
    argparser.add_argument("-v", "--verbose", action="count", default=0, help="More message output")
    argparser.add_argument("--outdir", help="Output directory")
    argparser.add_argument("--outbase", help="Output basename")
    argparser.add_argument("-s", "--name", required=True, help="Variable name prefix")
    argparser.add_argument("input", help="Firmware file")

    argc = len(argv)
    if argc <= 1:
        argparser.print_usage()
        return 1

    args = argparser.parse_args(argv[1:])

    global logger_level

    if args.verbose != 0:
        logger_level = logger_level_verbose(args.verbose)
        logger.setLevel(logger_level)

    if args.help:
        argparser.print_help()
        return 1

    outdir = args.outdir or "."
    varname = args.name
    infile = args.input

    outbase = args.outbase or os.path.basename(infile)

    outfile = os.path.join(outdir, f"{outbase}")
    insize = os.path.getsize(infile)

    with open(f"{outfile}.h", "w") as outh:
        outh.write(f"""/* bin2c */
#ifndef _H_BIN2C_{varname}
#define _H_BIN2C_{varname}

#include <stdlib.h>

extern const size_t {varname}_size;
extern const unsigned char *{varname};

#endif /* _H_BIN2C_{varname} */
""")

    pos = 0
    t1 = time.time()
    with (open(infile, "rb") as inbin,
            open(f"{outfile}.c", "w") as outc):
        outc.write(f"""/* bin2c */
#include <stdlib.h>
const size_t {varname}_size = {insize};
const unsigned char {varname}[{insize}] = {{""")
        while True:
            bs = inbin.read(8)
            if bs == b"":
                break
            ln = ", ".join([f"0x{x:02x}" for x in bs])
            if pos == 0:
                outc.write(f"\n\t{ln}")
            else:
                outc.write(f", /* {pos} */\n\t{ln}")
            pos += len(bs)
            if logger_level <= logging.DEBUG:
                logger.debug(f"written {pos}/{insize} {pos / insize * 100:.02f}%")
            elif time.time() - t1 > 1:
                logger.info(f"written {pos}/{insize} {pos / insize * 100:.02f}%")
                t1 = time.time()

        outc.write(f"""\n}};\n""")

if __name__ == "__main__":
    asyncio.run(main(sys.argv))
    # fn = "randgen_50"
    # asyncio.run(main(["self", "--name", f"{fn}", "--outdir", "agt-ws/dkmapi-ws/build", f"agt-ws/dkmapi-ws/build/{fn}"]))
