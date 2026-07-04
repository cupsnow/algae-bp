
start v4l2loopback 

    modprobe v4l2loopback video_nr=10 exclusive_caps=1 card_label="Desktop"

capture to v4l2loopback

    [ "$XDG_SESSION_TYPE" = "x11" ] && ffmpeg -f x11grab -video_size 1920x1080 -framerate 30 -i ${DISPLAY}.0 -vf scale=1280:720 -pix_fmt yuv420p -f v4l2 /dev/video10

play vlc v4l2:///dev/video10

    vlc v4l2:///dev/video10

