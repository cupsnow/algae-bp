imx219
====

designed concept

1. load fit image with overlay `bootm ${addr_fit}#conf-imx219`
1. boot run `/etc/init.d/imx219 start`
1. `/media/mmcblk0p1/uenv.txt` contain the config `fitext=#conf-imx219`


control
====

example

    v4l2-ctl -d /dev/v4l-subdev1 --list-ctrls
    v4l2-ctl -d /dev/v4l-subdev1 --set-ctrl=analogue_gain=100

mpv
====

example: play with short lag

    mpv --profile=low-latency --cache=no --untimed rtsp://192.168.16.26:8554/h264

example: capture raw image

    v4l2-ctl -d /dev/video0 --set-fmt-video=width=3280,height=2464,pixelformat=RG10 --stream-mmap --stream-count=10 --stream-to=imx219.raw --verbose

    ffplay -f rawvideo -pixel_format bayer_rggb16le -video_size 3280x2464 -i imx219.raw
