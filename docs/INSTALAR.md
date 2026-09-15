# kciwapp — instalar en otra máquina

Dos partes: un **server** (Linux, habla con WhatsApp como dispositivo
vinculado) y el **cliente** (Windows, portable, no instala nada).

## 1. Server

En un Debian/Ubuntu limpio (una VM con 2 GB alcanza), como root, con
`kciwapp-server` e `instalar.sh` en la misma carpeta:

```bash
./instalar.sh          # o ./instalar.sh 9000 para otro puerto
```

Instala MariaDB y ffmpeg, crea la base `whatsapp`, deja el binario y el
config en `/opt/kciwapp-server`, un servicio `kciwapp-server` y lo arranca.
Al final imprime la URL del QR y el `servidor.json` para el cliente.

**Slackware 15**: `./instalar-slackware.sh` hace lo mismo con la MariaDB que
trae Slackware y un `/etc/rc.d/rc.kciwapp-server` (start/stop/restart/status)
enganchado en `rc.local`; log en `/var/log/kciwapp-server.log`. ffmpeg hay que
ponerlo antes desde SlackBuilds (`sbopkg -i ffmpeg`); sin el anda igual pero
sin notas de voz convertidas, stickers ni miniaturas de video. El binario es
estatico, corre en cualquier Linux x64.

Vincular el teléfono: el server arranca sin sesión y no genera nada solo.
Al abrir el cliente con el `servidor.json` correcto, el cliente le pide el
QR y lo muestra en su ventana: WhatsApp → Dispositivos vinculados → Vincular
un dispositivo → escanear. Baja lo mismo que WhatsApp Web (los chats
recientes). Para pedir el historial completo (hasta 3 años, solo texto) poner
`"historia": "completa"` en `config.json` **antes** de vincular. Los adjuntos
se bajan cuando se abren.

`config.json`:

| clave            | qué es                                                                 |
|------------------|------------------------------------------------------------------------|
| `escucha`        | `:8080`                                                                |
| `token`          | lo que el cliente manda en `X-Token`; cualquier string largo           |
| `mariadb`        | DSN de la base                                                         |
| `store`          | sesión de WhatsApp (sqlite). **No compartir ni copiar a otro server.** |
| `media`          | carpeta de adjuntos                                                    |
| `historia`       | `reciente` (default) / `completa` / `no`                               |
| `bajar_historia` | bajar de a poco los adjuntos viejos del historial (`false`)            |

Logs: `journalctl -u kciwapp-server -f`. Reiniciar: `systemctl restart kciwapp-server`.

## 2. Cliente

Copiar la carpeta `portable\` (exe, `mpv\`, `WebView2Loader.dll`, `emoji.txt`,
`fondo-wa.webp`, `fondos\`) **sin** `datos\` ni `ajustes.json` (son de cada
usuario: cache, media, sesión de WhatsApp Web). Editar `servidor.json`:

```json
{"host": "192.168.1.10", "puerto": 8080, "token": "el-token-del-config"}
```

Windows 10/11 x64. Las llamadas usan el runtime de WebView2 (Edge), que
Windows 11 ya trae; se prenden en Settings → Calls y piden vincular un
segundo dispositivo (QR ahí mismo).

## Importar historial viejo (opcional, iPhone)

Con un backup sin cifrar del iPhone (3uTools, iMazing, iTunes), en el server:

```bash
kciwapp-server backup /ruta/al/backup    # ChatStorage.sqlite + Message/Media
```

Cruza por id de mensaje, así se puede correr las veces que haga falta.
