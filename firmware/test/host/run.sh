#!/bin/sh
# Host-side tests for the parts of the firmware that need no hardware:
# session storage, request validation, MQTT discovery payloads.
#   sh firmware/test/host/run.sh
set -e
here=$(cd "$(dirname "$0")" && pwd)
src="$here/../../src"
tmp=$(mktemp -d)
trap 'rm -r "$tmp"' EXIT
mkdir "$tmp/data"

# The pure helpers of web.c, lifted out so they compile without the HTTP server.
python3 - "$src/web.c" "$tmp/webfn.h" <<'PY'
import re, sys
src = open(sys.argv[1]).read()
out = "#include <ctype.h>\n#include <stdbool.h>\n#include <stdio.h>\n#include <string.h>\n#include <strings.h>\n"
for name in ("ends_with", "host_allowed", "printable", "topic_chars", "valid_id", "uri_session_id", "url_ok"):
    m = re.search(r"\nstatic [^\n]*\b" + name + r"\([^\n]*\)\n\{.*?\n\}\n", src, re.S)
    assert m, name
    out += m.group(0)
open(sys.argv[2], "w").write(out)
PY

cc -Wall -Wno-unused-function -Wno-unused-variable -DSTORE_BASE="\"$tmp/data\"" \
   -I "$here/stub" -I "$src" -I "$tmp" "$here/store_web_test.c" "$src/store.c" -o "$tmp/store_web"
"$tmp/store_web"

cc -w -I "$here/stub" -I "$src" "$here/mqtt_test.c" -o "$tmp/mqtt"
"$tmp/mqtt" > "$tmp/mqtt.out"
python3 - "$tmp/mqtt.out" <<'PY'
import json, sys
disc = live = 0
for line in open(sys.argv[1]):
    topic, payload = line.rstrip("\n").split("\t", 1)
    if topic.startswith("homeassistant/"):
        d = json.loads(payload); disc += 1
        assert d["unique_id"] and ("state_topic" in d or "command_topic" in d), d
    elif topic.endswith("/live"):
        json.loads(payload); live += 1
assert disc == 23 and live == 1, (disc, live)
print("MQTT OK: %d discovery messages" % disc)
PY
