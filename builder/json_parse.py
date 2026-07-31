#!/usr/bin/env python3
import sys, os, json, logging, argparse

logging.basicConfig(level=logging.NOTSET, format="[%(asctime)s][%(levelname)s][%(name)s][%(funcName)s][#%(lineno)d]%(message)s")

logger = logging.getLogger("configure")

def json_parse(infile, key):
    with open(infile) as f:
        jroot = json.load(f)
    jobj = jroot
    for k in key.split(","):
        k = k.strip()
        if k.startswith("[") and k.endswith("]") and isinstance(jobj, list):
            k = int(k[1:-1].strip())
        try:
            jobj = jobj[k]
        except Exception as err:
            # logger.error(f"{err}")
            return ""
    return jobj

def main(argv = sys.argv):
    argparser = argparse.ArgumentParser(prog=argv[0], 
			description="Configure project", 
			formatter_class=argparse.ArgumentDefaultsHelpFormatter,
			add_help=False)
    argparser.add_argument("-h", "--help", action="store_true", help="Show this help")
    argparser.add_argument("infile", help="Input json file")
    argparser.add_argument("key", help="Comma seperated key")

    argc = len(argv)
    if argc <= 2:
        argparser.print_usage()
        sys.exit(0)

    args = argparser.parse_args(argv[1:])

    print(json_parse(args.infile, args.key))

if __name__ == "__main__":
    main()
    # main(["json_parse.py", "air192/prebuilt/sa7715/common/etc/sa7715.json", "led,[1],color"])
    # main(["json_parse.py", "air192/prebuilt/sa7715/common/etc/sa7715.json", "downgradable"])
