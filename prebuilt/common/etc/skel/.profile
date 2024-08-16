#!/bin/sh

if type resize >/dev/null 2>&1; then
  alias rs='eval "$(resize)"'
  eval "$(resize)"
fi

