#!/usr/bin/env python3
import sys, os, logging, datetime
import argparse
import re
import shlex
import subprocess
import tempfile
from priv import *

self_path = os.path.abspath(__file__)
self_dirname = os.path.dirname(self_path)
self_basename = os.path.basename(self_path)
self_mainname = os.path.splitext(self_basename)[0]

logger_init(f"{os.path.splitext(__file__)[0]}.log")
logger = logger_get("sd", logging.DEBUG)

# cmd = "ls -l"
# cmd_ret = subprocess.run(cmd, shell=True, text=True, stdout=subprocess.PIPE)
# logger.debug(f"\n{cmd_ret.stdout}\ncmd ret {cmd_ret.returncode}")
# sys.exit()

# def run_cmd(cmd, **kwargs):
#     """Run command in shell

#     Args:
#         cmd (str): Command line string.

#     Returns:
#         subprocess.CompletedProcess: Completed process

#     Example:

#         resp = run_cmd("env | grep -i path", PATH="bin1:bin2", LD_LIBRARY_PATH="lib1:lib1")
#         logger.debug(f"return code: {resp.returncode}, stdout: {resp.stdout}")
#     """
#     ext = {}
#     env = kwargs.pop("env", dict(os.environ.copy()))
#     for k in ["LD_LIBRARY_PATH", "PATH"]:
#         klst = [*kwargs.pop(k, "").split(os.pathsep),
#                 *env.pop(k, "").split(os.pathsep)]
#         klst = [x for x in dict.fromkeys(klst) if x]
#         if klst:
#             env.update({k: os.pathsep.join(klst)})
#     #  If you wish to capture and combine both streams into one, use stdout=PIPE and stderr=STDOUT instead of capture_output
#     resp = subprocess.run(cmd, shell=True, text=True, env=env # , capture_output=True
#                           , stdout=subprocess.PIPE, stderr=subprocess.STDOUT, **kwargs)
#     return resp

# def shRun(cmd, **kwargs):
#     logger.debug(f"Return code: {resp.returncode}, stdout:\n{outstr}")
#     return resp.returncode, outstr

def shell_run(cmd, **args):
    logger.debug(f"Execute: {cmd}")
    cmd_ret = subprocess.run(cmd, shell=True, text=True, stdout=subprocess.PIPE, **args)
    # logger.debug(f"Execute: {cmd}, return {cmd_ret.returncode}")
    return cmd_ret

def main(argv=None):
    if not argv:
        argv = sys.argv
    argparser = argparse.ArgumentParser(prog=os.path.basename(argv[0]),
            formatter_class=argparse.ArgumentDefaultsHelpFormatter)
    argparser.add_argument("--sz1", default=130, type=int, metavar="<NUM>",
            help="Size of 1st partition in MB")
    argparser.add_argument("--offset1", default=2, type=int, metavar="<NUM>",
            help="Offset of 1st partition in MB")
    argparser.add_argument("--sz2", type=int, metavar="<NUM>",
            help="Size of 2nd partition in MB")
    argparser.add_argument("--bpiboot", metavar="<FILE>",
            help="Bootloader for BPI, ex: u-boot-sunxi-with-spl.bin")
    argparser.add_argument("--bbbboot", metavar="<LIST>",
            help="Bootloader for BBB, ex: MLO,u-boot.img")
    argparser.add_argument("--fat16",
            help="Use FAT16 for 1st partition (instead of FAT32)")
    argparser.add_argument("-t", "--dry", action="store_true",
            help="Do not acturally modify disk")
    argparser.add_argument("-v", "--verbose", default=0, action="count",
            help="More message output")
    argparser.add_argument("-q", "--quiet", 
            help="Less user interaction")
    argparser.add_argument("dev", help="SDCard Device")

    argc = len(argv)
    if argc <= 1:
        argparser.print_help()
        sys.exit(1)

    args = argparser.parse_args(argv[1:])

    if args.verbose != 0:
        logger_verbose(logger, args.verbose)

    def verify_extdisk(dev) -> str:
        cmd_ret = shell_run(f"udevadm info -q path {dev}")
        if cmd_ret.returncode != 0:
            logger.error(f"Failure detect class of dev {dev}, (udevadm return code {cmd_et.returncode})")
            return ""

        if re.match("/devices/.*/usb[0-9]*/.*", cmd_ret.stdout):
            logger.info(f"Guessed USB (udev: {cmd_ret.stdout})")
            return "USB"

        logger.error(f"Failure detect class of dev {dev} (udevadm stdout: {cmd_ret.stdout})")
        return ""

    def list_partable(dev):
        cmd_ret = shell_run(f"sudo sfdisk -l {dev}")
        if cmd_ret.returncode != 0:
            logger.error(f"Failure list partition for {dev}")
            return ""
        print(f"{cmd_ret.stdout}")
        return cmd_ret.stdout

    DD_ARGS1 = f"conv=fdatasync iflag=nonblock oflag=nonblock"

    def dd_io1(idev, odev, sz=0):
        cmd = f"sudo dd if={idev} of={odev} bs=4M"
        sz4m = 4 * 1048576
        if sz > 0:
            cnt = (sz + sz4m - 1) // sz4m
            cmd += f" count={cnt}"
        cmd += f" {DD_ARGS1}"
        if args.dry:
            logger.debug(f"Dryrun: {cmd}")
            return True
        cmd_ret = shell_run(f"{cmd}")
        if cmd_ret.returncode != 0:
            return False
        return True

    def format_disk(dev, cmd_stdin):
        cmd = f"sudo sfdisk {dev}"
        if args.dry:
            logger.debug(f"Dryrun: {cmd}, stdin\n{cmd_stdin}")
            return True
        cmd_ret = shell_run(cmd, input=cmd_stdin)
        if cmd_ret.returncode != 0:
            logger.error("Failure format disk")
            return False
        return True

    def bootable_disk(dev, idx=1):
        cmd = f"sudo sfdisk --activate {dev}"
        cmd += f" {idx}"
        if args.dry:
            logger.debug(f"Dryrun: {cmd}")
            return True
        cmd_ret = shell_run(cmd)
        if cmd_ret.returncode != 0:
            logger.error("Failure set bootable flag")
            return False
        return True

    def fsync(dly=1):
        cmd = f"sync"
        if dly > 0:
            cmd += f"; sleep {dly}"
        shell_run(cmd)

    def format_vfat(partdev, fatsz=32):
        cmd = f"sudo mkfs.vfat"
        if fatsz == 16 or fatsz == 32:
            cmd += f" -F {fatsz}"
        else:
            logger.error("Invalid partition1 fatfs size")
            return False
        cmd += f" -n BOOT {partdev}"
        if args.dry:
            logger.debug(f"Dryrun: {cmd}")
            return True
        cmd_ret = shell_run(cmd)
        if cmd_ret.returncode != 0:
            return False
        return True

    def format_ext4(partdev):
        cmd = f"sudo mkfs.ext4 -L rootfs {partdev}"
        if args.dry:
            logger.debug(f"Dryrun: {cmd}")
            return True
        cmd_ret = shell_run(cmd)
        if cmd_ret.returncode != 0:
            logger.error("Failure format partition2")
            return False
        return True

    def part_chmod(partdev, cliargstr="0777"):
        with tempfile.TemporaryDirectory() as tmpdir:
            cmd = f"sudo mount {partdev} {tmpdir}"
            if args.dry:
                logger.debug(f"Dryrun: {cmd}")
            else:
                cmd_ret = shell_run(cmd)
                if cmd_ret.returncode != 0:
                    logger.error(f"Failure mount {partdev}")
                    return False

            cmd = f"sudo chmod {cliargstr} {tmpdir}"
            if args.dry:
                logger.debug(f"Dryrun: {cmd}")
            else:
                cmd_ret = shell_run(cmd)
                if cmd_ret.returncode != 0:
                    logger.error(f"Failure change {partdev} chmod {cliargstr}")
                    return False

            cmd = f"sudo umount {tmpdir}"
            if args.dry:
                logger.debug(f"Dryrun: {cmd}")
            else:
                cmd_ret = shell_run(cmd)
                if cmd_ret.returncode != 0:
                    logger.error(f"Failure unmoount {partdev}")
                    return False
        return True

    logger.debug(f"Verify target device is usbdisk")
    if verify_extdisk(args.dev) not in ["USB"]:
        logger.error(f"Failure detect external disk {args.dev}")
        sys.exit(1)

    logger.debug(f"Review {args.dev} partition table to prevent broken device")
    if not list_partable(args.dev):
        sys.exit(1)

    msg = f"!!! Ctrl-C to break or press Enter to format {args.dev}"
    if args.dry or args.quiet:
        print(f"Dryrun: {msg}")
    else:
        input(f"{msg}")

    logger.debug(f"Some device take first sectors for bootloader, here zero them")
    if not dd_io1("/dev/zero", "args.dev", 4 * 1048576):
        logger.error("Failure clean old boot record")
        sys.exit(1)

    logger.debug(f"Format disk")
    
    # default fat32
    if args.fat16:
        part1_partcode = PARTCODE_FAT16
    else:
        part1_partcode = PARTCODE_W95_FAT32

    fdiskstr = (f"label: dos"
        f"\n{args.offset1}M,{args.sz1}M,{part1_partcode:x}"
        )
    if args.sz2:
        fdiskstr += f"\n,{args.sz2}M,L"
    else:
        fdiskstr += f"\n,,L"
    if not format_disk(args.dev, f"{fdiskstr}"):
        logger.error("Failure format disk")
        sys.exit(1)

    # for slow machine
    fsync()

    # set bootable flag
    if not bootable_disk(args.dev):
        logger.error("Failure set bootable flag")
        sys.exit(1)

    logger.debug(f"Result for format disk")
    if not list_partable(args.dev):
        sys.exit(1)

    # compose partition device name while format partition
    if re.match("/dev/mmcblk[0-9].*", args.dev):
        partsep = "p"
    else:
        partsep = ""

    partdev1 = f"{args.dev}{partsep}{1}"

    logger.info(f"Format partition1")
    partdev = partdev1
    if part1_partcode == PARTCODE_FAT16:
        fatsz = 16
    else:
        fatsz = 32
    if not format_vfat(partdev, fatsz):
        logger.error("Failure format partition1")
        sys.exit(1)

    partdev2 = f"{args.dev}{partsep}{2}"

    logger.info(f"Format partition2")
    partdev = partdev2
    if not format_ext4(partdev):
        logger.error("Failure format partition2")
        sys.exit(1)

    # if args.bpiboot:
    #     logger.info("Write BPI boot data")
    #     cmd = f"sudo dd if={args.bpiboot} of={args.dev} bs=1024 seek=8"
    #     if args.dryrun:
    #         logger.info(f"dryrun: {cmd}")
    #     else:
    #         eno, resp = shRun(cmd)
    #         if eno != 0:
    #             logger.error(f"Failed write {args.bpiboot}")
    #             sys.exit(1)

    # if args.bbbboot:
    #     print("Write BBB boot data")
    #     bbbboot=re.split("[, ]", args.bbbboot)
    #     if len(bbbboot) > 0:
    #         cmd = f"sudo dd if={bbbboot[0]} of={args.dev} count=1 seek=1 bs=128k"
    #         if args.dryrun:
    #             logger.info(f"dryrun: {cmd}")
    #         else:
    #             eno, resp = shRun(cmd)
    #             if eno != 0:
    #                 logger.error(f"Failed write {bbbboot[0]}")
    #                 sys.exit(1)
    #     if len(bbbboot) > 1:
    #         cmd = f"sudo dd if={bbbboot[1]} of={args.dev} count=2 seek=1 bs=384k"
    #         if args.dryrun:
    #             logger.info(f"dryrun: {cmd}")
    #         else:
    #             eno, resp = shRun(cmd)
    #             if eno != 0:
    #                 logger.error(f"Failed write {bbbboot[0]}")
    #                 sys.exit(1)

    logger.info("Since we manipulate partition2 file, loose partition2 permission, (tmpdir: {tmpdir})")
    if not part_chmod(partdev2):
        sys.exit(1)

    fsync()

if __name__ == "__main__":
    # main(f"sd.py -t --sz2=500 /dev/sdd".split())
    main(sys.argv)
    pass
