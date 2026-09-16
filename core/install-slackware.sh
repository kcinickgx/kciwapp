#!/bin/bash
# Instala kciwapp-server en Slackware (15.x), como root, con el binario
# kciwapp-server (estatico) al lado de este script:
#   MariaDB (viene con Slackware) inicializada y en rc.d, la base "whatsapp",
#   el binario en /opt/kciwapp-server, config.json con token nuevo, y
#   /etc/rc.d/rc.kciwapp-server enganchado en rc.local.
# ffmpeg no viene con Slackware: instalarlo antes desde SlackBuilds.org
# (sbopkg -i ffmpeg) o el script avisa y sigue (sin ffmpeg no hay notas de
# voz convertidas, stickers ni miniaturas de video; lo demas anda).
# Uso: ./install-slackware.sh [puerto]
set -euo pipefail
cd "$(dirname "$0")"
PUERTO="${1:-8080}"
DIR=/opt/kciwapp-server

[ "$(id -u)" = 0 ] || { echo "correlo como root"; exit 1; }
[ -f kciwapp-server ] || { echo "falta el binario kciwapp-server al lado de este script"; exit 1; }
command -v ffmpeg >/dev/null || echo "AVISO: no hay ffmpeg (sbopkg -i ffmpeg); sigo sin el"

echo "== MariaDB"
MYSQLD=/etc/rc.d/rc.mysqld
[ -f $MYSQLD ] || { echo "no esta rc.mysqld: instalar el paquete mariadb de Slackware (serie ap)"; exit 1; }
chmod +x $MYSQLD
if [ ! -d /var/lib/mysql/mysql ]; then
  mysql_install_db --user=mysql >/dev/null
  chown -R mysql:mysql /var/lib/mysql
fi
$MYSQLD status >/dev/null 2>&1 || { $MYSQLD start; sleep 3; }
MARIADB=$(command -v mariadb || command -v mysql)

echo "== base de datos"
PASS=$(tr -dc 'A-Za-z0-9' </dev/urandom | head -c 24)
$MARIADB <<SQL
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

echo "== rc.d"
cat > /etc/rc.d/rc.kciwapp-server <<'RC'
#!/bin/sh
# kciwapp-server: start | stop | restart | status
DIR=/opt/kciwapp-server
PID=/var/run/kciwapp-server.pid
LOG=/var/log/kciwapp-server.log
start() {
  if [ -f $PID ] && kill -0 "$(cat $PID)" 2>/dev/null; then echo "ya corre"; return; fi
  # Espera a MariaDB.
  for i in $(seq 1 30); do /etc/rc.d/rc.mysqld status >/dev/null 2>&1 && break; sleep 1; done
  cd $DIR
  nohup ./kciwapp-server $DIR/multi.json >>$LOG 2>&1 &
  echo $! > $PID
  echo "kciwapp-server arrancado (log: $LOG)"
}
stop() {
  # SIGTERM al padre: el cierra a los hijos (uno por cuenta).
  [ -f $PID ] && kill "$(cat $PID)" 2>/dev/null; rm -f $PID
  sleep 2; pkill -f "$DIR/kciwapp-server" 2>/dev/null || true
}
case "$1" in
  start) start ;;
  stop) stop ;;
  restart) stop; sleep 1; start ;;
  status) [ -f $PID ] && kill -0 "$(cat $PID)" 2>/dev/null && echo "corriendo (pid $(cat $PID))" || echo "parado" ;;
  *) echo "uso: $0 start|stop|restart|status"; exit 1 ;;
esac
RC
chmod +x /etc/rc.d/rc.kciwapp-server
grep -q rc.kciwapp-server /etc/rc.d/rc.local || cat >> /etc/rc.d/rc.local <<'LOCAL'

# kciwapp-server (WhatsApp)
if [ -x /etc/rc.d/rc.kciwapp-server ]; then /etc/rc.d/rc.kciwapp-server start; fi
LOCAL
grep -q rc.kciwapp-server /etc/rc.d/rc.local_shutdown 2>/dev/null || cat >> /etc/rc.d/rc.local_shutdown <<'LOCAL'

if [ -x /etc/rc.d/rc.kciwapp-server ]; then /etc/rc.d/rc.kciwapp-server stop; fi
LOCAL
chmod +x /etc/rc.d/rc.local_shutdown
/etc/rc.d/rc.kciwapp-server restart
sleep 2
/etc/rc.d/rc.kciwapp-server status | grep -q corriendo || { echo "no arranco; mira /var/log/kciwapp-server.log"; exit 1; }

echo
echo "Listo."
echo "  Cada usuario pone en su portable\\servidor.json un token propio, largo (20+ caracteres):"
echo "     {\"host\": \"$IP\", \"puerto\": $PUERTO, \"token\": \"<inventar uno largo>\"}"
echo "  Al abrir el cliente, el server crea la cuenta y el cliente muestra el QR para vincular."
echo "  Cuentas: $DIR/cuentas.json (para borrar una: sacarla de ahi, borrar $DIR/cuentas/<id> y DROP DATABASE whatsapp_<id>)."
echo "  Log: /var/log/kciwapp-server.log   Control: /etc/rc.d/rc.kciwapp-server start|stop|restart|status"
