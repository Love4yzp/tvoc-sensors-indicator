#!/usr/bin/env bash
# 上传 ESP-IDF 构建产物到 esp-flash-hub
# 通用脚本：拷到任何 ESP-IDF 项目的 scripts/ 下即可用，全部通过环境变量/参数配置。
#
# 用法:
#   ./scripts/upload_to_flasher.sh [version]
# version 缺省为 git describe（无 git 则用时间戳）。
#
# 环境变量:
#   FLASHER_URL       服务地址          (默认 https://flasher.love4yzpcat.heiyu.space,
#                      即懒猫网关国内入口; 也可用 https://flasher.love4yzp.me)
#   FLASHER_USER      管理账号          (默认 spencer)
#   FLASHER_PASSWORD  管理密码          (必填；可写进仓库根目录的 .flasher.env,
#                      该文件已在 .gitignore 中，不会提交)
#   FLASHER_PROJECT   项目 id           (默认当前目录名的小写连字符形式)
#   FLASHER_NAME      项目显示名        (默认同 PROJECT，仅创建时生效)
#   FLASHER_FIRMWARE  固件类型 id       (默认 app)
#   FLASHER_CHIP      芯片型号          (默认从 build 产物探测, 如 ESP32-S3)
#   FLASHER_SSH       SSH 主机          (可选, 如 root@host；设置后固件走 SSH 传到
#                      服务器本地上传, 绕过 Cloudflare Tunnel, 大文件快得多)
#
# 项目/固件类型不存在时会自动创建。
set -euo pipefail
cd "$(dirname "$0")/.."

# 本地凭据文件（不提交）：FLASHER_USER=... / FLASHER_PASSWORD=...
[[ -f .flasher.env ]] && source .flasher.env

BASE="${FLASHER_URL:-https://flasher.love4yzpcat.heiyu.space}"
USER="${FLASHER_USER:-spencer}"
PASS="${FLASHER_PASSWORD:?请设置 FLASHER_PASSWORD}"
PROJECT="${FLASHER_PROJECT:-$(basename "$PWD" | tr 'A-Z' 'a-z' | tr -c 'a-z0-9' '-' | sed 's/--*/-/g;s/^-//;s/-$//')}"
FIRMWARE="${FLASHER_FIRMWARE:-app}"
VERSION="${1:-$(git describe --tags --always --dirty 2>/dev/null || date +%Y%m%d%H%M%S)}"
SSH_HOST="${FLASHER_SSH:-}"

[[ -f build/flash_args ]] || { echo "找不到 build/flash_args，先执行 idf.py build" >&2; exit 1; }

# 芯片型号探测（可用 FLASHER_CHIP 覆盖）
detect_chip() {
  local target
  target=$(sed -n 's/.*"target"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/p' build/project_description.json 2>/dev/null | head -1)
  [[ -z "$target" && -f build/flasher_args.json ]] && \
    target=$(sed -n 's/.*"target"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/p' build/flasher_args.json | head -1)
  case "${target:-}" in
    esp32)   echo ESP32 ;;
    esp32s2) echo ESP32-S2 ;;
    esp32s3) echo ESP32-S3 ;;
    esp32c3) echo ESP32-C3 ;;
    esp32c6) echo ESP32-C6 ;;
    esp32h2) echo ESP32-H2 ;;
    *)       echo "${target:-ESP32}" ;;
  esac
}
CHIP="${FLASHER_CHIP:-$(detect_chip)}"

# 解析分区表: FIELDS/FLOCAL 一一对应, PARTS 为 JSON
FIELDS=(); FLOCAL=(); PARTS="["; i=0
while read -r offset bin; do
  [[ "$offset" == 0x* ]] || continue
  file="build/$bin"
  [[ -f "$file" ]] || { echo "缺少文件: $file" >&2; exit 1; }
  field="part$i"
  FIELDS+=("$field"); FLOCAL+=("$file")
  (( i > 0 )) && PARTS+=","
  PARTS+="{\"field\":\"$field\",\"offset\":$((offset))}"
  i=$((i + 1))
done < build/flash_args
PARTS+="]"
(( i > 0 )) || { echo "flash_args 中没有可上传的分区" >&2; exit 1; }

if [[ -n "$SSH_HOST" ]]; then
  # ---- SSH 模式：固件 scp 到服务器，在服务器本地 curl localhost 上传 ----
  REMOTE_DIR="/tmp/esp-flash-hub-upload-$$"
  ssh -o BatchMode=yes "$SSH_HOST" "mkdir -p '$REMOTE_DIR'"
  trap 'ssh -o BatchMode=yes "$SSH_HOST" "rm -rf \"$REMOTE_DIR\"" 2>/dev/null || true' EXIT
  scp -o BatchMode=yes -q "${FLOCAL[@]}" "$SSH_HOST:$REMOTE_DIR/"

  CURL_ARGS=""
  for j in "${!FIELDS[@]}"; do
    CURL_ARGS+=" -F $(printf '%q' "${FIELDS[$j]}=@$REMOTE_DIR/$(basename "${FLOCAL[$j]}")")"
  done

  echo "上传版本 $VERSION ($i 个分区) -> $SSH_HOST:localhost [$PROJECT/$FIRMWARE]"
  # 项目/固件类型须已存在（先以 HTTPS 模式跑过一次或后台已建好）
  ssh -o BatchMode=yes "$SSH_HOST" \
    "curl -sS -f -u $(printf '%q' "$USER:$PASS") \
       -F $(printf '%q' "version=$VERSION") -F $(printf '%q' "parts=$PARTS") $CURL_ARGS \
       $(printf '%q' "http://localhost:3000/api/projects/$PROJECT/firmwares/$FIRMWARE/versions")"
  echo
  exit 0
fi

# ---- HTTPS 模式：全部走公网 API ----
# 本机代理可能劫持域名 DNS，--resolve 固定到 Cloudflare 边缘 IP（仅 flasher.love4yzp.me）
# 固定字符串而非数组：macOS bash 3.2 对空数组展开在 set -u 下报错
RESOLVE=""
case "$BASE" in *flasher.love4yzp.me*) RESOLVE="--resolve flasher.love4yzp.me:443:104.21.72.6";; esac

API="$BASE/api/projects"
AUTH=(-sS -f -u "$USER:$PASS")

LIST=$(curl $RESOLVE "${AUTH[@]}" "$API")

# 确保项目存在
if ! grep -q "\"id\":\"$PROJECT\"" <<< "$LIST"; then
  echo "创建项目 $PROJECT"
  curl $RESOLVE "${AUTH[@]}" -X POST "$API" \
    -H 'Content-Type: application/json' \
    -d "{\"name\":\"${FLASHER_NAME:-$PROJECT}\",\"description\":\"\"}" >/dev/null
fi

# 确保固件类型存在（只在该项目段内匹配；dumps 用紧凑分隔符保证 grep 可命中）
PROJECT_JSON=$(python3 -c 'import json,sys; ps=json.load(sys.stdin); print(json.dumps(next((p for p in ps if p["id"]==sys.argv[1]), {}), separators=(",",":")))' "$PROJECT" <<< "$LIST" 2>/dev/null || echo '{}')
if ! grep -q "\"id\":\"$FIRMWARE\"" <<< "$PROJECT_JSON"; then
  echo "创建固件类型 $FIRMWARE ($CHIP)"
  curl $RESOLVE "${AUTH[@]}" -X POST "$API/$PROJECT/firmwares" \
    -H 'Content-Type: application/json' \
    -d "{\"name\":\"$FIRMWARE\",\"description\":\"\",\"chipFamily\":\"$CHIP\"}" >/dev/null
fi

CURL_ARGS=()
for j in "${!FIELDS[@]}"; do
  CURL_ARGS+=(-F "${FIELDS[$j]}=@${FLOCAL[$j]}")
done

echo "上传版本 $VERSION ($i 个分区) -> $BASE [$PROJECT/$FIRMWARE]"
curl $RESOLVE "${AUTH[@]}" \
  -F "version=$VERSION" -F "parts=$PARTS" "${CURL_ARGS[@]}" \
  "$API/$PROJECT/firmwares/$FIRMWARE/versions"
echo
