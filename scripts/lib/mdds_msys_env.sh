#!/usr/bin/env bash
# Shared Git-Bash/native-process boundary helpers for MDDS validation runners.

# Return MSYS2_ENV_CONV_EXCL with one exact inherited variable appended once.
# MSYS2 uses this semicolon-delimited list while converting the Bash process
# environment for native Windows children.  The caller intentionally supplies
# only an identifier, never arbitrary text that could alter conversion rules.
mdds_append_msys2_env_conv_excl() {
  local variable_name="$1"
  local list="${MSYS2_ENV_CONV_EXCL:-}"
  if ! [[ "$variable_name" =~ ^[A-Za-z_][A-Za-z0-9_]*$ ]]; then
    return 2
  fi
  case ";${list};" in
    *";${variable_name};"*) ;;
    *) list="${list:+${list};}${variable_name}" ;;
  esac
  printf '%s\n' "$list"
}
