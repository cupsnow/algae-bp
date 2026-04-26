log_d () {
  echo "[Debug] $*"
}

for pkg in *; do
  [ -L "$pkg" ] || continue
  # [[ "$pkg" =~ .*-upstream$ ]] && {
  #   log_d "pkg: $pkg"
  #   rm $pkg
  #   continue
  # }
  _lo_tgt="$(readlink $pkg)"
  [[ "$_lo_tgt" =~ ^${HOME}/02_dev/pkgs ]] && {
    log_d "pkg: $pkg -> $_lo_tgt"
    continue
  }
  false && [[ "$_lo_tgt" =~ ^${HOME}/02_dev ]] && {
    _lo_tgt2="${_lo_tgt#${HOME}/02_dev/}"
    log_d "pkg: $pkg -> $_lo_tgt -> $_lo_tgt2"
    echo "ln -sfnv ${HOME}/02_dev/pkgs/${_lo_tgt2} ./$pkg"
    continue
  }
done

