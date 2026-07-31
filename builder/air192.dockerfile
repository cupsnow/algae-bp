# map image user to host user, modify /etc/subuid and /etc/subgid
#     dockremap:0:65536
#
# build
#     docker buildx build -f air192.dockerfile --tag=cupsnow/air192 .
#
# push to docker hub
#     docker image push cupsnow/air192:latest
# 
# Makefile
#
# # DOCKER_RUN_args+=-it
# # DOCKER_RUN_args+=-m 1g --cpus="2.5"
# DOCKER_RUN_args+=--rm
# # DOCKER_RUN_args+=--name air192
# 
# DOCKER_MAKE_ARGS+=$(call CLIARGS_OP,V,V=$(V))
# 
# DOCKER_RUN=docker run \
#     -v $(EVEPLAYWSDIR):$(EVEPLAYWSDIR) \
#     $(DOCKER_RUN_args)
# 
# DOCKER_MAKE=$(DOCKER_RUN) --user joelai --workdir $(EVEPLAYWSDIR)/air192 cupsnow/air192 \
#     $(MAKE) $(DOCKER_MAKE_ARGS)
# 
# dockerit_%: DOCKER_RUN_args+=-it
# dockerit_%:
# 	$(DOCKER_MAKE) $(@:dockerit_%=%)
# 
# docker_%:
# 	$(DOCKER_MAKE) $(@:docker_%=%)

ARG CLIENT_UID=1000
ARG CLIENT_NAME=joelai

FROM ubuntu:22.04
LABEL maintainer="Joe Lai" \
    description="Builder with ub20"
RUN dpkg --add-architecture i386

ARG PKG1="ca-certificates gpg wget curl"
ARG PKG2="autoconf autoconf-archive automake autopoint bc bison build-essential \
    cmake doxygen fakeroot file flex gettext git graphviz libasound2-dev \
    libavformat-dev liblzo2-dev libncurses5-dev libssl-dev libswresample-dev \
    libswscale-dev libtool lsb-release mtd-utils pkg-config python3 python3-pip \
    python3-tk python3-venv rsync texinfo xxd yasm zip zlib1g-dev:i386"

RUN apt-get update -y && DEBIAN_FRONTEND=noninteractive apt-get install -y \
    ${PKG1}
RUN apt-get update -y && DEBIAN_FRONTEND=noninteractive apt-get install -y \
    ${PKG2}

RUN set -x \
    && adduser --uid=1000 --disabled-password --gecos="joelai" --shell=/bin/bash joelai
