#!/bin/sh

export LANG=C.utf8

if type resize >/dev/null 2>&1; then
  alias rs='eval "$(resize)"'
  eval "$(resize)"
fi

