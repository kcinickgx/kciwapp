# Plan: reemplazar Matrix por un servidor propio con whatsmeow

Decidido el 2026-09-14. Matrix (Synapse + mautrix-whatsapp + matrix-rust-sdk)
queda como relay caro: el bridge ya habla WhatsApp con `whatsmeow`; todo lo
demas traduce a un modelo ajeno (rooms, fantasmas, LIDs, Megolm, sliding sync,
cache del SDK, indice encima del cache). Se reemplaza por un servidor Go que
usa `whatsmeow` directo y un cliente que sincroniza por HTTP/JSON.

Mientras tanto, lo actual (Matrix, VM 104 = 192.168.5.18) sigue andando y
kciwapp sigue apuntando ahi. Se corta recien cuando lo nuevo ande unos dias.

## Servidor: VM 192.168.5.15 (hostname `whatsapp`)

- Debian 13, 8 nucleos, 3,8 GB RAM, 111 GB libres. SSH puerto 22122, root con
  llave. IP fija en `/etc/network/interfaces` (gw y DNS 192.168.5.2);
  `dhcpcd` deshabilitado porque pisaba `/etc/resolv.conf` (ahora `.2` y
  `1.1.1.1` a mano).
- Ya instalado: `golang-go` 1.24, `git`, `sqlite3`, `build-essential`,
  `curl`, `mariadb-server` (activo).
- Sin Docker: binario Go como servicio systemd (`kciwapp-server`), directorio
  `/opt/kciwapp-server/` con el binario, `store.db` (SQLite de whatsmeow) y
  `media/`.

### Bases

- **whatsmeow** (sesion, claves Signal, contactos): su propio store en
  **SQLite** (`/opt/kciwapp-server/store.db`). No tocar: es lo que menos
  probado esta con MySQL y es chico.
- **Nuestra** (lo que consume el cliente): **MariaDB**, base `whatsapp`,
  usuario `wa`, password local en `/opt/kciwapp-server/config.json`. Tablas:
  - `chats` (jid, nombre, es_grupo, foto, ultimo_ts, no_leidos, archivado)
  - `contactos` (jid, lid, telefono, nombre_push, nombre_agenda, foto)
  - `mensajes` (id_wa, chat, remitente, ts, tipo, texto, cita_id, media_id,
    editado, borrado, estado_entrega 0..3 = enviado/servidor/entregado/leido)
  - `reacciones` (mensaje, remitente, emoji, ts)
  - `media` (id, mensaje, mime, nombre, bytes, ancho, alto, segundos, ruta
    local, estado 0..2 = pendiente/bajado/fallo)
  - `eventos` (seq autoincrement, ts, tipo, json): **el log secuencial** que
    el cliente consume por cursor. Cada cambio (mensaje nuevo, edicion,
    borrado, reaccion, acuse, chat nuevo, contacto) es una fila.

### API HTTP (puerto 8080, LAN, token simple en header)

- `GET  /estado`            conectado / hace falta QR / logueado como
- `GET  /qr`                PNG o texto del QR mientras no hay sesion
- `GET  /chats`             lista completa
- `GET  /mensajes?chat=&antes=&limite=`   paginado hacia atras
- `GET  /eventos?desde=<seq>`             long-poll (hasta 25 s) o SSE
- `GET  /media/<id>`        el archivo descifrado (baja de WA si falta)
- `POST /enviar`            {chat, texto, cita?}  |  multipart con archivo
                            (foto/video/documento/nota de voz .ogg opus)
- `POST /reaccion`, `POST /editar`, `POST /borrar`, `POST /leido`
- `POST /importar`          un export de iOS (`_chat.txt` + archivos) para
                            un chat; entra a las mismas tablas

### Orden de trabajo

1. Servidor: modulo Go `kciwapp-server` (repo `C:\Projects\kciwapp-server`,
   se compila en la VM). Vincular con QR (una sola vez; **no** encadenar
   re-vinculaciones, es lo unico que sube el riesgo de ban), recibir
   mensajes a MariaDB, bajar media, `/chats`, `/mensajes`, `/eventos`,
   `/enviar`. Probar con `curl`.
2. History sync: HECHO el 2026-09-14 (vinculado, 5493704259609). El
   texto de la historia llego solo del celu (cientos de miles de mensajes,
   pesa poco). **La media vieja NO se baja de internet** (el usuario no
   quiere bajar 3 anos por la red, y ademas el CDN corta con 403 al
   ritmo): `bajar_historia: false` en config.json; un adjunto viejo se
   baja recien cuando se abre en la app. La media de la historia sale del
   export del celu (paso 4). Los mensajes nuevos si bajan su media al toque.
3. Cliente: reemplazar `src/matrix.rs` (3.500 lineas) por `src/servidor.rs`:
   primera vez baja chats + mensajes (media a demanda), despues consume
   `/eventos` con el cursor guardado. Debe producir los mismos `Sala` /
   `Mensaje` / `Adjunto` / `Citado` que hoy, asi `main.rs`, `base.rs`
   (indice local, busqueda sin acentos, alturas), `media.rs`, `audio.rs`,
   `aviso.rs`, `sistema.rs` y `ajustes.rs` quedan como estan. Se van: el SDK
   de Matrix, cifrado, recovery key, fantasmas, LIDs, huecos, tildes por
   eventos `com.beeper.*`.
4. Importador de exports de iOS. Formato ya analizado con `C:\Whatsapp\danny`:
   `[dd/mm/aaaa, hh:mm:ss] Nombre: texto`; borrados = "This message was
   deleted." / "You deleted this message."; media = "‎audio omitted" etc. y
   el archivo al lado con la fecha en el nombre
   (`00000237-AUDIO-2018-09-20-17-22-00.opus`): se empareja por fecha+tipo
   (segundo) y orden. un chat de 412k mensajes, 24.760 archivos, 4,1 GB; el
   celu exporto menos media de la que el texto nombra (lo borrado del
   telefono queda solo como linea). Importar solo lo anterior a lo que ya
   tenga el servidor para no duplicar.
5. Unos dias en paralelo; despues apagar Synapse y el bridge en la VM 104.

### Notas sueltas

- Lo que se pierde con esto: Cinny y cualquier cliente Matrix.
- Riesgo de ban: igual que hoy (misma libreria, misma sesion companera); lo
  que lo sube es re-vincular seguido o mandar en rafaga.
- Media grande: WhatsApp permite hasta 2 GB por archivo; sin limite de
  nuestro lado (fue el `max_upload_size: 50M` de Synapse el que rebotaba
  los archivos, ya subido a 2G en la VM 104).
