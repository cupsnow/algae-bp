#!/usr/bin/env python3
import sys, os, logging, datetime, argparse
import pyudev
from priv import *

self_path = os.path.abspath(__file__)
self_dirname = os.path.dirname(self_path)
self_basename = os.path.basename(self_path)
self_mainname = os.path.splitext(self_basename)[0]

logger_init(f"{os.path.splitext(__file__)[0]}.log")
logger = logger_get("sd", logging.ERROR)

udev_ctx = pyudev.Context()

def find_dev_usb(target_fstype, target_label):
    for dev_iter in udev_ctx.list_devices(subsystem='block', DEVTYPE='partition'):
        bustype = dev_iter.get('ID_BUS')
        if bustype != 'usb':
            continue
        fstype = dev_iter.get('ID_FS_TYPE')
        label = dev_iter.get('ID_FS_LABEL')
        if fstype == target_fstype and label == target_label:
            logger.debug(f"Found: {dev_iter.device_node} ({fstype}, label={label})")
            return dev_iter.device_node

def cmd_find_part(cliargs):
    fstype = cliargs.fstype or "vfat"
    label = cliargs.label or "BPBOOT"
    # find_dev_usb("vfat", "BPBOOT")
    # find_dev_usb("ext4", "BPROOT")
    device_node = find_dev_usb(fstype, label)
    if device_node:
        print(f"{device_node}")
        return device_node

def main(argv=None):
    if not argv:
        argv = sys.argv
    argparser = argparse.ArgumentParser()
    argparser.add_argument("-v", "--verbose", action='count', default=0, help="More output")
    cmdparser = argparser.add_subparsers(dest="subcommand", metavar="COMMAND")

    cmdparser_help = cmdparser.add_parser("help", help="Show full help")

    cmdparser_findpart = cmdparser.add_parser("find_part", help="Find partition by fstype and label")
    cmdparser_findpart.add_argument("-f", "--fstype", help="File system type")
    cmdparser_findpart.add_argument("-l", "--label", help="File system label")

    cmdparser_distsd = cmdparser.add_parser("distsd", help="Write distribute to sd")

    argc = len(argv)

    args = argparser.parse_args(argv[1:])

    if args.verbose > 0:
        logger_verbose(logger, args.verbose)

    if args.subcommand == "help":
        argparser.print_help()
        for cmdparser_iter in [cmdparser_help, cmdparser_findpart, cmdparser_distsd]:
            print(f"\n")
            cmdparser_iter.print_help()
        return 1

    if args.subcommand == "find_part":
        return cmd_find_part(args)

    if args.subcommand == "distsd":
        return cmd_help(args)

    if argc <= 1:
        argparser.print_help()
        return 1

if __name__ == "__main__":
    # main(f"sd2.py -vvv find_part -f vfat -l BPBOOT".split())
    main()

