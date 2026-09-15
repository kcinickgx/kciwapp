# kciwapp — instalar en otra máquina

Dos partes: un **server** (Linux; habla con WhatsApp como dispositivo
vinculado, una cuenta por token) y el **cliente** (Windows, portable, no
instala nada).

## 1. Server

En un Debian/Ubuntu limpio (una VM con 2 GB alcanza), como root, con
`kciwapp-server` e `instalar.sh` en la misma carpeta:

```bash
./instalar.sh          # o ./instalar.sh 9000 para otro puerto
```

Instala MariaDB y ffmpeg, crea el usuario de base, deja el binario y
`multi.json` en `/opt/kciwapp-server`, un servicio `kciwapp-server` y lo
arranca. Escucha solo en la IP de la LAN.

**Slackware 15**: `./instalar-slackware.sh` hace lo mismo con la MariaDB que
trae Slackware y un `/etc/rc.d/rc.kciwapp-server` (start/stop/restart/status)
enganchado en `rc.local`; log en `/var/log/kciwapp-server.log`. ffmpeg hay que
ponerlo antes desde SlackBuilds (`sbopkg -i ffmpeg`); sin él anda igual pero
sin notas de voz convertidas, stickers ni miniaturas de video. El binario es
estático, corre en cualquier Linux x64.

### Varias cuentas

El server arranca sin sesión de WhatsApp y no genera nada solo. Cada cliente
se presenta con su **token** (cualquier string de 20+ caracteres, inventado
por el usuario): si el token ya existe, usa esa cuenta; si no, el server le
crea una (base `whatsapp_N`, `store.db` y media propios en
`/opt/kciwapp-server/cuentas/N/`, un proceso hijo en `127.0.0.1:900N`) y el
cliente muestra el QR para vincular: WhatsApp → Dispositivos vinculados →
Vincular un dispositivo → escanear. Baja lo mismo que WhatsApp Web (los chats
recientes); los adjuntos se bajan cuando se abren.

Las cuentas están en `cuentas.json`. Para borrar una: pararlo, sacarla de
ahí, borrar `cuentas/N` y `DROP DATABASE whatsapp_N`.

`multi.json`:

| clave      | qué es                                                                 |
|------------|------------------------------------------------------------------------|
| `escucha`  | `IP:puerto` donde atiende a los clientes                               |
| `dir`      | carpeta base (`/opt/kciwapp-server`)                                   |
| `mariadb`  | DSN con `%s` donde va el nombre de la base de cada cuenta              |
| `historia` | para las cuentas nuevas: `reciente` (default) / `completa` (3 años, texto) / `no` |

Logs: `journalctl -u kciwapp-server -f` (cada línea lleva `[N]` con la cuenta).

## 2. Cliente

Copiar la carpeta `portable\` (exe, `mpv\`, `WebView2Loader.dll`, `emoji.txt`,
`fondo-wa.webp`, `fondos\`) **sin** `datos\` ni `ajustes.json` (son de cada
usuario: cache, media, sesión de WhatsApp Web). Editar `servidor.json`:

```json
{"host": "192.168.1.10", "puerto": 8080, "token": "un-token-largo-inventado-por-vos"}
```

Windows 10/11 x64. Las llamadas usan el runtime de WebView2 (Edge), que
Windows 11 ya trae; se prenden en Settings → Calls y piden vincular un
segundo dispositivo (QR ahí mismo).

## Importar historial viejo (opcional, iPhone)

Con un backup sin cifrar del iPhone (3uTools, iMazing, iTunes), en el server,
contra el config de la cuenta (`cuentas/N/config.json`):

```bash
KCIWAPP_CONFIG=/opt/kciwapp-server/cuentas/N/config.json kciwapp-server backup /ruta/al/backup
```

Cruza por id de mensaje, así se puede correr las veces que haga falta.
