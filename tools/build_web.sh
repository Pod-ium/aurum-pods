#!/bin/sh
# PicoPods Web: builds every pod for the browser and assembles the site.
#
#   PICO_SDK_PATH=/path/to/pico-sdk PICOPODS_PATH=/path/to/hydrogenium-pods \
#   CARBO_PODS_PATH=/path/to/carbo-pods HELIUM_PODS_PATH=/path/to/helium-pods \
#       tools/build_web.sh [out-dir]
#
# Each repository builds its own pods with its tools/build_web_pods.sh; this
# script runs them and puts the results next to the page (web/). The pods of
# this repository and of hydrogenium-pods (pd_demo) are always included;
# carbo-pods and helium-pods are left out when their path is not set.
# Needs the Emscripten SDK on PATH (source emsdk_env.sh; tested with 3.1.74).
# Output (default dist/web): index.html, app.js, keyboard.js, pods.json and
# one <pod>.pod.js per pod. Serve that folder over HTTP
# (python3 -m http.server -d dist/web) and open it.
set -eu
root=$(cd "$(dirname "$0")/.." && pwd)
out=${1:-$root/dist/web}
build=${WEB_BUILD_DIR:-$root/build-web}
: "${PICOPODS_PATH:?set PICOPODS_PATH to a hydrogenium-pods checkout}"
export PICOPODS_PATH

# <name> <repository>: build that repository's pods into $build/<name>-pods.
part() {
    if [ -z "$2" ]; then
        echo "== $1: path not set, its pods are left out"
        return
    fi
    echo "== $1"
    WEB_BUILD_DIR="$build/$1" "$2/tools/build_web_pods.sh" "$build/$1-pods" >/dev/null
    parts="$parts $build/$1-pods"
}
parts=""
part aurum "$root"
part carbo "${CARBO_PODS_PATH:-}"
part helium "${HELIUM_PODS_PATH:-}"
part hydrogenium "$PICOPODS_PATH"

rm -rf "$out"
mkdir -p "$out"
cp "$root"/web/index.html "$root"/web/app.js "$root"/web/keyboard.js "$out"/
# shellcheck disable=SC2086
python3 - "$out" $parts <<'PY'
import json, os, shutil, sys
out, parts = sys.argv[1], sys.argv[2:]
pods = []
for d in parts:
    for meta in json.load(open(os.path.join(d, "pods.json")))["pods"]:
        shutil.copy(os.path.join(d, meta["name"] + ".pod.js"), out)
        pods.append(meta)
json.dump({"pods": pods}, open(os.path.join(out, "pods.json"), "w"), indent=1)
PY
echo "PicoPods Web: $out"
ls -l "$out"
