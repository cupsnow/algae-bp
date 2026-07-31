#!/bin/bash
# shellcheck disable=SC2317,SC2120

# Assume
# joelai@lavender5:~/02_exdev2/agt-ws/esh-ws$ cat /etc/subuid
# dockremap:0:65536
# joelai@lavender5:~/02_exdev2/agt-ws/esh-ws$ cat /etc/subgid
# dockremap:0:65536

ts1 () {
  date
}

log_e () {
  _lo_ts="$(ts1)"
  echo "${_lo_ts:+[$_lo_ts]}[ERROR] $*"
}

log_d () {
  _lo_ts="$(ts1)"
  echo "${_lo_ts:+[$_lo_ts]}[Debug] $*"
}

cmd_run () {
  log_d "Execute $*"
  "$@"
}

_pri_cn1="jl.localhost"
[ -n "${_pri_server_cn}" ] || _pri_server_cn="192.168.50.123"
# [ -n "${_pri_client_cn}" ] || _pri_client_cn="192.168.50.161"

gencnf () {
  _lo_ofile=$1
  _lo_cn=$2
  _lo_san=
  if [ -n "$_lo_cn" ] && [ ! "$_lo_cn" = "localhost" ]; then
    _lo_san="${_lo_san:+${_lo_san},}${_lo_cn:+DNS:$_lo_cn}"
  fi
  _lo_san="${_lo_san:+${_lo_san},}DNS:localhost,IP:0.0.0.0"
  cat <<-EEOH >"$_lo_ofile"
subjectAltName=$_lo_san
EEOH
}

gencert_ca () {
  # generate root ca
  _lo_prefix=${1:-mosquitto_ca}

  cmd_run openssl genrsa -out ${_lo_prefix}.key -des3 -passout pass:joelai 2048
  cmd_run openssl req -new -key ${_lo_prefix}.key -out ${_lo_prefix}.csr \
      -passin pass:joelai -subj "/C=TW/ST=taiwan/L=taipei/O=Fake Authority"
  cmd_run openssl x509 -req -in ${_lo_prefix}.csr -signkey ${_lo_prefix}.key \
      -out ${_lo_prefix}.crt -days 1000 -passin pass:joelai
}

gencert_serv () {
  # generate server cert
  _lo_prefix=${1:-mosquitto_serv}

  cmd_run openssl genrsa -out ${_lo_prefix}.key 2048
  cmd_run openssl req -new -out ${_lo_prefix}.csr -key ${_lo_prefix}.key \
      -subj "/C=TW/ST=taiwan/L=taipei/O=Localhost MQTT Broker${_pri_server_cn:+/CN=$_pri_server_cn}"
  gencnf ${_lo_prefix}.cnf $_pri_server_cn
  [ -f ${_lo_prefix}.cnf ] && _lo_cnf=${_lo_prefix}.cnf
  cmd_run openssl x509 -req -in ${_lo_prefix}.csr -CA mosquitto_ca.crt \
      -CAkey mosquitto_ca.key -CAcreateserial -out ${_lo_prefix}.crt -days 600 \
      -passin pass:joelai ${_lo_cnf:+-extfile $_lo_cnf}
}

gencert_client () {
  # generate client cert
  _lo_prefix=${1:-mosquitto_client}

  cmd_run openssl genrsa -out ${_lo_prefix}.key 2048
  cmd_run openssl req -new -out ${_lo_prefix}.csr -key ${_lo_prefix}.key \
      -subj "/C=TW/ST=taiwan/L=taipei/O=MQTT client${_pri_client_cn:+/CN=$_pri_client_cn}"
  gencnf ${_lo_prefix}.cnf $_pri_client_cn
  [ -f ${_lo_prefix}.cnf ] && _lo_cnf=${_lo_prefix}.cnf
  cmd_run openssl x509 -req -in ${_lo_prefix}.csr -CA mosquitto_ca.crt \
      -CAkey mosquitto_ca.key -CAcreateserial -out ${_lo_prefix}.crt -days 600 \
      -passin pass:joelai ${_lo_cnf:+-extfile $_lo_cnf}
}

# http://www.steves-internet-guide.com/mosquitto-tls/
gencert () {
  _lo_opt=$1

  if [ "$_lo_opt" = "ca" ]; then
    gencert_ca
    return
  fi

  if [ "$_lo_opt" = "serv" ]; then
    gencert_serv
    return
  fi

  if [ "$_lo_opt" = "client" ]; then
    gencert_client
    return
  fi

  if [ "$_lo_opt" = "force" ]; then
    gencert_ca || return
    gencert_serv || return
    gencert_client || return
    return
  fi

  # if [ ! "$_lo_opt" = "absent" ]; then
  #   log_e "Invalid argument"
  #   return 1
  # fi

  if [ ! -f "mosquitto_ca.crt" ]; then
    gencert_ca || return
  fi
  if [ ! -f "mosquitto_serv.key" ] \
      || [ ! -f "mosquitto_serv.crt" ]; then
    gencert_serv || return
  fi
  if [ ! -f "mosquitto_client.key" ] \
      || [ ! -f "mosquitto_client.crt" ]; then
    gencert_client || return
  fi

  rm -rf mosquitto_ca.csr \
      mosquitto_serv.csr mosquitto_serv.cnf \
      mosquitto_client.csr mosquitto_client.cnf
}

genpwd_mosquitto () {
  _lo_pwd=mosquitto_passwd.conf
  _lo_shd=mosquitto_shadow.conf

  _lo_uid="$(id -u)"
  _lo_user="$(id -un)"

  _lo_pwd_own=
  if [ ! -f "$_lo_pwd" ]; then
    _lo_pwd_own=1
    cat >$_lo_pwd <<-EOHH
pub:123456779
${_lo_user}:12345677910
EOHH
  fi

  cat >genpwd_mosquitto.sh <<-EOHH
adduser -D -u${_lo_uid} ${_lo_user} ${_lo_user}
su ${_lo_user} -c "exec cp -v /host/${_lo_pwd} /host/${_lo_shd}"
su ${_lo_user} -c "exec chmod 660 /host/${_lo_shd}"
su ${_lo_user} -c "exec mosquitto_passwd -U /host/${_lo_shd}"
EOHH

chmod +x genpwd_mosquitto.sh

# shellcheck disable=SC2086
cmd_run docker run -it -v "$PWD:/host" ${_lo_opt_vol} --rm eclipse-mosquitto \
    /host/genpwd_mosquitto.sh

if [ -n "$_lo_pwd_own" ]; then
  rm -rf genpwd_mosquitto.sh $_lo_pwd
fi

}

genpwd () {
    _lo_opt="$1"

  if [ "$_lo_opt" = "force" ]; then
    genpwd_mosquitto || return
    return
  fi

  # if [ ! "$_lo_opt" = "absent" ]; then
  #   log_e "Invalid argument"
  #   return 1
  # fi

  if [ ! -f "mosquitto_shadow.conf" ]; then
    genpwd_mosquitto || return
  fi
}

genconf_mosquitto () {
  _lo_conf=mosquitto.conf

  cat >"${_lo_conf}" <<-EOHH || { echo "Failure gen conf"; return 1; }
persistence true
persistence_location /mosquitto/data/
log_dest stdout
log_dest file /mosquitto/log/mosquitto.log
# log_type all
connection_messages true
log_timestamp true
per_listener_settings true
EOHH

  cat >>"${_lo_conf}" <<-EOHH || { echo "Failure gen conf"; return 1; }
listener 1883
allow_anonymous true
EOHH

  cat >>"${_lo_conf}" <<-EOHH || { echo "Failure gen conf"; return 1; }
listener 3883
allow_anonymous false
password_file /mosquitto/config/mosquitto_shadow.conf
EOHH

  cat >>"${_lo_conf}" <<-EOHH || { echo "Failure gen conf"; return 1; }
listener 5883
allow_anonymous true
cafile /mosquitto/config/mosquitto_ca.crt
keyfile /mosquitto/config/mosquitto_serv.key
certfile /mosquitto/config/mosquitto_serv.crt
# tls_version tlsv1.3
require_certificate false
EOHH

  cat >>"${_lo_conf}" <<-EOHH || { echo "Failure gen conf"; return 1; }
listener 8883
allow_anonymous true
cafile /mosquitto/config/mosquitto_ca.crt
keyfile /mosquitto/config/mosquitto_serv.key
certfile /mosquitto/config/mosquitto_serv.crt
# tls_version tlsv1.3
require_certificate true
EOHH

}

genconf () {
  _lo_opt="$1"

  if [ "$_lo_opt" = "force" ]; then
    genconf_mosquitto || return
    return
  fi

  # if [ ! "$_lo_opt" = "absent" ]; then
  #   log_e "Invalid argument"
  #   return 1
  # fi

  if [ ! -f "mosquitto.conf" ]; then
    genconf_mosquitto || return
  fi

}

distclean () {
  for i in mosquitto_ca mosquitto_serv mosquitto_client; do
    for j in .key .crt .csr .cnf; do
      rm -rfv "${i}${j}"
    done
  done
  rm -rfv mosquitto mosquitto.conf mosquitto_shadow.conf \
      genpwd_mosquitto.sh
}

_pri_esg_topic_prefix="gg12345867/872697113110"

esh_sub () {
  _lo_port=$1

  # validate number and larger then 0
  if [ "$(( _lo_port ))" -ne 0 ]; then
    shift
  else
    _lo_port=
  fi

  _lo_args=
  if [ "$_lo_port" = "5883" ] || [ "$_lo_port" = "8883" ]; then
    _lo_args="${_lo_args} --cafile mosquitto_ca.crt --cert mosquitto_client.crt --key mosquitto_client.key"
  fi

  # shellcheck disable=SC2086
  cmd_run mosquitto_sub -h localhost ${_lo_port:+-p $_lo_port} -u pub -P 123456779 \
    ${_lo_args} -t "mqtt/test/tap1" \
    -t "${_pri_esg_topic_prefix}/control/request" \
    -t "${_pri_esg_topic_prefix}/control/response" \
    -t "${_pri_esg_topic_prefix}/status/request" \
    -t "${_pri_esg_topic_prefix}/status/response" \
    -t "${_pri_esg_topic_prefix}/statusChange/response" "$@"
}

esh_pub () {
  _lo_port=$1

  # validate number and larger then 0
  if [ "$(( _lo_port ))" -ne 0 ]; then
    shift
  else
    _lo_port=
  fi

  _lo_args=
  if [ "$_lo_port" = "5883" ] || [ "$_lo_port" = "8883" ]; then
    _lo_args="${_lo_args} --cafile mosquitto_ca.crt --cert mosquitto_client.crt --key mosquitto_client.key"
  fi

  # shellcheck disable=SC2086
  cmd_run mosquitto_pub -h localhost ${_lo_port:+-p $_lo_port} -u pub -P 123456779 \
    ${_lo_args} "$@"
}

show_help () {
  cat <<-EOHELP
USAGE
  ${1:-$(basename "$0")} [OPTIONS] -- [COMMAND]

OPTIONS
  -h, --help        Show this help
  --gencert[=ca|serv|client|absent|force]
      Generate certification for ca, server, client, whatever absent
      or force all. (default: absent)
  --genconf[=absent|force]
      Generate mosquitto config file when absent or force. (default: absent)
  --genpwd[=absent|force]
      Generate mosquitto password file when absent or force. (default: absent)

COMMAND
  esh_sub [PORT] [mosquitto_pub argument -t]
  esh_pub [PORT] [mosquitto_pub arguments -t, -f or -m]
  gencert_client [PREFIX]
  distclean

DESCRIPTION
  Example for generate client certificate:

      _pri_client_cn="192.168.50.123" ${1:-$(basename "$0")} \\
          gencert_client "mosquitto_123"

  Example for subcribe and publish:

      mosquitto_sub -h localhost -p 1883 \\
          -t "${_pri_esg_topic_prefix}/control/request" \\
          -t "${_pri_esg_topic_prefix}/control/response" \\
          -t "mqtt/test/tap1"

      mosquitto_pub -h 192.168.50.123 -p 3883 \\
          -u pub -P 123456779 \\
          -t "mqtt/test/tap1" -m "[\$(date)] abcdef"

      mosquitto_pub -h 192.168.50.123 -p 8883 \\
          --cafile mosquitto_ca.crt \\
          --cert mosquitto_client.crt --key mosquitto_client.key \\
          -t "mqtt/test/tap1" --insecure -m "[\$(date)] abcdef"

EOHELP
}

_pri_opts_long="help,gencert::,genconf::,genpwd::,distclean"
_pri_opts_short="hg::c::p::"
_pri_opts="$(getopt -l $_pri_opts_long -- $_pri_opts_short "$@")" || exit 1
eval set -- "$_pri_opts"
while [ -n "$1" ]; do
  case "$1" in
  -h|--help)
    shift
    show_help
    exit
    ;;
  -g|--gencert)
    _lo_opt=$2
    shift 2
    gencert "$_lo_opt"
    exit
    ;;
  -c|--genconf)
    _lo_opt=$2
    shift 2
    genconf "$_lo_opt"
    exit
    ;;
  -p|--genpwd)
    _lo_opt=$2
    shift 2
    genpwd "$_lo_opt"
    exit
    ;;
  --distclean)
    shift
    for i in mosquitto_ca mosquitto_serv mosquitto_client; do
      for j in .key .crt .csr .cnf; do
        rm -rf ${i}${j}
      done
    done
    rm -rf mosquitto mosquitto.conf mosquitto_shadow.conf \
        genpwd_mosquitto.sh
    exit
    ;;
  --)
    shift
    break
    ;;
  esac
done

case "$1" in
gencert|genconf|genpwd|distclean|esh_sub|esh_pub|gencert_client)
  shift1
  cmd_run "$@"
  exit
  ;;
esac

gencert absent || { log_e "Failed generate cert"; exit; }
genpwd absent || { log_e "Failed generate shadow"; exit; }
genconf absent || { log_e "Failed generate conf"; exit; }

cmd_run mkdir -p mosquitto/config
cmd_run cp -v mosquitto.conf mosquitto_shadow.conf mosquitto_ca.crt \
    mosquitto_serv.key mosquitto_serv.crt \
    mosquitto/config/

cmd_run mkdir -p mosquitto/data mosquitto/log

_lo_opt_port="${_lo_opt_port} -p 1883:1883"
for i in 3883 5883 8883; do
  _lo_opt_port="${_lo_opt_port} -p ${i}:${i}"
done

_lo_opt_vol="${_lo_opt_vol} -v $PWD/mosquitto/config:/mosquitto/config"
for i in mosquitto/data mosquitto/log; do
  if [ -d "$PWD/$i" ]; then
    _lo_opt_vol="${_lo_opt_vol} -v $PWD/$i:/${i}"
  fi
done

_lo_opt_cmd="$*"
if [ -z "$_lo_opt_cmd" ]; then
  _lo_opt_cmd="mosquitto -c /mosquitto/config/mosquitto.conf"
fi

# shellcheck disable=SC2086,SC2090
cmd_run docker run -it ${_lo_opt_port} \
  -v "$PWD:/host" ${_lo_opt_vol} \
  --name mqtt --rm eclipse-mosquitto ${_lo_opt_cmd}

# cmd_run docker compose -f mosquitto.docker-compose.yaml \
#     run --rm -i --name eshmqtt eshmqtt \
#     ${_lo_cmd}
