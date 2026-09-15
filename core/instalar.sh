#!/bin/bash
# Instala kciwapp-server en un Debian/Ubuntu limpio (como root):
#   MariaDB + ffmpeg, la base "whatsapp", el binario en /opt/kciwapp-server,
#   config.json con token nuevo, unit de systemd, y arranca.
# Uso: ./instalar.sh [puerto]     (el binario kciwapp-server tiene que estar al lado)
set -euo pipefail
cd "$(dirname "$0")"
PUERTO="${1:-8080}"
DIR=/opt/kciwapp-server

[ "$(id -u)" = 0 ] || { echo "correlo como root"; exit 1; }
[ -f kciwapp-server ] || { echo "falta el binario kciwapp-server al lado de este script"; exit 1; }

echo "== paquetes"
export DEBIAN_FRONTEND=noninteractive
apt-get update -qq
apt-get install -y -qq mariadb-server ffmpeg curl >/dev/null
systemctl enable --now mariadb >/dev/null

echo "== base de datos"
PASS=$(tr -dc 'A-Za-z0-9' </dev/urandom | head -c 24)
mariadb <<SQL
CREATE DATABASE IF NOT EXISTS whatsapp CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;
CREATE USER IF NOT EXISTS 'wa'@'localhost' IDENTIFIED BY '$PASS';
ALTER USER 'wa'@'localhost' IDENTIFIED BY '$PASS';
GRANT ALL ON \`whatsapp%\`.* TO 'wa'@'localhost';
FLUSH PRIVILEGES;
SQL

echo "== binario y config"
mkdir -p "$DIR/media"
install -m 755 kciwapp-server "$DIR/kciwapp-server"
# IP donde escucha: la de la LAN (no todas las interfaces).
IP=$(hostname -I 2>/dev/null | awk '{print $1}')
[ -n "$IP" ] || IP=$(ip -4 addr | grep -o 'inet [0-9.]*' | grep -v 127.0.0.1 | head -1 | cut -d' ' -f2)
if [ ! -f "$DIR/multi.json" ]; then
  cat > "$DIR/multi.json" <<JSON
{
  "multi": true,
  "escucha": "$IP:$PUERTO",
  "dir": "$DIR",
  "mariadb": "wa:$PASS@tcp(127.0.0.1:3306)/%s?parseTime=true&charset=utf8mb4",
  "historia": "reciente"
}
JSON
  chmod 600 "$DIR/multi.json"
  echo "[]" > "$DIR/cuentas.json"
  chmod 600 "$DIR/cuentas.json"
else
  echo "   (multi.json ya existia, no se toca)"
fi

echo "== servicio"
cat > /etc/systemd/system/kciwapp-server.service <<UNIT
[Unit]
Description=kciwapp-server (WhatsApp via whatsmeow)
After=network-online.target mariadb.service
Wants=network-online.target

[Service]
ExecStart=$DIR/kciwapp-server $DIR/multi.json
WorkingDirectory=$DIR
Restart=always
RestartSec=5
LimitNOFILE=65536
KillMode=control-group

[Install]
WantedBy=multi-user.target
UNIT
systemctl daemon-reload
systemctl enable --now kciwapp-server >/dev/null
sleep 2
systemctl is-active kciwapp-server >/dev/null || { echo "no arranco; mira: journalctl -u kciwapp-server"; exit 1; }

IP=$(hostname -I | awk '{print $1}')
echo
echo "Listo."
echo "  Cada usuario pone en su portable\\servidor.json un token propio, largo (20+ caracteres):"
echo "     {\"host\": \"$IP\", \"puerto\": $PUERTO, \"token\": \"<inventar uno largo>\"}"
echo "  Al abrir el cliente, el server crea la cuenta y el cliente muestra el QR para vincular."
echo "  Cuentas: $DIR/cuentas.json (para borrar una: sacarla de ahi, borrar $DIR/cuentas/<id> y DROP DATABASE whatsapp_<id>)."
echo "  Logs: journalctl -u kciwapp-server -f"
