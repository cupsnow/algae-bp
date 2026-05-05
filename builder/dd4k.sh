#!/bin/bash

dd bs=4M conv=fdatasync status=progress iflag=nonblock oflag=nonblock "$@"
