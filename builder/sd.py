#!/usr/bin/env python3
import sys, os, logging, datetime
import argparse
import re
import shlex
import subprocess
import tempfile

# sys.path.append(os.path.dirname(__file__))
from priv import *


self_path = os.path.abspath(__file__)
self_dirname = os.path.dirname(self_path)
self_basename = os.path.basename(self_path)
self_mainname = os.path.splitext(self_basename)[0]

# logging.basicConfig(level=logging.NOTSET,
#         format="[%(asctime)s][%(levelname)s][%(name)s][%(funcName)s][#%(lineno)d]%(message)s")

# logger = logging.getLogger("sd")

# logger_level = logging.INFO
# logger.setLevel(logger_level)

# def logger_level_verbose(lvl, inc):
#     lut = [logging.CRITICAL, logging.ERROR, logging.WARNING, logging.INFO,
#             logging.DEBUG, logging.NOTSET]
#     idx = lut.index(lvl)
#     if inc + idx >= len(lut):
#         idx = len(lut) - 1
#     elif inc + idx < 0:
#         idx = 0
#     return (lut[idx], idx)

logger_init(f"{os.path.splitext(__file__)[0]}.log")
logger = logger_get("sd")

cmd = "ls -l"
pcs = subprocess.run(cmd, shell=True, text=True)
logger.debug(f"{pcs.stdout}")


def run_cmd(cmd, **kwargs):
    """Run command in shell

    Args:
        cmd (str): Command line string.

    Returns:
        subprocess.CompletedProcess: Completed process

    Example:

        resp = run_cmd("env | grep -i path", PATH="bin1:bin2", LD_LIBRARY_PATH="lib1:lib1")
        logger.debug(f"return code: {resp.returncode}, stdout: {resp.stdout}")
    """
    ext = {}
    env = kwargs.pop("env", dict(os.environ.copy()))
    for k in ["LD_LIBRARY_PATH", "PATH"]:
        klst = [*kwargs.pop(k, "").split(os.pathsep),
                *env.pop(k, "").split(os.pathsep)]
        klst = [x for x in dict.fromkeys(klst) if x]
        if klst:
            env.update({k: os.pathsep.join(klst)})
    #  If you wish to capture and combine both streams into one, use stdout=PIPE and stderr=STDOUT instead of capture_output
    resp = subprocess.run(cmd, shell=True, text=True, env=env # , capture_output=True
                          , stdout=subprocess.PIPE, stderr=subprocess.STDOUT, **kwargs)
    return resp

def shRun(cmd, **kwargs):
    verbose = (logger_level_verbose(logger_level, 0)[1] 
            >= logger_level_verbose(logging.DEBUG, 0)[1])
    if verbose:
        logger.debug(f"Execute: {cmd}")
    resp = run_cmd(cmd, **kwargs)
    outstr = resp.stdout.strip()
    if verbose:
        logger.debug(f"Return code: {resp.returncode}, stdout:\n{outstr}")
    return resp.returncode, outstr

def listPartition(dev):
    eno, resp = shRun(f"sudo sfdisk -l {dev}")
    logger.debug(f"sudo sfdisk return code: {eno}, stdout:\n{resp}")
    if eno != 0:
        logger.error("Failed list partition")
    return eno

def main(argv=sys.argv):
    argparser = argparse.ArgumentParser(prog=os.path.basename(argv[0]),
            formatter_class=argparse.ArgumentDefaultsHelpFormatter)
    argparser.add_argument("--sz1", default=250, type=int, metavar="<NUM>",
            help="Size of 1st partition in MB")
    argparser.add_argument("--offset1", default=2, type=int, metavar="<NUM>",
            help="Offset of 1st partition in MB")
    argparser.add_argument("--bpiboot", default=None, metavar="<FILE>",
            help="Bootloader for BPI, ex: u-boot-sunxi-with-spl.bin")
    argparser.add_argument("--bbbboot", default=None, metavar="<LIST>",
            help="Bootloader for BBB, ex: MLO,u-boot.img")
    argparser.add_argument("--fat16", default=False, action="store_true", 
            help="Use FAT16 for 1st partition (instead of FAT32)")
    argparser.add_argument("-v", "--verbose", default=0, action="count", 
            help="More message output")
    argparser.add_argument("-q", "--quiet",  default=False, action="store_true", 
            help="Less user interaction")
    argparser.add_argument("-n", "--dryrun", default=False, action="store_true", 
            help="No harm action only")
    argparser.add_argument("dev", default=None, help="SDCard Device")

    argc = len(argv)
    if argc <= 1:
        argparser.print_help()
        sys.exit(1)

    args = argparser.parse_args(argv[1:])

    if args.verbose != 0:
        logger.setLevel(logger_level_verbose(logger_level, args.verbose)[0])

    devPartSep = ""
    if re.match("/dev/mmcblk[0-9].*", args.dev):
        logger.info("Guessed mmcblk")
        devPartSep = "p"

    devBus = "Any"
    while devBus == "Any":
        eno, resp = shRun(f"udevadm info -q path {args.dev}")
        if eno != 0:
            logger.error("Failed get udevadm info")
            sys.exit(1)
        if re.match("/devices/.*/usb[0-9]*/.*", resp):
            devBus = "USB"
            logger.info(f"Guessed {devBus} (udev: {resp})")
            break
        logger.error(f"System might corrupt when use {args.dev} (udev: {resp})")
        sys.exit(1)

    print(f"Require sudo to list partition of {args.dev}")
    eno, resp = shRun(f"sudo sfdisk -l {args.dev}")
    if eno != 0:
        logger.error("Failed to list partition")
        sys.exit(1)
    print(f"{resp}")

    if not args.dryrun and not args.quiet:
        input(f"!!! Ctrl-C to break or press Enter to format {args.dev}")

    print(f"Clean old boot record")
    cmd = f"sudo dd if=/dev/zero of={args.dev} bs=1M count={80}"
    if args.dryrun:
        logger.info(f"dryrun: {cmd}\n")
    else:
        eno, resp = shRun(cmd)
        if eno != 0:
            logger.error("Failed clean old boot record")
            sys.exit(1)

    # 0xc W95 FAT32 (LBA)
    # 0x6 fat16
    if (args.fat16 is not None
            and args.fat16):
        fs1FatId=0x6
    else:
        fs1FatId = 0xc

    cmd = f"sudo sfdisk {args.dev}"
    cmdIn = (f"label: dos"
            f"\n{args.offset1}M,{args.sz1}M,{fs1FatId:x}"
            f"\n{args.sz1 + 1}M,,L,-")
    if args.dryrun:
        logger.info(f"dryrun: {cmd}\nstdin:\n{cmdIn}")
    else:
        eno, resp = shRun(cmd, input=cmdIn)
        if eno != 0:
            logger.error("Failed re-partition")
            sys.exit(1)

    # for slow machine
    shRun("sync; sleep 1")

    cmd = f"sudo sfdisk --activate {args.dev} 1"
    if args.dryrun:
        logger.info(f"dryrun: {cmd}\n")
    else:
        eno, resp = shRun(cmd)
        if eno != 0:
            logger.error("Failed set bootable partition")
            sys.exit(1)

    print(f"List partition of {args.dev}")
    eno, resp = shRun(f"sudo sfdisk -l {args.dev}")
    if eno != 0:
        logger.error("Failed to list partition")
        sys.exit(1)
    print(f"{resp}")

    print("Format partitions")
    cmd = f"sudo mkfs.vfat"
    if fs1FatId == 0xc:
        cmd += " -F 32"
    elif fs1FatId == 0x6:
        cmd += " -F 16"
    else:
        logger.error("Unknown fatfs size")
        sys.exit(1)
    cmd += f" -n BOOT {args.dev}{devPartSep}1"
    if args.dryrun:
        logger.info(f"dryrun: {cmd}")
    else:
        eno, resp = shRun(cmd)
        if eno != 0:
            logger.error("Failed format vfat")
            sys.exit(1)

    cmd = f"sudo mkfs.ext4 -L rootfs {args.dev}{devPartSep}2"
    if args.dryrun:
        logger.info(f"dryrun: {cmd}")
    else:
        eno, resp = shRun(cmd)
        if eno != 0:
            logger.error("Failed format ext4")
            sys.exit(1)

    if args.bpiboot:
        print("Write BPI boot data")
        cmd = f"sudo dd if={args.bpiboot} of={args.dev} bs=1024 seek=8"
        if args.dryrun:
            logger.info(f"dryrun: {cmd}")
        else:
            eno, resp = shRun(cmd)
            if eno != 0:
                logger.error(f"Failed write {args.bpiboot}")
                sys.exit(1)

    if args.bbbboot:
        print("Write BBB boot data")
        bbbboot=re.split("[, ]", args.bbbboot)
        if len(bbbboot) > 0:
            cmd = f"sudo dd if={bbbboot[0]} of={args.dev} count=1 seek=1 bs=128k"
            if args.dryrun:
                logger.info(f"dryrun: {cmd}")
            else:
                eno, resp = shRun(cmd)
                if eno != 0:
                    logger.error(f"Failed write {bbbboot[0]}")
                    sys.exit(1)
        if len(bbbboot) > 1:
            cmd = f"sudo dd if={bbbboot[1]} of={args.dev} count=2 seek=1 bs=384k"
            if args.dryrun:
                logger.info(f"dryrun: {cmd}")
            else:
                eno, resp = shRun(cmd)
                if eno != 0:
                    logger.error(f"Failed write {bbbboot[0]}")
                    sys.exit(1)

    with tempfile.TemporaryDirectory() as tmpdir:
        print("Set rootfs permission")
        logger.debug(f"tmpdir: {tmpdir}")
        cmd = f"sudo mount {args.dev}{devPartSep}2 {tmpdir}"
        if args.dryrun:
            logger.info(f"dryrun: {cmd}")
        else:
            eno, resp = shRun(cmd)
            if eno != 0:
                logger.error(f"mount {args.dev}{devPartSep}2")
                sys.exit(1)

        cmd = f"sudo chmod 0777 {tmpdir}"
        if args.dryrun:
            logger.info(f"dryrun: {cmd}")
        else:
            eno, resp = shRun(cmd)
            if eno != 0:
                logger.error(f"set rootfs mode 0777")
                sys.exit(1)

        cmd = f"sudo umount {tmpdir}"
        if args.dryrun:
            logger.info(f"dryrun: {cmd}")
        else:
            eno, resp = shRun(cmd)
            if eno != 0:
                logger.error(f"Failed umount {args.dev}{devPartSep}2")
                sys.exit(1)

    shRun("sync; sleep 1")

if __name__ == "__main__":
    # main(["sd.py", "--sz1=100", "--fat16",
    #         "--bbbboot=destdir/boot/MLO,destdir/boot/u-boot.img",
    #         "-n", "/dev/sdd"])
    # main(["sd.py", "--offset1=4",
    #         "-n", "/dev/sdd"])
    # main(sys.argv)
    pass
