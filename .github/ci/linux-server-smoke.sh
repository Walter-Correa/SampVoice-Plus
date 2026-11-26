#!/usr/bin/env bash
set -euo pipefail
export DEBIAN_FRONTEND=noninteractive

# Keep debian:bullseye base so runtime checks match glibc <= 2.31; use archive.debian.org
# with deb.debian.org fallback (without bullseye-security) during Debian 11 archive migration.
dpkg --add-architecture i386
cat > /etc/apt/sources.list <<'EOF'
deb http://archive.debian.org/debian bullseye main
deb http://archive.debian.org/debian bullseye-updates main
deb http://archive.debian.org/debian-security bullseye-security main
EOF
rm -f /etc/apt/sources.list.d/*

if ! apt-get -o Acquire::Check-Valid-Until=false update </dev/null; then
  echo "archive.debian.org update failed; falling back to deb.debian.org (without bullseye-security)..."
  cat > /etc/apt/sources.list <<'EOF'
deb http://deb.debian.org/debian bullseye main
deb http://deb.debian.org/debian bullseye-updates main
EOF
  apt-get -o Acquire::Check-Valid-Until=false update </dev/null
fi

apt-get -o Acquire::Check-Valid-Until=false install -y --no-install-recommends \
  ca-certificates curl unzip tar gzip iproute2 \
  libc6:i386 libstdc++6:i386 libgcc-s1:i386 </dev/null

WORK="/tmp/svplus-smoke"
rm -rf "$WORK"
mkdir -p "$WORK/dl" "$WORK/inc" "$WORK/pkg"
unzip -q /workspace/linux-pkg/SampVoice-Plus-Server-Linux.zip -d "$WORK/pkg"

SAMP_URL="https://raw.githubusercontent.com/Se8870/SAMP-File-Archive/master/archives/samp037svr_R2-2-1.tar.gz"
SAMP_SHA="83b274c214aa42331fa147ded69278658816e8f4fdce6f285e3bcd0198e1e596"
SAMP_DL_URL="https://raw.githubusercontent.com/KrustyKoyle/files.sa-mp.com-Archive/master/samp03DLsvr_R1.tar.gz"
SAMP_DL_SHA="59db9845b0c28688ff4da35970b36da12d962021ffb372e14ee3559d59ce3ece"
PR_URL="https://github.com/katursis/Pawn.RakNet/releases/download/1.6.0/pawnraknet-1.6.0-linux.tar.gz"
PR_SHA="25727f11ed0c320469f1026dd2f727956203dca053b5dc82fcb83502b9a8b3d9"
SAMP_INC_URL="https://raw.githubusercontent.com/Se8870/SAMP-File-Archive/master/archives/samp037_svr_R2-2-1_win32.zip"
SAMP_INC_SHA="781db1d35a3d74fee84952d8d22731c8abbc78cc6baa571b65bd4d5d366fbe78"
PAWNC_URL="https://github.com/pawn-lang/compiler/releases/download/v3.10.10/pawnc-3.10.10-linux.tar.gz"
PAWNC_SHA="9bbb1df6e933318fce1fa61951e4196b70613c637a5a1d4c96937e147c18468d"

curl -fsSL "$SAMP_URL"     -o "$WORK/dl/samp037svr.tar.gz"
curl -fsSL "$SAMP_DL_URL"  -o "$WORK/dl/samp03DLsvr_R1.tar.gz"
curl -fsSL "$PR_URL"       -o "$WORK/dl/pawnraknet.tar.gz"
curl -fsSL "$SAMP_INC_URL" -o "$WORK/dl/sampwin32.zip"
curl -fsSL "$PAWNC_URL"    -o "$WORK/dl/pawnc.tar.gz"

printf "%s  %s\n" \
  "$SAMP_SHA"     "$WORK/dl/samp037svr.tar.gz" \
  "$SAMP_DL_SHA"  "$WORK/dl/samp03DLsvr_R1.tar.gz" \
  "$PR_SHA"       "$WORK/dl/pawnraknet.tar.gz" \
  "$SAMP_INC_SHA" "$WORK/dl/sampwin32.zip" \
  "$PAWNC_SHA"    "$WORK/dl/pawnc.tar.gz" | sha256sum -c -

mkdir -p "$WORK/dl_dl"
tar -xzf "$WORK/dl/samp037svr.tar.gz"     -C "$WORK/dl"
tar -xzf "$WORK/dl/samp03DLsvr_R1.tar.gz" -C "$WORK/dl_dl"
tar -xzf "$WORK/dl/pawnraknet.tar.gz"     -C "$WORK/dl"
tar -xzf "$WORK/dl/pawnc.tar.gz"          -C "$WORK/dl"
unzip -q -j "$WORK/dl/sampwin32.zip" "pawno/include/*.inc" -d "$WORK/inc"
cp "$WORK/dl/Pawn.RakNet.inc" "$WORK/inc/"
cp "$WORK/pkg/include/sampvoice.inc" "$WORK/inc/"

printf "%s\n" \
  "#include <a_samp>" \
  "#include <Pawn.RakNet>" \
  "#include <sampvoice>" \
  "main() {}" \
  "public OnGameModeInit() {" \
  "  SvDebug(true);" \
  "  SvInit(24000);" \
  "  SvCreateGStream(0xFFFFFFFF, \"SmokeGlobal\");" \
  "  SvGetPlusClientVersion(0);" \
  "  SvIsPlayerVoiceReady(0);" \
  "  SvIsPlayerSpeaking(0);" \
  "  SvGetPlayerVoiceStatus(0);" \
  "  SvIsValidStream(0);" \
  "  SvIsValidEffect(0);" \
  "  SvMutePlayerForListener(0, 0);" \
  "  SvUnmutePlayerForListener(0, 0);" \
  "  SvIsPlayerMutedForListener(0, 0);" \
  "  SvStreamSetListenerVolume(0, 0, 1.0);" \
  "  SvStreamGetListenerVolume(0, 0);" \
  "  SvGetStreamListenerCount(0);" \
  "  SvGetStreamSpeakerCount(0);" \
  "  printf(\"SmokeAPI natives=72\");" \
  "  return 1;" \
  "}" \
  "public SV_VOID:OnPlayerVoiceReady(SV_UINT:playerid) {}" \
  "public SV_VOID:OnPlayerVoiceStart(SV_UINT:playerid) {}" \
  "public SV_VOID:OnPlayerVoiceStop(SV_UINT:playerid) {}" \
  "IPacket:207(playerid, BitStream:bs) { return 1; }" > "$WORK/smoke.pwn"

printf "%s\n" \
  "#include <a_samp>" \
  "#include <sampvoice>" \
  "main() {}" \
  "public OnGameModeInit() {" \
  "  SvDebug(true);" \
  "  SvInit(24000);" \
  "  SvGetPlusClientVersion(0);" \
  "  SvIsPlayerVoiceReady(0);" \
  "  SvIsPlayerSpeaking(0);" \
  "  SvGetPlayerVoiceStatus(0);" \
  "  SvIsValidStream(0);" \
  "  SvIsValidEffect(0);" \
  "  SvMutePlayerForListener(0, 0);" \
  "  SvUnmutePlayerForListener(0, 0);" \
  "  SvIsPlayerMutedForListener(0, 0);" \
  "  SvStreamSetListenerVolume(0, 0, 1.0);" \
  "  SvStreamGetListenerVolume(0, 0);" \
  "  SvGetStreamListenerCount(0);" \
  "  SvGetStreamSpeakerCount(0);" \
  "  printf(\"SmokeAPI natives=72\");" \
  "  printf(\"SmokeDL stream=%d\", _:SvCreateGStream(0xFFFFFFFF, \"SmokeGlobalDL\"));" \
  "  return 1;" \
  "}" \
  "public SV_VOID:OnPlayerVoiceReady(SV_UINT:playerid) {}" \
  "public SV_VOID:OnPlayerVoiceStart(SV_UINT:playerid) {}" \
  "public SV_VOID:OnPlayerVoiceStop(SV_UINT:playerid) {}" > "$WORK/smoke_dl.pwn"

PAWNC_DIR="$WORK/dl/pawnc-3.10.10-linux"
chmod +x "$PAWNC_DIR/bin/pawncc"
LD_LIBRARY_PATH="$PAWNC_DIR/lib" "$PAWNC_DIR/bin/pawncc" \
  "$WORK/smoke.pwn" "-i$WORK/inc" "-o$WORK/smoke.amx" "-;+" "-(+"
LD_LIBRARY_PATH="$PAWNC_DIR/lib" "$PAWNC_DIR/bin/pawncc" \
  "$WORK/smoke_dl.pwn" "-i$WORK/inc" "-o$WORK/smoke_dl.amx" "-;+" "-(+"

EX_OUT=$(LD_LIBRARY_PATH="$PAWNC_DIR/lib" "$PAWNC_DIR/bin/pawncc" \
  "$WORK/pkg/Example.pwn" "-i$WORK/inc" "-o$WORK/Example.amx" "-;+" "-(+" 2>&1) || {
  echo "ERROR: Example.pwn compilation failed:"
  echo "$EX_OUT"
  exit 1
}
echo "$EX_OUT"
if printf "%s" "$EX_OUT" | grep -qiF "warning"; then
  echo "ERROR: Example.pwn produced compiler warnings; failing smoke."
  exit 1
fi

run_order() {
  local label="$1"
  local src_dir="$2"
  local amx_file="$3"
  local plugins_line="$4"
  local samp_port="$5"
  local voice_port="$6"
  local expected_log="$7"
  local rcon_pw
  rcon_pw="$(od -An -tx1 -N12 /dev/urandom | tr -d " \n")"

  local rundir="$WORK/run_${label}"
  rm -rf "$rundir"
  cp -a "$src_dir" "$rundir"
  mkdir -p "$rundir/plugins" "$rundir/gamemodes"
  cp "$WORK/pkg/plugins/sampvoice.so" "$rundir/plugins/sampvoice.so"
  if printf "%s" "$plugins_line" | grep -qF "pawnraknet.so"; then
    cp "$WORK/dl/pawnraknet.so" "$rundir/plugins/pawnraknet.so"
  fi
  cp "$amx_file"                      "$rundir/gamemodes/smoke.amx"
  chmod +x "$rundir/samp03svr"

  printf "%s\n" \
    "lanmode 0" \
    "rcon_password ${rcon_pw}" \
    "maxplayers 10" \
    "port ${samp_port}" \
    "hostname svplus-smoke-${label}" \
    "gamemode0 smoke 1" \
    "plugins ${plugins_line}" \
    "sampvoice.port ${voice_port}" > "$rundir/server.cfg"

  pushd "$rundir" >/dev/null
  set +e
  timeout 45s ./samp03svr </dev/null >/dev/null 2>&1 &
  local tpid=$!
  sleep 3
  ss -ulpn | tee "/workspace/smoke-logs/ss_${label}.txt"
  wait "$tpid"
  local rc=$?
  set -e
  popd >/dev/null

  cp "$rundir/server_log.txt" "/workspace/smoke-logs/server_log_${label}.txt"
  echo "=== [${label}] exit code = ${rc} ==="
  cat "$rundir/server_log.txt"

  if [ "$rc" -ne 124 ]; then
    echo "ERROR: Expected timeout exit code 124 for ${label}, got ${rc}"
    exit 1
  fi
  if ! grep -qE "\[sv\+:INF\] SampVoice Plus [^ ]+ voice server running on UDP port ${voice_port}\b" "$rundir/server_log.txt"; then
    echo "ERROR: Missing [sv+:INF] voice server running on UDP port ${voice_port} in ${label} server_log.txt"
    exit 1
  fi
  EFFECTIVE_COUNT=$(grep -cF "[sv+:DBG] Effective config:" "$rundir/server_log.txt" || true)
  if [ "$EFFECTIVE_COUNT" -ne 1 ]; then
    echo "ERROR: Expected '[sv+:DBG] Effective config:' once in ${label} server_log.txt, found ${EFFECTIVE_COUNT}"
    exit 1
  fi
  if grep -qF "[sv+:ERR]" "$rundir/server_log.txt"; then
    echo "ERROR: Unexpected [sv+:ERR] in ${label} server_log.txt"
    exit 1
  fi
  if grep -qiF "not registered" "$rundir/server_log.txt"; then
    echo "ERROR: Found 'not registered' in ${label} server_log.txt"
    exit 1
  fi
  if grep -qiF "File or function is not found" "$rundir/server_log.txt"; then
    echo "ERROR: Found 'File or function is not found' in ${label} server_log.txt"
    exit 1
  fi
  if ! grep -qF "SmokeAPI natives=72" "$rundir/server_log.txt"; then
    echo "ERROR: Expected 'SmokeAPI natives=72' not found in ${label} server_log.txt"
    exit 1
  fi
  if ! grep -q "$expected_log" "$rundir/server_log.txt"; then
    echo "ERROR: Expected log '${expected_log}' not found in ${label} server_log.txt"
    exit 1
  fi
  if [ "$(printf '%s' "$plugins_line" | tr -s ' ' ' ' | sed 's/^ //;s/ $//')" = "sampvoice.so" ]; then
    if grep -qF "owned by another plugin" "$rundir/server_log.txt"; then
      echo "ERROR: Unexpected 'owned by another plugin' in ${label} server_log.txt"
      exit 1
    fi
  fi
  if ! grep -qE ":${voice_port}\b" "/workspace/smoke-logs/ss_${label}.txt"; then
    echo "ERROR: Voice UDP port ${voice_port} not found in ss -ulpn output for ${label}"
    exit 1
  fi
}

run_order "pawnraknet_first" "$WORK/dl/samp03"    "$WORK/smoke.amx"    "pawnraknet.so sampvoice.so" 17785 18895 "Pawn\.RakNet"
run_order "sampvoice_first"  "$WORK/dl/samp03"    "$WORK/smoke.amx"    "sampvoice.so pawnraknet.so" 17786 18896 "Pawn\.RakNet"
run_order "samp03dl"         "$WORK/dl_dl/samp03" "$WORK/smoke_dl.amx" "sampvoice.so"               17787 18897 "SmokeDL stream="
