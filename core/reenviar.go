package main

import (
	"errors"
	"net/http"
	"os"

	"go.mau.fi/whatsmeow"
	"go.mau.fi/whatsmeow/proto/waE2E"
	"google.golang.org/protobuf/proto"
)

// POST /reenviar {chat, id, destino}: manda a `destino` una copia del
// mensaje. Para media NO se reusan las claves/URL viejas: el CDN de WhatsApp
// las vence a las semanas y al destinatario le sale "el video ya no esta
// disponible". Se vuelve a subir el archivo (la copia local, o se baja
// primero, pidiendoselo al telefono si hace falta) y se mandan claves nuevas.
func hReenviar(w http.ResponseWriter, r *http.Request) {
	var p struct {
		Chat    string `json:"chat"`
		ID      string `json:"id"`
		Destino string `json:"destino"`
	}
	if err := leerJSON(r, &p); err != nil {
		fallar(w, 400, err)
		return
	}
	j, ok := jidDe(p.Destino)
	m := mensajePorID(p.Chat, p.ID)
	if !ok || m == nil || m.Borrado {
		fallar(w, 404, errors.New("message not found"))
		return
	}
	ctxInfo := &waE2E.ContextInfo{IsForwarded: proto.Bool(true), ForwardingScore: proto.Uint32(1)}
	var msg *waE2E.Message
	c := contenido{tipo: m.Tipo, texto: m.Texto}
	if m.Media != nil {
		var mimeT, tipo string
		var crudo []byte
		if err := db.QueryRow("SELECT mime, proto FROM media WHERE id = ?", m.Media.ID).Scan(&mimeT, &crudo); err != nil || len(crudo) == 0 {
			fallar(w, 404, errors.New("media not found"))
			return
		}
		tipo = m.Tipo
		dm, err := mensajeDescargable(mimeT, tipo, crudo)
		if err != nil {
			fallar(w, 500, err)
			return
		}
		datos, err := bytesDeMedia(m.Media.ID)
		if err != nil {
			fallar(w, 410, errors.New("media no longer available; open it first so the phone re-uploads it"))
			return
		}
		var clase whatsmeow.MediaType
		switch tipo {
		case "imagen", "figurita":
			clase = whatsmeow.MediaImage
		case "video", "gif":
			clase = whatsmeow.MediaVideo
		case "audio", "nota":
			clase = whatsmeow.MediaAudio
		default:
			clase = whatsmeow.MediaDocument
		}
		subida, err := cli.Upload(ctx, datos, clase)
		if err != nil {
			fallar(w, http.StatusBadGateway, err)
			return
		}
		refrescarSubida(dm, subida)
		c.media = &Media{Mime: m.Media.Mime, Nombre: m.Media.Nombre, Bytes: m.Media.Bytes, Ancho: m.Media.Ancho, Alto: m.Media.Alto, Segundos: m.Media.Segundos}
		switch x := dm.(type) {
		case *waE2E.ImageMessage:
			x.ContextInfo = ctxInfo
			c.proto, c.mini = x, x.GetJPEGThumbnail()
			msg = &waE2E.Message{ImageMessage: x}
		case *waE2E.VideoMessage:
			x.ContextInfo = ctxInfo
			c.proto, c.mini = x, x.GetJPEGThumbnail()
			msg = &waE2E.Message{VideoMessage: x}
		case *waE2E.AudioMessage:
			x.ContextInfo = ctxInfo
			c.proto = x
			msg = &waE2E.Message{AudioMessage: x}
		case *waE2E.DocumentMessage:
			x.ContextInfo = ctxInfo
			c.proto, c.mini = x, x.GetJPEGThumbnail()
			msg = &waE2E.Message{DocumentMessage: x}
		case *waE2E.StickerMessage:
			x.ContextInfo = ctxInfo
			c.proto = x
			msg = &waE2E.Message{StickerMessage: x}
		}
	} else {
		if m.Texto == "" {
			fallar(w, 400, errors.New("nothing to forward"))
			return
		}
		msg = &waE2E.Message{ExtendedTextMessage: &waE2E.ExtendedTextMessage{Text: proto.String(m.Texto), ContextInfo: ctxInfo}}
	}
	resp, err := cli.SendMessage(ctx, j, msg)
	if err != nil {
		fallar(w, http.StatusBadGateway, err)
		return
	}
	nuevo := anotarEnviado(p.Destino, resp, c, nil, "")
	if nuevo.Media != nil && m.Media != nil {
		// La copia local ya la tenemos si el original estaba bajado.
		var ruta string
		var estado int
		db.QueryRow("SELECT ruta, estado FROM media WHERE id = ?", m.Media.ID).Scan(&ruta, &estado)
		db.Exec("UPDATE media SET ruta = ?, estado = ? WHERE id = ?", ruta, estado, nuevo.Media.ID)
	}
	db.Exec("UPDATE mensajes SET reenviado = 1 WHERE chat = ? AND id_wa = ?", p.Destino, nuevo.ID)
	responder(w, nuevo)
}

// Los bytes del adjunto: la copia local si esta, si no se baja (y si el CDN
// ya no lo tiene, se le pide al telefono; es un pedido del usuario).
func bytesDeMedia(id int64) ([]byte, error) {
	var ruta string
	var estado int
	db.QueryRow("SELECT ruta, estado FROM media WHERE id = ?", id).Scan(&ruta, &estado)
	if estado != 1 || ruta == "" {
		var err error
		if ruta, err = bajar(id, true); err != nil {
			return nil, err
		}
	}
	return os.ReadFile(ruta)
}

// Pone en el mensaje las claves y la URL de una subida nueva; el resto
// (miniatura, medidas, duracion, onda, nombre) queda como estaba.
func refrescarSubida(dm whatsmeow.DownloadableMessage, s whatsmeow.UploadResponse) {
	url, dp, largo := proto.String(s.URL), proto.String(s.DirectPath), proto.Uint64(s.FileLength)
	switch x := dm.(type) {
	case *waE2E.ImageMessage:
		x.URL, x.DirectPath, x.MediaKey, x.FileEncSHA256, x.FileSHA256, x.FileLength = url, dp, s.MediaKey, s.FileEncSHA256, s.FileSHA256, largo
	case *waE2E.VideoMessage:
		x.URL, x.DirectPath, x.MediaKey, x.FileEncSHA256, x.FileSHA256, x.FileLength = url, dp, s.MediaKey, s.FileEncSHA256, s.FileSHA256, largo
	case *waE2E.AudioMessage:
		x.URL, x.DirectPath, x.MediaKey, x.FileEncSHA256, x.FileSHA256, x.FileLength = url, dp, s.MediaKey, s.FileEncSHA256, s.FileSHA256, largo
	case *waE2E.DocumentMessage:
		x.URL, x.DirectPath, x.MediaKey, x.FileEncSHA256, x.FileSHA256, x.FileLength = url, dp, s.MediaKey, s.FileEncSHA256, s.FileSHA256, largo
	case *waE2E.StickerMessage:
		x.URL, x.DirectPath, x.MediaKey, x.FileEncSHA256, x.FileSHA256, x.FileLength = url, dp, s.MediaKey, s.FileEncSHA256, s.FileSHA256, largo
	}
}
