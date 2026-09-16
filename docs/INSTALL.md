# kciwapp — instalar en otra máquina

Un solo cliente (Windows, portable). La primera vez que se abre (sin
cuentas) pregunta dónde corre WhatsApp y el token:

- **This computer**: el cliente lanza `core\kciwapp-core.exe` (el server en
  Go con SQLite) al lado suyo, genera un token y muestra el QR. Todo queda
  en `portable\datos\`. WhatsApp está online solo mientras el cliente corre
  (si pasa 14 días cerrado, el teléfono desvincula el dispositivo).
- **A kciwapp server**: host, puerto y token (el botón Generate inventa uno;
  un token nuevo crea una cuenta nueva en el server, el mismo token en otra
  PC comparte la cuenta).

Las cuentas quedan en `cuentas.json` al lado del exe (una por WhatsApp:
host, puerto, token y su carpeta bajo `datos\`). Con una sola entra
directo; con varias pregunta cuál al abrir. Desde el menú `⋯` de la
cabecera se cambia de cuenta o se agrega otra (el cliente se reabre con la
elegida). Para volver a empezar, borrar `cuentas.json` (y `datos\`).

## 0. Cliente local (lo más simple)

Bajar https://github.com/kcinickgx/kciwapp/releases/download/current/kciwapp-setup.exe
y abrirlo: pregunta
dónde instalar (por defecto `C:\Program Files\KciWAPP`; pide permiso de admin y
deja la carpeta escribible para el usuario), baja todo del release `current`
de GitHub (`kciwapp.md5` lista los archivos; el modelo de whisper viene del
repo oficial en Hugging Face), crea los accesos directos y abre kciwapp.
Listo. Necesita Windows 10/11 x64 (WebView2 para las llamadas viene con
Win11). Después el cliente se actualiza solo desde ahí: chequea al abrir y
desde el menú `⋯` → Check for updates.

### Transcripción de notas de voz (opcional)

El botón de transcribir en los audios aparece si existe `whisper\` al lado
del exe, con `whisper-cli.exe` (whisper.cpp con CUDA, para GPU NVIDIA) y el
modelo `ggml-large-v3-turbo.bin`. Son 2,7 GB de los 3 del paquete: el setup
la ofrece con un tilde (sin GPU NVIDIA, destildar). Si no está la carpeta,
el botón no aparece y las actualizaciones tampoco la bajan; borrarla equivale
a destildarla.

## 1. Server

En un Debian/Ubuntu limpio (una VM con 2 GB alcanza), como root, con
`kciwapp-server` e `install.sh` (assets del mismo release) en la misma
carpeta:

```bash
./install.sh          # o ./install.sh 9000 para otro puerto
```

Instala MariaDB y ffmpeg, crea el usuario de base, deja el binario y
`multi.json` en `/opt/kciwapp-server`, un servicio `kciwapp-server` y lo
arranca. Escucha solo en la IP de la LAN.

**Slackware 15**: `./install-slackware.sh` hace lo mismo con la MariaDB que
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

## 2. Cliente contra el server

La misma carpeta `cliente\` (sin `core\` si no se quiere el modo local),
**sin** `datos\`, `cuentas.json` ni `ajustes.json` de otro usuario. Al abrir,
elegir **A kciwapp server** y poner host, puerto y token (o editar
`cuentas.json`):

```json
{"cuentas": [
  {"nombre": "", "host": "192.168.1.10", "puerto": 8080, "token": "un-token-largo-inventado-por-vos", "carpeta": "datos"}
]}
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
