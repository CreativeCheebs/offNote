#!/usr/bin/env bash
# Install or update the Offnote AFFiNE sidecar as a systemd --user service.
# Everything the sidecar needs (lib.js, append-core.js, server.js, this
# script) lives in this one directory - copy or git-pull just this folder.
#
# Safe to re-run any number of times:
#   - npm install is idempotent
#   - connectors.json is only ever created from the template, never overwritten
#   - the token in .env is kept as-is unless you pass --new-token
#   - the systemd unit is regenerated and reloaded, not duplicated
#   - the service is restarted (brief, sub-second downtime), not left in a
#     broken half-updated state
#
# Usage:
#   ./install.sh                  # first install, or update after a code pull
#   ./install.sh --port 9000      # change the listening port
#   ./install.sh --new-token      # rotate the sidecar token
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ENV_FILE="$SCRIPT_DIR/.env"
CONNECTORS_FILE="$SCRIPT_DIR/connectors.json"
SERVICE_NAME="offnote-sidecar"
UNIT_FILE="$HOME/.config/systemd/user/${SERVICE_NAME}.service"

NEW_PORT=""
ROTATE_TOKEN=0
while [[ $# -gt 0 ]]; do
  case "$1" in
    --port) NEW_PORT="$2"; shift 2 ;;
    --new-token) ROTATE_TOKEN=1; shift ;;
    *) echo "unknown argument: $1" >&2; exit 1 ;;
  esac
done

echo "==> Installing dependencies"
(cd "$SCRIPT_DIR" && npm install --no-audit --no-fund)

if [[ ! -f "$CONNECTORS_FILE" ]]; then
  echo "==> No connectors.json yet - creating from the example template"
  cp "$SCRIPT_DIR/connectors.example.json" "$CONNECTORS_FILE"
  echo "    Edit $CONNECTORS_FILE with your real AFFiNE credentials before the service can deliver anything."
fi

# Preserve an existing port/token unless explicitly overridden - this is what
# makes re-running safe: nothing here can silently invalidate a client's
# already-configured sidecar_url/sidecar_token.
EXISTING_PORT=""
EXISTING_TOKEN=""
if [[ -f "$ENV_FILE" ]]; then
  EXISTING_PORT="$(grep -m1 '^PORT=' "$ENV_FILE" | cut -d= -f2- || true)"
  EXISTING_TOKEN="$(grep -m1 '^AFFINE_SIDECAR_TOKEN=' "$ENV_FILE" | cut -d= -f2- || true)"
fi

PORT="${NEW_PORT:-${EXISTING_PORT:-8787}}"
if [[ $ROTATE_TOKEN -eq 1 || -z "$EXISTING_TOKEN" ]]; then
  TOKEN="$(openssl rand -hex 24)"
  echo "==> Generated a new sidecar token"
else
  TOKEN="$EXISTING_TOKEN"
fi

cat > "$ENV_FILE" <<EOF
AFFINE_SIDECAR_TOKEN=$TOKEN
PORT=$PORT
EOF
chmod 600 "$ENV_FILE"
echo "==> Wrote $ENV_FILE (port $PORT, gitignored - this is the only copy of the token)"

echo "==> Installing systemd user service"
mkdir -p "$(dirname "$UNIT_FILE")"
cat > "$UNIT_FILE" <<EOF
[Unit]
Description=Offnote AFFiNE sidecar
After=network-online.target

[Service]
Type=simple
WorkingDirectory=$SCRIPT_DIR
ExecStart=$(command -v node) --env-file=.env server.js
Restart=on-failure
RestartSec=5

[Install]
WantedBy=default.target
EOF

systemctl --user daemon-reload
systemctl --user enable "$SERVICE_NAME"
systemctl --user restart "$SERVICE_NAME"

# Lets the service keep running after you log out / disconnect SSH, and come
# back after a reboot - without this, systemd --user units die with your
# session.
if command -v loginctl >/dev/null 2>&1; then
  loginctl enable-linger "$USER" 2>/dev/null || true
fi

sleep 1
echo ""
echo "==> Status"
systemctl --user --no-pager status "$SERVICE_NAME" || true

echo ""
echo "==> Health check"
curl -sf "http://localhost:${PORT}/health" && echo || echo "(failed - check 'journalctl --user -u ${SERVICE_NAME}')"

echo ""
echo "==> Addresses this host is reachable at (pick whichever your client can route to)"
if command -v tailscale >/dev/null 2>&1; then
  TS_IP="$(tailscale ip -4 2>/dev/null || true)"
  [[ -n "$TS_IP" ]] && echo "  Tailscale:  http://$TS_IP:${PORT}"
fi
hostname -I 2>/dev/null | tr ' ' '\n' | grep -v '^$' | while read -r ip; do
  echo "  LAN:        http://$ip:${PORT}"
done

echo ""
echo "==> Sidecar token (goes in each client's sidecar_token config field)"
echo "  $TOKEN"
