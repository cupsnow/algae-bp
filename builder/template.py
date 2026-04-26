import sys, os, logging, argparse
import priv

priv.logger_init(f"{os.path.splitext(__file__)[0]}.log")
logger = priv.logger_get("nonoptions", logging.DEBUG)

appcfg = {}

def main(argv=None):
    if not argv:
        argv = sys.argv
    
    argparser = argparse.ArgumentParser()
    argparser.add_argument("-v", "--verbose", action="count", default=0, help="More output")
    cmdparser = argparser.add_subparsers(dest="subcommand", metavar="COMMAND")
    cmdparser_help = cmdparser.add_parser("help", help="Show full help")

    cmdparser_nonoptions = cmdparser.add_parser("nonoptions", help="Change symlink")
    cmdparser_nonoptions.add_argument("args", nargs="*", help="args")

    argc = len(argv)
    cliargs = argparser.parse_args(argv[1:])
    appcfg.update({"cliargs": cliargs})

    if cliargs.verbose > 0:
        priv.logger_verbose(logger, cliargs.verbose)

    if cliargs.subcommand == "help":
        argparser.print_help()
        for cmdparser_iter in [cmdparser_help]:
            print(f"\n")
            cmdparser_iter.print_help()
        return 1

    if cliargs.subcommand == "nonoptions":
        logger.debug(f"args: {cliargs.args}")
        return 1

if __name__ == "__main__":
    main(f"chin.py -vvv help".split())
    main(f"chin.py -vvv nonoptions a b".split())
    # main()
