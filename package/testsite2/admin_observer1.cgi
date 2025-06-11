#!/bin/sh

log_file="admin_observer1.log"

log_d () {
  echo "$*"
}

log_f () {
  [ -n "$log_file" ] || return
  [ -n "$1" ] || { echo "" >> $log_file; date >> $log_file; return; }
  echo "$*" >> $log_file
}

log_cmd () {
  [ -n "$1" ] || return
  [ -n "$log_file" ] || { $@; return; }
  log_f "Execute: $@"
  $@ >> $log_file
}

test_json () {
  
  cat <<-EOMSG
HTTP/1.1 201 Ok
Content-Type: application/json

{"name": "ok"}
EOMSG

}

test_plain () {
  
  cat <<-EOMSG
HTTP/1.1 201 Ok
Content-Type: text/plain

$(env)
EOMSG

}

log_f

log_f "$*"

if [ "$1" = "timed" ]; then
  log_f "sleep ${2:-3}"
  sleep ${2:-3}
  log_f "launch killall cat"
  killall cat
  exit
fi

log_f "launch $(basename $0) timed <>"
$0 timed 1 &

log_cmd env

log_cmd cat /dev/stdin

log_f "ending"

# test_json
test_plain

# while read line; do
#   log_f $line
# done < "${1:-/dev/stdin}"
