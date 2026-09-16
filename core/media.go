package main

import (
	"errors"
	"fmt"
	"io"
	"log"
	"mime"
	"net/http"
	"os"
	"path/filepath"
	"strings"
	"sync"
	"time"

	"go.mau.fi/whatsmeow"
	"go.mau.fi/whatsmeow/proto/waE2E"
	"go.mau.fi/whatsmeow/proto/waMmsRetry"
	"go.mau.fi/whatsmeow/types"
	"go.mau.fi/whatsmeow/types/events"
	"google.golang.org/protobuf/proto"
)

// Cola de bajadas: los adjuntos nuevos entran aca y se bajan enseguida; los
// de la historia los va tomando el alimentador. Bajan varios a la vez: es
// un GET al CDN de WhatsApp, no pasa por la sesion.
var pedidos = make(chan int64, 10000)

const bajadoresParalelos = 1

// Cuando el CDN contesta 403 nos esta frenando por ritmo: se para la bajada
// de fondo un rato, y cada vez que se repite el rato se dobla.
var (
	pausaMu    sync.Mutex
	pausaHasta time.Time
	pausaLargo = 15 * time.Minute
)

func frenar() {
	pausaMu.Lock()
	defer pausaMu.Unlock()
	if time.Now().Before(pausaHasta) {
		return
	}
	pausaHasta = time.Now().Add(pausaLargo)
	log.Printf("el CDN devolvio 403: bajadas de fondo paradas %v", pausaLargo)
	if pausaLargo < 4*time.Hour {
		pausaLargo *= 2
	}
}

func frenado() bool {
	pausaMu.Lock()
	defer pausaMu.Unlock()
	return time.Now().Before(pausaHasta)
}

func pedirBajada(id int64) {
	select {
	case pedidos <- id:
	default:
	}
}

func bajadorPendientes() {
	esperarConexion(time.Hour)
	for i := 0; i < bajadoresParalelos; i++ {
		go func() {
			for id := range pedidos {
				for frenado() {
					time.Sleep(10 * time.Second)
				}
				bajar(id, false)
				time.Sleep(time.Second)
			}
		}()
	}
	// El alimentador: cuando la cola se vacia, mete otra tanda de la
	// historia, lo mas nuevo primero, que es lo que mas se mira.
	for {
		if !cfg.BajarHistoria || !conectado.Load() || len(pedidos) > 0 || frenado() {
			time.Sleep(2 * time.Second)
			continue
		}
		filas, err := db.Query(`SELECT md.id FROM media md JOIN mensajes m ON m.chat = md.chat AND m.id_wa = md.mensaje
			WHERE md.estado = 0 AND md.intentos < 3 ORDER BY m.ts DESC LIMIT 200`)
		if err != nil {
			time.Sleep(10 * time.Second)
			continue
		}
		var tanda []int64
		for filas.Next() {
			var id int64
			if filas.Scan(&id) == nil {
				tanda = append(tanda, id)
			}
		}
		filas.Close()
		if len(tanda) == 0 {
			time.Sleep(30 * time.Second)
			continue
		}
		bajandoMu.Lock()
		enVuelo := len(bajando)
		bajandoMu.Unlock()
		if enVuelo > 0 && len(tanda) <= enVuelo {
			// Solo quedan los que ya estan en curso.
			time.Sleep(2 * time.Second)
			continue
		}
		for _, id := range tanda {
			pedidos <- id
		}
		// Que la tanda se termine antes de volver a preguntar, asi los
		// intentos fallidos no se repiten en loop.
		for len(pedidos) > 0 {
			time.Sleep(time.Second)
		}
		time.Sleep(3 * time.Second)
	}
}

// Reconstruye el sub-mensaje descargable guardado con el media.
func mensajeDescargable(mime string, tipo string, crudo []byte) (whatsmeow.DownloadableMessage, error) {
	var m whatsmeow.DownloadableMessage
	switch tipo {
	case "imagen":
		m = &waE2E.ImageMessage{}
	case "video", "gif":
		m = &waE2E.VideoMessage{}
	case "audio", "nota":
		m = &waE2E.AudioMessage{}
	case "documento":
		m = &waE2E.DocumentMessage{}
	case "figurita":
		m = &waE2E.StickerMessage{}
	default:
		return nil, fmt.Errorf("tipo %q sin media", tipo)
	}
	if err := proto.Unmarshal(crudo, m.(proto.Message)); err != nil {
		return nil, err
	}
	return m, nil
}

var bajandoMu sync.Mutex
var bajando = map[int64]bool{}

// El adjunto ya no esta en el CDN de WhatsApp (solo lo tiene el telefono).
var errVencido = errors.New("media no longer available on WhatsApp")

// Baja un adjunto a disco. Devuelve la ruta, o error. Si WhatsApp ya no lo
// tiene (404/410) y `pedirAlTelefono`, le pide al telefono que lo vuelva a
// subir; eso pasa por la sesion, asi que solo se hace cuando el usuario
// abre ese adjunto, no para la historia entera.
func bajar(id int64, pedirAlTelefono bool) (string, error) {
	bajandoMu.Lock()
	if bajando[id] {
		bajandoMu.Unlock()
		return "", errors.New("ya se esta bajando")
	}
	bajando[id] = true
	bajandoMu.Unlock()
	defer func() {
		bajandoMu.Lock()
		delete(bajando, id)
		bajandoMu.Unlock()
	}()

	var chat, msgID, mimeT, nombre, ruta, tipo string
	var estado int
	var propio bool
	var crudo []byte
	err := db.QueryRow(`SELECT md.chat, md.mensaje, md.mime, md.nombre, md.ruta, md.estado, md.proto, m.tipo, m.propio
		FROM media md JOIN mensajes m ON m.chat = md.chat AND m.id_wa = md.mensaje WHERE md.id = ?`, id).
		Scan(&chat, &msgID, &mimeT, &nombre, &ruta, &estado, &crudo, &tipo, &propio)
	if err != nil {
		return "", err
	}
	if estado == 1 && ruta != "" {
		return ruta, nil
	}
	if len(crudo) == 0 {
		return "", errors.New("sin datos para bajar")
	}
	dm, err := mensajeDescargable(mimeT, tipo, crudo)
	if err != nil {
		return "", err
	}
	datos, err := cli.Download(ctx, dm)
	if err != nil {
		// Los links de media vencen (llevan fecha): el CDN contesta 403 para lo
		// viejo, 404/410 para lo borrado. En los tres casos solo el telefono
		// lo tiene; a demanda se le pide que lo resuba.
		var http whatsmeow.DownloadHTTPError
		vencido := errors.Is(err, whatsmeow.ErrMediaDownloadFailedWith404) ||
			errors.Is(err, whatsmeow.ErrMediaDownloadFailedWith410) ||
			(errors.As(err, &http) && http.StatusCode == 403)
		db.Exec("UPDATE media SET intentos = intentos + 1 WHERE id = ?", id)
		if vencido {
			db.Exec("UPDATE media SET estado = 2 WHERE id = ?", id)
			if pedirAlTelefono {
				log.Printf("media %d vencido en WhatsApp, pido reenvio al telefono", id)
				pedirReenvio(chat, msgID, propio, dm.GetMediaKey())
			}
			evento("media", chat, map[string]any{"id": id, "mensaje": msgID, "estado": 2})
			return "", errVencido
		}
		log.Printf("media %d: %v", id, err)
		return "", err
	}
	ruta = rutaPara(id, mimeT, nombre)
	if err := os.MkdirAll(filepath.Dir(ruta), 0o755); err != nil {
		return "", err
	}
	if err := os.WriteFile(ruta, datos, 0o644); err != nil {
		return "", err
	}
	db.Exec("UPDATE media SET ruta = ?, estado = 1, bytes = ? WHERE id = ?", ruta, len(datos), id)
	pausaMu.Lock()
	pausaLargo = 15 * time.Minute
	pausaMu.Unlock()
	// Un estado "foto con musica" es una foto: queda como imagen (avisa solo).
	if tipo == "video" && convertirFotoConMusica(id, chat, msgID, ruta) {
		return strings.TrimSuffix(ruta, ".mp4") + ".jpg", nil
	}
	asegurarMiniatura(id, chat, msgID, tipo, ruta)
	evento("media", chat, map[string]any{"id": id, "mensaje": msgID, "estado": 1})
	return ruta, nil
}

var extensiones = map[string]string{
	"image/jpeg": ".jpg", "image/png": ".png", "image/webp": ".webp", "image/gif": ".gif",
	"video/mp4": ".mp4", "video/3gpp": ".3gp", "audio/ogg": ".ogg", "audio/mpeg": ".mp3",
	"audio/mp4": ".m4a", "audio/aac": ".aac", "audio/amr": ".amr", "audio/wav": ".wav",
	"application/pdf": ".pdf", "text/plain": ".txt",
}

func rutaPara(id int64, mimeT, nombre string) string {
	ext := filepath.Ext(nombre)
	if ext == "" || len(ext) > 6 {
		base := strings.TrimSpace(strings.Split(mimeT, ";")[0])
		ext = extensiones[base]
		if ext == "" {
			if lista, _ := mime.ExtensionsByType(base); len(lista) > 0 {
				ext = lista[0]
			}
		}
	}
	return filepath.Join(cfg.Media, fmt.Sprintf("%03d", id/1000), fmt.Sprintf("%d%s", id, ext))
}

// Le pide al telefono que vuelva a subir un adjunto vencido. La respuesta
// llega como events.MediaRetry.
func pedirReenvio(chat, msgID string, propio bool, clave []byte) {
	j, ok := jidDe(chat)
	if !ok || len(clave) == 0 {
		return
	}
	var remitente string
	db.QueryRow("SELECT remitente FROM mensajes WHERE chat = ? AND id_wa = ?", chat, msgID).Scan(&remitente)
	s, _ := jidDe(remitente)
	info := &types.MessageInfo{
		MessageSource: types.MessageSource{Chat: j, Sender: s, IsFromMe: propio, IsGroup: j.Server == types.GroupServer},
		ID:            msgID,
	}
	if err := cli.SendMediaRetryReceipt(ctx, info, clave); err != nil {
		log.Printf("reenvio: %v", err)
	}
}

func reintentoMedia(evt *events.MediaRetry) {
	chat := normalizar(evt.ChatID)
	var id int64
	var mimeT, tipo string
	var crudo []byte
	err := db.QueryRow(`SELECT md.id, md.mime, md.proto, m.tipo FROM media md JOIN mensajes m ON m.chat = md.chat AND m.id_wa = md.mensaje
		WHERE md.chat = ? AND md.mensaje = ?`, chat, evt.MessageID).Scan(&id, &mimeT, &crudo, &tipo)
	if err != nil {
		return
	}
	dm, err := mensajeDescargable(mimeT, tipo, crudo)
	if err != nil {
		return
	}
	if evt.Error != nil {
		log.Printf("reenvio de %d: error %d", id, evt.Error.Code)
		return
	}
	aviso, err := whatsmeow.DecryptMediaRetryNotification(evt, dm.GetMediaKey())
	if err != nil {
		log.Printf("reenvio de %d: %v", id, err)
		return
	}
	if aviso.GetResult() != waMmsRetry.MediaRetryNotification_SUCCESS {
		log.Printf("reenvio de %d: el telefono dijo %s", id, aviso.GetResult())
		return
	}
	// Con la ruta nueva, el sub-mensaje vuelve a ser descargable.
	switch m := dm.(type) {
	case *waE2E.ImageMessage:
		m.DirectPath, m.URL = proto.String(aviso.GetDirectPath()), nil
	case *waE2E.VideoMessage:
		m.DirectPath, m.URL = proto.String(aviso.GetDirectPath()), nil
	case *waE2E.AudioMessage:
		m.DirectPath, m.URL = proto.String(aviso.GetDirectPath()), nil
	case *waE2E.DocumentMessage:
		m.DirectPath, m.URL = proto.String(aviso.GetDirectPath()), nil
	case *waE2E.StickerMessage:
		m.DirectPath, m.URL = proto.String(aviso.GetDirectPath()), nil
	}
	nuevo, _ := proto.Marshal(dm.(proto.Message))
	db.Exec("UPDATE media SET proto = ?, estado = 0, intentos = 0 WHERE id = ?", nuevo, id)
	log.Printf("reenvio de %d: el telefono lo resubio, bajando", id)
	if _, err := bajar(id, false); err != nil {
		log.Printf("reenvio de %d: bajada fallo: %v", id, err)
	}
}

// ---- fotos de perfil ------------------------------------------------------

var fotosPedidas = map[string]time.Time{}

func pedirFoto(jid string) {
	fotosPedidas[jid] = time.Time{}
}

// Va buscando, de a una y despacio, las fotos de los chats y contactos que
// no tienen. Las que WhatsApp niega se reintentan recien al dia siguiente.
func buscadorFotos() {
	esperarConexion(time.Hour)
	time.Sleep(20 * time.Second)
	for {
		if !conectado.Load() {
			time.Sleep(10 * time.Second)
			continue
		}
		filas, err := db.Query(`SELECT c.jid FROM chats c LEFT JOIN contactos k ON k.jid = c.jid
			WHERE c.foto = '' AND COALESCE(k.foto, '') = '' ORDER BY c.ultimo_ts DESC LIMIT 200`)
		if err != nil {
			time.Sleep(time.Minute)
			continue
		}
		var candidatos []string
		for filas.Next() {
			var j string
			if filas.Scan(&j) == nil {
				if cuando, ya := fotosPedidas[j]; !ya || time.Since(cuando) > 24*time.Hour {
					candidatos = append(candidatos, j)
				}
			}
		}
		filas.Close()
		if len(candidatos) == 0 {
			time.Sleep(time.Minute)
			continue
		}
		for _, j := range candidatos {
			fotosPedidas[j] = time.Now()
			bajarFoto(j)
			time.Sleep(1500 * time.Millisecond)
		}
	}
}

func bajarFoto(jidTexto string) {
	j, ok := jidDe(jidTexto)
	if !ok || j.Server == types.BroadcastServer || j.User == "0" {
		return
	}
	info, err := cli.GetProfilePictureInfo(ctx, j, &whatsmeow.GetProfilePictureParams{Preview: true})
	if err != nil || info == nil || info.URL == "" {
		if err != nil && !errors.Is(err, whatsmeow.ErrProfilePictureNotSet) && !errors.Is(err, whatsmeow.ErrProfilePictureUnauthorized) {
			log.Printf("foto de %s: %v", jidTexto, err)
		}
		return
	}
	resp, err := http.Get(info.URL)
	if err != nil {
		return
	}
	defer resp.Body.Close()
	datos, err := io.ReadAll(resp.Body)
	if err != nil || resp.StatusCode != 200 {
		return
	}
	ruta := filepath.Join(cfg.Media, "fotos", j.User+".jpg")
	os.MkdirAll(filepath.Dir(ruta), 0o755)
	if os.WriteFile(ruta, datos, 0o644) != nil {
		return
	}
	if j.Server == types.GroupServer {
		db.Exec("UPDATE chats SET foto = ? WHERE jid = ?", ruta, jidTexto)
		evento("chat", jidTexto, chatDe(jidTexto))
	} else {
		db.Exec("UPDATE contactos SET foto = ? WHERE jid = ?", ruta, jidTexto)
		if c := contactoDe(jidTexto); c != nil {
			evento("contacto", "", c)
		}
		if c := chatDe(jidTexto); c != nil {
			evento("chat", jidTexto, c)
		}
	}
}
