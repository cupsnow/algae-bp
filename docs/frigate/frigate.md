Frigate usage
====

Frigate
----

Host: Ubuntu 22.04

My patch to build frigate: [frigate-build2.patch](frigate-build2.patch)

Change directory to frigate, run

    make debug

Or

    docker buildx build --target=frigate --file docker/main/Dockerfile . \
	    --build-arg DEBUG=true \
		--tag frigate:latest \
		--load

Run

    docker compose -f docs/frigate/frigate-docker-compose.yml up

> Output message shows default user (admin) and password.
> Connect to https://localhost:8971

Capture desktop as v4l2 video source
----

Host: Ubuntu 22.04 X11 (not wayland)

    modprobe v4l2loopback video_nr=10 exclusive_caps=1 card_label="Desktop"
    [ "$XDG_SESSION_TYPE" = "x11" ] && ffmpeg -f x11grab -video_size 1920x1080 -framerate 30 -i ${DISPLAY}.0 -vf scale=1280:720 -pix_fmt yuv420p -f v4l2 /dev/video10

RTSP server
----

    build/tester_v4l2rtsp-ub20/tester_v4l2rtsp /dev/video10 1920 1080 30 2000 9554

Test

    ffplay rtsp://172.20.0.1:9554/h264esvideotest

ONVIF server
----

Source

    https://github.com/KoynovStas/onvif_srvd.git

Run

    build/onvif_srvd-ub20/onvif_srvd --no_fork \
      --ifs enx086d41e611a8 --port 10080 \
      --scope onvif://www.onvif.org/name/UbuntuDesktop \
      --scope onvif://www.onvif.org/type/NetworkVideoTransmitter \
      --scope onvif://www.onvif.org/Profile/Streaming \
      --scope onvif://www.onvif.org/Profile/S \
      --name Desktop \
      --width 1920 --height 1080 \
      --url "rtsp://%s:9554/h264esvideotest" --type H264 \
      --user admin --password password

