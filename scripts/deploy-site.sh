#!/usr/bin/env bash
# Publishes website/ to https://lynx-share.com (Hostinger static deploy).
# Needs the Hostinger API token in ~/.config/hostinger/token (never commit it).
set -euo pipefail
cd "$(dirname "$0")/../website"
T=$(cat ~/.config/hostinger/token); B=https://developers.hostinger.com
USER_=u258086853; DOMAIN=lynx-share.com
ZIP=$(mktemp -d)/lynx-site.zip
python3 -c "import zipfile,os;z=zipfile.ZipFile('$ZIP','w',zipfile.ZIP_DEFLATED);[z.write(os.path.join(r,x),os.path.relpath(os.path.join(r,x),'.')) for r,d,f in os.walk('.') for x in f];z.close()"
J=$(curl -sf -X POST -H "Authorization: Bearer $T" -H 'Content-Type: application/json' -d "{\"username\":\"$USER_\",\"domain\":\"$DOMAIN\"}" $B/api/hosting/v1/files/upload-urls)
get() { python3 -c "import json,sys;print(json.load(sys.stdin)['$1'])" <<<"$J"; }
URL=$(get url); AK=$(get auth_key); RK=$(get rest_auth_key); SIZE=$(stat -c%s "$ZIP")
H=(-H "X-Auth: $AK" -H "X-Auth-Rest: $RK" -H 'Tus-Resumable: 1.0.0')
curl -sf -o /dev/null -X POST "$URL/lynx-site.zip?override=true" "${H[@]}" -H "Upload-Length: $SIZE" -H 'Upload-Offset: 0'
curl -sf -o /dev/null -X PATCH "$URL/lynx-site.zip?override=true" "${H[@]}" -H 'Upload-Offset: 0' -H 'Content-Type: application/offset+octet-stream' --data-binary @"$ZIP"
curl -sf -X POST -H "Authorization: Bearer $T" -H 'Content-Type: application/json' -d '{"archive_path":"lynx-site.zip"}' $B/api/hosting/v1/accounts/$USER_/websites/$DOMAIN/deploy; echo
# the CDN keeps assets for 30 days: purge it so the new files show up at once
curl -sf -X DELETE -H "Authorization: Bearer $T" "$B/api/hosting/v1/accounts/$USER_/websites/$DOMAIN/cache/clear"; echo
