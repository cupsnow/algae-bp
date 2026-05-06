log_d () {
  echo "[Debug] $*"
}

chln_v1 () {
  for pkg in *; do
    [ -L "$pkg" ] || {
      log_d "$pkg not symlink"
      continue
    }
    _lo_tgt="$(readlink $pkg)"
    true && [ "$(dirname $_lo_tgt)" = ".." ] && {
      _lo_tgt2="${HOME}/02_exdev2/pkgs/$(basename $_lo_tgt)"
      [ -e "$_lo_tgt2" ] || {
        log_d "$pkg: $_lo_tgt -> ${_lo_tgt2} missing"
        continue
      }
      log_d "$pkg: $_lo_tgt -> ${_lo_tgt2}"
      # ln -sfn "${_lo_tgt2}" $pkg
    }
  done
}
