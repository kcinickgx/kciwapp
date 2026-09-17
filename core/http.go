package main

import (
	"fmt"
	"os/exec"
	"encoding/json"
	"errors"
	"io"
	"log"
	"net/http"
	"os"
	"path/filepath"
	"strconv"
	"strings"
	"time"

	"github.com/skip2/go-qrcode"
	"go.mau.fi/whatsmeow"
	"go.mau.fi/whatsmeow/proto/waE2E"
	"go.mau.fi/whatsmeow/types"
	"google.golang.org/protobuf/proto"
)

func servirHTTP() {
	mux := http.NewServeMux()
	// Sin token: para poder vincular desde un navegador.
	mux.HandleFunc("GET /estado", hEstado)
	// Con token.
	mux.HandleFunc("GET /qr", conToken(hQR))
	mux.HandleFunc("POST /vincular", conToken(hVincular))
	mux.HandleFunc("GET /chats", conToken(hChats))
	mux.HandleFunc("GET /contactos", conToken(hContactos))
	mux.HandleFunc("GET /miembros", conToken(hMiembros))
	mux.HandleFunc("GET /mensajes", conToken(hMensajes))
	mux.HandleFunc("GET /buscar", conToken(hBuscar))
	mux.HandleFunc("GET /cantidad", conToken(hCantidad))
	mux.HandleFunc("POST /transcripcion", conToken(hTranscripcion))
	mux.HandleFunc("POST /reenviar", conToken(hReenviar))
	mux.HandleFunc("POST /contacto", conToken(hContacto))
	mux.HandleFunc("GET /eventos", conToken(hEventos))
	mux.HandleFunc("GET /media/{id}", conToken(hMedia))
	mux.HandleFunc("GET /miniatura/{id}", conToken(hMiniatura))
	mux.HandleFunc("GET /foto/{jid}", conToken(hFoto))
	mux.HandleFunc("POST /enviar", conToken(hEnviar))
	mux.HandleFunc("POST /reaccion", conToken(hReaccion))
	mux.HandleFunc("POST /editar", conToken(hEditar))
	mux.HandleFunc("POST /borrar", conToken(hBorrar))
	mux.HandleFunc("POST /leido", conToken(hLeido))
	mux.HandleFunc("POST /escribiendo", conToken(hEscribiendo))
	mux.HandleFunc("POST /presencia", conToken(hPresencia))
	mux.HandleFunc("POST /boton", conToken(hBoton))
	mux.HandleFunc("POST /llamada/rechazar", conToken(hRechazarLlamada))
	srv := &http.Server{Addr: cfg.Escucha, Handler: mux, ReadHeaderTimeout: 10 * time.Second}
	log.Printf("http en %s", cfg.Escucha)
	if err := srv.ListenAndServe(); err != nil {
		log.Fatalf("http: %v", err)
	}
}

func conToken(h http.HandlerFunc) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		t := r.Header.Get("X-Token")
		if t == "" {
			t = r.URL.Query().Get("token")
		}
		if t != cfg.Token {
			http.Error(w, "token", http.StatusUnauthorized)
			return
		}
		h(w, r)
	}
}

func responder(w http.ResponseWriter, v any) {
	w.Header().Set("Content-Type", "application/json; charset=utf-8")
	json.NewEncoder(w).Encode(v)
}

func fallar(w http.ResponseWriter, codigo int, err error) {
	w.Header().Set("Content-Type", "application/json; charset=utf-8")
	w.WriteHeader(codigo)
	json.NewEncoder(w).Encode(map[string]string{"error": err.Error()})
}

func leerJSON(r *http.Request, v any) error {
	return json.NewDecoder(io.LimitReader(r.Body, 1<<20)).Decode(v)
}

// ---- estado y QR ----------------------------------------------------------

func hEstado(w http.ResponseWriter, r *http.Request) {
	est := map[string]any{
		"conectado": conectado.Load(),
		"logueado":  cli.Store.ID != nil,
		"qr":        qrActual.Load().(string) != "",
		"vinculando": vinculando.Load(),
	}
	if cli.Store.ID != nil {
		est["jid"] = cli.Store.ID.ToNonAD().String()
		est["telefono"] = cli.Store.ID.User
		est["nombre"] = cli.Store.PushName
	}
	var seq int64
	db.QueryRow("SELECT COALESCE(MAX(seq), 0) FROM eventos").Scan(&seq)
	est["seq"] = seq
	// Cambia con cada importacion masiva: el cliente tira su cache y recarga.
	est["importacion"] = valor("importacion")
	responder(w, est)
}

// POST /vincular: arranca el ciclo del QR (si no hay sesion).
func hVincular(w http.ResponseWriter, r *http.Request) {
	if cli.Store.ID != nil {
		responder(w, map[string]any{"logueado": true})
		return
	}
	empezarVinculacion()
	responder(w, map[string]any{"logueado": false, "vinculando": true})
}

// GET /qr: el QR actual como PNG (404 si todavia no hay o ya esta vinculado).
func hQR(w http.ResponseWriter, r *http.Request) {
	codigo := qrActual.Load().(string)
	if codigo == "" {
		if cli.Store.ID != nil {
			fallar(w, 404, errors.New("already linked"))
		} else {
			fallar(w, 404, errors.New("no qr yet"))
		}
		return
	}
	png, err := qrcode.Encode(codigo, qrcode.Medium, 512)
	if err != nil {
		fallar(w, 500, err)
		return
	}
	w.Header().Set("Content-Type", "image/png")
	w.Header().Set("Cache-Control", "no-store")
	w.Write(png)
}

// GET /cantidad?chat=: cuantos mensajes hay del chat (para la barra de "cargar todo").
func hCantidad(w http.ResponseWriter, r *http.Request) {
	var n int64
	db.QueryRow("SELECT COUNT(*) FROM mensajes WHERE chat = ?", r.URL.Query().Get("chat")).Scan(&n)
	responder(w, map[string]any{"cantidad": n})
}

// ---- lecturas -------------------------------------------------------------

func hChats(w http.ResponseWriter, r *http.Request) {
	filas, err := db.Query(`SELECT c.jid, c.es_grupo, c.foto, c.ultimo_ts, c.no_leidos, c.archivado,
		IF(c.es_grupo, c.nombre, COALESCE(NULLIF(k.nombre_agenda, ''), NULLIF(k.nombre_push, ''), c.nombre, '')), COALESCE(k.foto, '')
		FROM chats c LEFT JOIN contactos k ON k.jid = c.jid ORDER BY c.ultimo_ts DESC`)
	if err != nil {
		fallar(w, 500, err)
		return
	}
	defer filas.Close()
	lista := []*Chat{}
	for filas.Next() {
		var c Chat
		var fotoContacto string
		if err := filas.Scan(&c.JID, &c.EsGrupo, &c.Foto, &c.UltimoTS, &c.NoLeidos, &c.Archivado, &c.Nombre, &fotoContacto); err != nil {
			continue
		}
		if c.Foto == "" {
			c.Foto = fotoContacto
		}
		lista = append(lista, &c)
	}
	if r.URL.Query().Get("ultimo") != "" {
		for _, c := range lista {
			f := db.QueryRow("SELECT "+columnasMensaje+" "+desdeMensajes+" WHERE m.chat = ? ORDER BY m.ts DESC LIMIT 1", c.JID)
			if m, err := leerMensaje(f); err == nil {
				c.Ultimo = m
			}
		}
	}
	responder(w, lista)
}

func hContactos(w http.ResponseWriter, r *http.Request) {
	filas, err := db.Query("SELECT jid, lid, telefono, nombre_push, nombre_agenda, foto FROM contactos")
	if err != nil {
		fallar(w, 500, err)
		return
	}
	defer filas.Close()
	lista := []Contacto{}
	for filas.Next() {
		var c Contacto
		if filas.Scan(&c.JID, &c.LID, &c.Telefono, &c.NombrePush, &c.NombreAgenda, &c.Foto) == nil {
			lista = append(lista, c)
		}
	}
	responder(w, lista)
}

func hMiembros(w http.ResponseWriter, r *http.Request) {
	chat := r.URL.Query().Get("chat")
	filas, err := db.Query("SELECT jid, admin FROM miembros WHERE chat = ?", chat)
	if err != nil {
		fallar(w, 500, err)
		return
	}
	defer filas.Close()
	type miembro struct {
		JID   string `json:"jid"`
		Admin bool   `json:"admin"`
	}
	lista := []miembro{}
	for filas.Next() {
		var m miembro
		if filas.Scan(&m.JID, &m.Admin) == nil {
			lista = append(lista, m)
		}
	}
	responder(w, lista)
}

// GET /mensajes?chat=&antes=<ts>&limite=50  (los ultimos `limite` anteriores
// a `antes`, en orden cronologico). Con `desde=<ts>` trae hacia adelante.
func hMensajes(w http.ResponseWriter, r *http.Request) {
	q := r.URL.Query()
	chat := q.Get("chat")
	limite, _ := strconv.Atoi(q.Get("limite"))
	if limite <= 0 || limite > 1000000 {
		limite = 50
	}
	var filas interface {
		Next() bool
		Scan(...any) error
		Close() error
	}
	var err error
	if desde := q.Get("desde"); desde != "" {
		filas, err = db.Query("SELECT "+columnasMensaje+" "+desdeMensajes+" WHERE m.chat = ? AND m.ts >= ? ORDER BY m.ts, m.id_wa LIMIT ?", chat, desde, limite)
	} else {
		antes, _ := strconv.ParseInt(q.Get("antes"), 10, 64)
		if antes <= 0 {
			antes = 1 << 62
		}
		filas, err = db.Query("SELECT "+columnasMensaje+" "+desdeMensajes+" WHERE m.chat = ? AND m.ts < ? ORDER BY m.ts DESC, m.id_wa DESC LIMIT ?", chat, antes, limite)
	}
	if err != nil {
		fallar(w, 500, err)
		return
	}
	lista := []*Mensaje{}
	ids := []string{}
	for filas.Next() {
		if m, err := leerMensaje(filas); err == nil {
			lista = append(lista, m)
			ids = append(ids, m.ID)
		}
	}
	filas.Close()
	if q.Get("desde") == "" {
		for i, j := 0, len(lista)-1; i < j; i, j = i+1, j-1 {
			lista[i], lista[j] = lista[j], lista[i]
		}
	}
	reacs := reaccionesDe(chat, ids)
	bots := botonesDe(chat, ids)
	for _, m := range lista {
		m.Reacciones = reacs[m.ID]
		m.Botones = bots[m.ID]
	}
	responder(w, lista)
}

// GET /eventos?desde=<seq>  long-poll de hasta 25 s. Si el cursor quedo
// mas atras de lo que guardamos, avisa `resync` y el cliente baja todo.
func hEventos(w http.ResponseWriter, r *http.Request) {
	desde, _ := strconv.ParseInt(r.URL.Query().Get("desde"), 10, 64)
	type ev struct {
		Seq   int64           `json:"seq"`
		TS    int64           `json:"ts"`
		Tipo  string          `json:"tipo"`
		Chat  string          `json:"chat,omitempty"`
		Datos json.RawMessage `json:"datos"`
	}
	var minimo int64
	db.QueryRow("SELECT COALESCE(MIN(seq), 0) FROM eventos").Scan(&minimo)
	if desde > 0 && minimo > desde+1 {
		responder(w, map[string]any{"resync": true})
		return
	}
	tope := time.Now().Add(25 * time.Second)
	for {
		espera := esperarEvento()
		filas, err := db.Query("SELECT seq, ts, tipo, chat, datos FROM eventos WHERE seq > ? ORDER BY seq LIMIT 500", desde)
		if err != nil {
			fallar(w, 500, err)
			return
		}
		lista := []ev{}
		for filas.Next() {
			var e ev
			var datos string
			if filas.Scan(&e.Seq, &e.TS, &e.Tipo, &e.Chat, &datos) == nil {
				e.Datos = json.RawMessage(datos)
				lista = append(lista, e)
			}
		}
		filas.Close()
		if len(lista) > 0 || time.Now().After(tope) {
			responder(w, map[string]any{"eventos": lista})
			return
		}
		select {
		case <-espera:
		case <-time.After(time.Until(tope)):
		case <-r.Context().Done():
			return
		}
	}
}

func hMedia(w http.ResponseWriter, r *http.Request) {
	id, _ := strconv.ParseInt(r.PathValue("id"), 10, 64)
	var ruta, mimeT, nombre string
	var estado int
	if err := db.QueryRow("SELECT ruta, mime, nombre, estado FROM media WHERE id = ?", id).Scan(&ruta, &mimeT, &nombre, &estado); err != nil {
		http.NotFound(w, r)
		return
	}
	if estado != 1 || ruta == "" {
		var err error
		// Solo con ?pedir=1 (el usuario abrio el adjunto) se le pide al
		// telefono que resuba lo vencido; las burbujas no.
		ruta, err = bajar(id, r.URL.Query().Get("pedir") == "1")
		if err != nil {
			if errors.Is(err, errVencido) || estado == 2 {
				// 410: el cliente sabe que se le pidio al telefono y espera el evento.
				fallar(w, http.StatusGone, errVencido)
			} else {
				fallar(w, http.StatusBadGateway, err)
			}
			return
		}
	}
	if r.URL.Query().Get("wav") == "1" {
		// Para transcribir: WAV 16 kHz mono, generado una vez al lado del original.
		wav := ruta + ".16k.wav"
		if _, err := os.Stat(wav); err != nil {
			cmd := exec.Command("ffmpeg", "-y", "-loglevel", "error", "-i", ruta, "-ac", "1", "-ar", "16000", "-c:a", "pcm_s16le", wav)
			if out, err := cmd.CombinedOutput(); err != nil {
				fallar(w, 500, fmt.Errorf("ffmpeg: %v %s", err, out))
				return
			}
		}
		w.Header().Set("Content-Type", "audio/wav")
		http.ServeFile(w, r, wav)
		return
	}
	if mimeT != "" {
		w.Header().Set("Content-Type", mimeT)
	}
	if nombre != "" {
		w.Header().Set("Content-Disposition", "inline; filename=\""+strings.ReplaceAll(nombre, "\"", "")+"\"")
	}
	http.ServeFile(w, r, ruta)
}

// POST /transcripcion {chat, id, texto}: la transcripcion de una nota de voz
// queda como texto del mensaje (se ve debajo del audio y entra en la busqueda).
func hTranscripcion(w http.ResponseWriter, r *http.Request) {
	var p struct {
		Chat  string `json:"chat"`
		ID    string `json:"id"`
		Texto string `json:"texto"`
	}
	if err := leerJSON(r, &p); err != nil {
		fallar(w, 400, err)
		return
	}
	texto := strings.TrimSpace(p.Texto)
	if texto == "" {
		texto = "(no speech detected)"
	}
	db.Exec("UPDATE mensajes SET texto = ? WHERE chat = ? AND id_wa = ?", texto, p.Chat, p.ID)
	evento("transcripcion", p.Chat, map[string]any{"id": p.ID, "texto": texto})
	responder(w, map[string]any{"ok": true})
}

func hMiniatura(w http.ResponseWriter, r *http.Request) {
	id, _ := strconv.ParseInt(r.PathValue("id"), 10, 64)
	var mini []byte
	if err := db.QueryRow("SELECT miniatura FROM media WHERE id = ?", id).Scan(&mini); err != nil || len(mini) == 0 {
		http.NotFound(w, r)
		return
	}
	w.Header().Set("Content-Type", "image/jpeg")
	w.Write(mini)
}

func hFoto(w http.ResponseWriter, r *http.Request) {
	jid := r.PathValue("jid")
	var ruta string
	db.QueryRow("SELECT foto FROM contactos WHERE jid = ? AND foto <> ''", jid).Scan(&ruta)
	if ruta == "" {
		db.QueryRow("SELECT foto FROM chats WHERE jid = ? AND foto <> ''", jid).Scan(&ruta)
	}
	if ruta == "" {
		http.NotFound(w, r)
		return
	}
	http.ServeFile(w, r, ruta)
}

// ---- envios ---------------------------------------------------------------

// El contexto de una cita, para meter en el mensaje que se manda.
func contextoCita(chat, citaID string) *waE2E.ContextInfo {
	if citaID == "" {
		return nil
	}
	m := mensajePorID(chat, citaID)
	if m == nil {
		return &waE2E.ContextInfo{StanzaID: proto.String(citaID)}
	}
	participante := m.Remitente
	if m.Propio {
		participante = cli.Store.ID.ToNonAD().String()
	}
	texto := m.Texto
	if texto == "" {
		texto = nombreTipo(m.Tipo)
	}
	return &waE2E.ContextInfo{
		StanzaID:      proto.String(citaID),
		Participant:   proto.String(participante),
		QuotedMessage: &waE2E.Message{Conversation: proto.String(texto)},
	}
}

// Guarda lo que acabamos de mandar como mensaje propio y lo devuelve.
func anotarEnviado(chat string, resp whatsmeow.SendResponse, c contenido, ctxInfo *waE2E.ContextInfo, rutaLocal string) *Mensaje {
	m := &Mensaje{
		ID: resp.ID, Chat: chat, Remitente: cli.Store.ID.ToNonAD().String(), Propio: true,
		TS: resp.Timestamp.UnixMilli(), Tipo: c.tipo, Texto: c.texto, Estado: 1,
	}
	if ctxInfo != nil {
		m.CitaID = ctxInfo.GetStanzaID()
		m.CitaRemitente = ctxInfo.GetParticipant()
		m.CitaTexto = ctxInfo.GetQuotedMessage().GetConversation()
		// Lo citado es una foto, un audio...: el mismo texto que al recibir.
		if q := mensajePorID(chat, m.CitaID); q != nil {
			m.CitaTexto = q.Texto
			if m.CitaTexto == "" && q.Tipo != "" && q.Tipo != "texto" {
				m.CitaTexto = nombreTipo(q.Tipo)
			}
		}
	}
	if c.media != nil {
		var crudo []byte
		if c.proto != nil {
			crudo, _ = proto.Marshal(c.proto)
		}
		var mini any
		if len(c.mini) > 0 {
			mini = c.mini
		}
		var onda any
		if len(c.onda) > 0 {
			onda = c.onda
		}
		res, err := db.Exec(`INSERT INTO media (chat, mensaje, mime, nombre, bytes, ancho, alto, segundos, ruta, estado, miniatura, proto, onda)
			VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, 1, ?, ?, ?)`,
			chat, m.ID, c.media.Mime, c.media.Nombre, c.media.Bytes, c.media.Ancho, c.media.Alto, c.media.Segundos, rutaLocal, mini, crudo, onda)
		if err == nil {
			c.media.ID, _ = res.LastInsertId()
			c.media.Estado = 1
			c.media.Miniatura = len(c.mini) > 0
			for _, b := range c.onda {
				c.media.Onda = append(c.media.Onda, int(b))
			}
			m.Media = c.media
		}
	}
	insertarMensaje(m, false, "")
	return m
}

// POST /enviar: JSON {chat, texto, cita_id?} o multipart con `archivo`,
// `chat`, `texto` (epigrafe), `cita_id`, `tipo` (imagen/video/gif/audio/
// nota/documento/figurita; si falta se deduce del mime), `segundos`.
func hEnviar(w http.ResponseWriter, r *http.Request) {
	if !conectado.Load() {
		fallar(w, http.StatusServiceUnavailable, errors.New("not connected"))
		return
	}
	if strings.HasPrefix(r.Header.Get("Content-Type"), "multipart/") {
		enviarArchivo(w, r)
		return
	}
	var p struct {
		Chat   string `json:"chat"`
		Texto  string `json:"texto"`
		CitaID string `json:"cita_id"`
		// Solo para un estado (chat status@broadcast): color de fondo ARGB y letra.
		Fondo uint32 `json:"fondo"`
		Letra int32  `json:"letra"`
	}
	if err := leerJSON(r, &p); err != nil {
		fallar(w, 400, err)
		return
	}
	j, ok := jidDe(p.Chat)
	texto := strings.TrimSpace(p.Texto)
	if !ok || texto == "" {
		fallar(w, 400, errors.New("chat and texto required"))
		return
	}
	ctxInfo := contextoCita(p.Chat, p.CitaID)
	var msg *waE2E.Message
	if j == types.StatusBroadcastJID {
		// Un estado de texto: como lo manda el telefono, con fondo, letra y texto blanco.
		fondo := p.Fondo
		if fondo == 0 {
			fondo = 0xFF37474F
		}
		letra := waE2E.ExtendedTextMessage_FontType(p.Letra)
		msg = &waE2E.Message{ExtendedTextMessage: &waE2E.ExtendedTextMessage{
			Text: proto.String(texto), BackgroundArgb: proto.Uint32(fondo), TextArgb: proto.Uint32(0xFFFFFFFF), Font: &letra}}
	} else if ctxInfo == nil {
		msg = &waE2E.Message{Conversation: proto.String(texto)}
	} else {
		msg = &waE2E.Message{ExtendedTextMessage: &waE2E.ExtendedTextMessage{Text: proto.String(texto), ContextInfo: ctxInfo}}
	}
	resp, err := cli.SendMessage(ctx, j, msg)
	if err != nil {
		fallar(w, http.StatusBadGateway, err)
		return
	}
	responder(w, anotarEnviado(p.Chat, resp, contenido{tipo: "texto", texto: texto}, ctxInfo, ""))
}

func enviarArchivo(w http.ResponseWriter, r *http.Request) {
	if err := r.ParseMultipartForm(64 << 20); err != nil {
		fallar(w, 400, err)
		return
	}
	chat := r.FormValue("chat")
	j, ok := jidDe(chat)
	if !ok {
		fallar(w, 400, errors.New("chat required"))
		return
	}
	archivo, cabecera, err := r.FormFile("archivo")
	if err != nil {
		fallar(w, 400, err)
		return
	}
	defer archivo.Close()
	datos, err := io.ReadAll(archivo)
	if err != nil {
		fallar(w, 400, err)
		return
	}
	mimeT := r.FormValue("mime")
	if mimeT == "" {
		mimeT = cabecera.Header.Get("Content-Type")
	}
	if mimeT == "" || mimeT == "application/octet-stream" {
		mimeT = http.DetectContentType(datos)
	}
	tipo := r.FormValue("tipo")
	if tipo == "" {
		switch {
		case strings.HasPrefix(mimeT, "image/webp"):
			tipo = "figurita"
		case strings.HasPrefix(mimeT, "image/"):
			tipo = "imagen"
		case strings.HasPrefix(mimeT, "video/"):
			tipo = "video"
		case strings.HasPrefix(mimeT, "audio/ogg"):
			tipo = "nota"
		case strings.HasPrefix(mimeT, "audio/"):
			tipo = "audio"
		default:
			tipo = "documento"
		}
	}
	epigrafe := strings.TrimSpace(r.FormValue("texto"))
	segundos, _ := strconv.Atoi(r.FormValue("segundos"))
	nombre := cabecera.Filename
	if tipo == "figurita" && !strings.HasPrefix(mimeT, "image/webp") {
		convertido, err := aFigurita(datos)
		if err != nil {
			fallar(w, 500, err)
			return
		}
		datos = convertido
		mimeT = "image/webp"
		nombre = "sticker.webp"
	}
	if tipo == "nota" && !strings.HasPrefix(mimeT, "audio/ogg") {
		convertido, seg, err := aOggOpus(datos)
		if err != nil {
			fallar(w, 500, err)
			return
		}
		datos = convertido
		mimeT = "audio/ogg; codecs=opus"
		if seg > 0 {
			segundos = seg
		}
		nombre = "nota.ogg"
	}
	ctxInfo := contextoCita(chat, r.FormValue("cita_id"))

	var claseSubida whatsmeow.MediaType
	switch tipo {
	case "imagen", "figurita":
		claseSubida = whatsmeow.MediaImage
	case "video", "gif":
		claseSubida = whatsmeow.MediaVideo
	case "audio", "nota":
		claseSubida = whatsmeow.MediaAudio
	default:
		claseSubida = whatsmeow.MediaDocument
	}
	subida, err := cli.Upload(ctx, datos, claseSubida)
	if err != nil {
		fallar(w, http.StatusBadGateway, err)
		return
	}
	largo := proto.Uint64(subida.FileLength)
	c := contenido{tipo: tipo, texto: epigrafe, ctx: ctxInfo}
	c.media = &Media{Mime: mimeT, Bytes: int64(len(datos)), Segundos: segundos}
	var msg *waE2E.Message
	switch tipo {
	case "imagen":
		im := &waE2E.ImageMessage{
			URL: proto.String(subida.URL), DirectPath: proto.String(subida.DirectPath), MediaKey: subida.MediaKey,
			Mimetype: proto.String(mimeT), FileEncSHA256: subida.FileEncSHA256, FileSHA256: subida.FileSHA256, FileLength: largo,
			ContextInfo: ctxInfo,
		}
		if epigrafe != "" {
			im.Caption = proto.String(epigrafe)
		}
		c.proto = im
		msg = &waE2E.Message{ImageMessage: im}
	case "figurita":
		st := &waE2E.StickerMessage{
			URL: proto.String(subida.URL), DirectPath: proto.String(subida.DirectPath), MediaKey: subida.MediaKey,
			Mimetype: proto.String(mimeT), FileEncSHA256: subida.FileEncSHA256, FileSHA256: subida.FileSHA256, FileLength: largo,
			ContextInfo: ctxInfo,
		}
		c.proto = st
		msg = &waE2E.Message{StickerMessage: st}
	case "video", "gif":
		vm := &waE2E.VideoMessage{
			URL: proto.String(subida.URL), DirectPath: proto.String(subida.DirectPath), MediaKey: subida.MediaKey,
			Mimetype: proto.String(mimeT), FileEncSHA256: subida.FileEncSHA256, FileSHA256: subida.FileSHA256, FileLength: largo,
			ContextInfo: ctxInfo, GifPlayback: proto.Bool(tipo == "gif"),
		}
		if epigrafe != "" {
			vm.Caption = proto.String(epigrafe)
		}
		if segundos > 0 {
			vm.Seconds = proto.Uint32(uint32(segundos))
		}
		c.proto = vm
		msg = &waE2E.Message{VideoMessage: vm}
	case "audio", "nota":
		am := &waE2E.AudioMessage{
			URL: proto.String(subida.URL), DirectPath: proto.String(subida.DirectPath), MediaKey: subida.MediaKey,
			Mimetype: proto.String(mimeT), FileEncSHA256: subida.FileEncSHA256, FileSHA256: subida.FileSHA256, FileLength: largo,
			ContextInfo: ctxInfo, PTT: proto.Bool(tipo == "nota"),
		}
		if tipo == "nota" {
			am.Mimetype = proto.String("audio/ogg; codecs=opus")
			c.media.Mime = "audio/ogg; codecs=opus"
			if onda := ondaDe(datos); onda != nil {
				am.Waveform = onda
				c.onda = onda
			}
		}
		if segundos > 0 {
			am.Seconds = proto.Uint32(uint32(segundos))
		}
		c.proto = am
		msg = &waE2E.Message{AudioMessage: am}
	default:
		dm := &waE2E.DocumentMessage{
			URL: proto.String(subida.URL), DirectPath: proto.String(subida.DirectPath), MediaKey: subida.MediaKey,
			Mimetype: proto.String(mimeT), FileEncSHA256: subida.FileEncSHA256, FileSHA256: subida.FileSHA256, FileLength: largo,
			ContextInfo: ctxInfo, FileName: proto.String(nombre), Title: proto.String(nombre),
		}
		if epigrafe != "" {
			dm.Caption = proto.String(epigrafe)
		}
		c.proto = dm
		c.media.Nombre = nombre
		msg = &waE2E.Message{DocumentMessage: dm}
	}
	resp, err := cli.SendMessage(ctx, j, msg)
	if err != nil {
		fallar(w, http.StatusBadGateway, err)
		return
	}
	// Copia local: lo que mandamos ya lo tenemos, no hace falta bajarlo.
	m := anotarEnviado(chat, resp, c, ctxInfo, "")
	if m.Media != nil {
		ruta := rutaPara(m.Media.ID, mimeT, nombre)
		os.MkdirAll(filepath.Dir(ruta), 0o755)
		if os.WriteFile(ruta, datos, 0o644) == nil {
			db.Exec("UPDATE media SET ruta = ? WHERE id = ?", ruta, m.Media.ID)
			asegurarMiniatura(m.Media.ID, chat, m.ID, tipo, ruta)
		}
	}
	responder(w, m)
}

func hReaccion(w http.ResponseWriter, r *http.Request) {
	var p struct {
		Chat  string `json:"chat"`
		ID    string `json:"id"`
		Emoji string `json:"emoji"`
	}
	if err := leerJSON(r, &p); err != nil {
		fallar(w, 400, err)
		return
	}
	j, ok := jidDe(p.Chat)
	m := mensajePorID(p.Chat, p.ID)
	if !ok || m == nil {
		fallar(w, 404, errors.New("message not found"))
		return
	}
	remitente := cli.Store.ID.ToNonAD()
	if !m.Propio {
		remitente, _ = jidDe(m.Remitente)
	}
	if _, err := cli.SendMessage(ctx, j, cli.BuildReaction(j, remitente, p.ID, p.Emoji)); err != nil {
		fallar(w, http.StatusBadGateway, err)
		return
	}
	yo := cli.Store.ID.ToNonAD().String()
	if p.Emoji == "" {
		db.Exec("DELETE FROM reacciones WHERE chat = ? AND mensaje = ? AND remitente = ?", p.Chat, p.ID, yo)
	} else {
		db.Exec(`INSERT INTO reacciones (chat, mensaje, remitente, emoji, ts) VALUES (?, ?, ?, ?, ?)
			ON DUPLICATE KEY UPDATE emoji = VALUES(emoji), ts = VALUES(ts)`, p.Chat, p.ID, yo, p.Emoji, ahoraMs())
	}
	evento("reaccion", p.Chat, map[string]any{"id": p.ID, "remitente": yo, "emoji": p.Emoji})
	responder(w, map[string]any{"ok": true})
}

func hEditar(w http.ResponseWriter, r *http.Request) {
	var p struct {
		Chat  string `json:"chat"`
		ID    string `json:"id"`
		Texto string `json:"texto"`
	}
	if err := leerJSON(r, &p); err != nil {
		fallar(w, 400, err)
		return
	}
	j, ok := jidDe(p.Chat)
	m := mensajePorID(p.Chat, p.ID)
	texto := strings.TrimSpace(p.Texto)
	if !ok || m == nil || !m.Propio || texto == "" {
		fallar(w, 400, errors.New("own message with text required"))
		return
	}
	nuevo := &waE2E.Message{Conversation: proto.String(texto)}
	if _, err := cli.SendMessage(ctx, j, cli.BuildEdit(j, p.ID, nuevo)); err != nil {
		fallar(w, http.StatusBadGateway, err)
		return
	}
	db.Exec("UPDATE mensajes SET texto = ?, editado = 1 WHERE chat = ? AND id_wa = ?", texto, p.Chat, p.ID)
	evento("editado", p.Chat, map[string]any{"id": p.ID, "texto": texto})
	responder(w, map[string]any{"ok": true})
}

func hBorrar(w http.ResponseWriter, r *http.Request) {
	var p struct {
		Chat string `json:"chat"`
		ID   string `json:"id"`
	}
	if err := leerJSON(r, &p); err != nil {
		fallar(w, 400, err)
		return
	}
	j, ok := jidDe(p.Chat)
	m := mensajePorID(p.Chat, p.ID)
	if !ok || m == nil {
		fallar(w, 404, errors.New("message not found"))
		return
	}
	remitente := cli.Store.ID.ToNonAD()
	if !m.Propio {
		// Borrar para todos un mensaje ajeno solo lo puede un admin del grupo;
		// en un chat comun o en los estados de otro, se borra aca nomas.
		remitente, _ = jidDe(m.Remitente)
	}
	if m.Propio || j.Server == types.GroupServer {
		if _, err := cli.SendMessage(ctx, j, cli.BuildRevoke(j, remitente, p.ID)); err != nil {
			fallar(w, http.StatusBadGateway, err)
			return
		}
	}
	db.Exec("UPDATE mensajes SET borrado = 1 WHERE chat = ? AND id_wa = ?", p.Chat, p.ID)
	evento("borrado", p.Chat, map[string]any{"id": p.ID})
	responder(w, map[string]any{"ok": true})
}

// POST /leido {chat, ids?}: marca leido en WhatsApp (tildes azules para el
// otro) y deja el chat sin no-leidos. Sin ids, marca los ajenos recientes.
func hLeido(w http.ResponseWriter, r *http.Request) {
	var p struct {
		Chat string   `json:"chat"`
		IDs  []string `json:"ids"`
	}
	if err := leerJSON(r, &p); err != nil {
		fallar(w, 400, err)
		return
	}
	j, ok := jidDe(p.Chat)
	if !ok {
		fallar(w, 400, errors.New("chat required"))
		return
	}
	// Agrupados por remitente, que es como lo pide whatsmeow.
	porRemitente := map[string][]types.MessageID{}
	if len(p.IDs) == 0 {
		filas, err := db.Query("SELECT id_wa, remitente FROM mensajes WHERE chat = ? AND propio = 0 ORDER BY ts DESC LIMIT 50", p.Chat)
		if err == nil {
			for filas.Next() {
				var id, rem string
				if filas.Scan(&id, &rem) == nil {
					porRemitente[rem] = append(porRemitente[rem], id)
				}
			}
			filas.Close()
		}
	} else {
		for _, id := range p.IDs {
			var rem string
			if db.QueryRow("SELECT remitente FROM mensajes WHERE chat = ? AND id_wa = ? AND propio = 0", p.Chat, id).Scan(&rem) == nil {
				porRemitente[rem] = append(porRemitente[rem], id)
			}
		}
	}
	for rem, ids := range porRemitente {
		s, _ := jidDe(rem)
		if err := cli.MarkRead(ctx, ids, time.Now(), j, s); err != nil {
			log.Printf("leido: %v", err)
		}
	}
	db.Exec("UPDATE chats SET no_leidos = 0 WHERE jid = ?", p.Chat)
	evento("leido", p.Chat, map[string]any{})
	responder(w, map[string]any{"ok": true})
}

// POST /presencia {chat}: pide que el telefono nos mande el "typing" de ese
// contacto (WhatsApp solo lo manda de a quien te suscribiste; los grupos no
// lo necesitan).
func hPresencia(w http.ResponseWriter, r *http.Request) {
	var p struct {
		Chat string `json:"chat"`
	}
	if err := leerJSON(r, &p); err != nil {
		fallar(w, 400, err)
		return
	}
	j, ok := jidDe(p.Chat)
	if !ok {
		fallar(w, 400, errors.New("chat required"))
		return
	}
	if j.Server == types.DefaultUserServer {
		if err := cli.SubscribePresence(ctx, j); err != nil {
			log.Printf("presencia %s: %v", p.Chat, err)
		}
	}
	responder(w, map[string]any{"ok": true})
}

// POST /llamada/rechazar {id}
func hRechazarLlamada(w http.ResponseWriter, r *http.Request) {
	var p struct {
		ID string `json:"id"`
	}
	if err := leerJSON(r, &p); err != nil {
		fallar(w, 400, err)
		return
	}
	if err := rechazarLlamada(p.ID); err != nil {
		fallar(w, 400, err)
		return
	}
	responder(w, map[string]any{"ok": true})
}

// POST /escribiendo {chat, estado: "typing"|"recording"|""}
func hEscribiendo(w http.ResponseWriter, r *http.Request) {
	var p struct {
		Chat   string `json:"chat"`
		Estado string `json:"estado"`
	}
	if err := leerJSON(r, &p); err != nil {
		fallar(w, 400, err)
		return
	}
	j, ok := jidDe(p.Chat)
	if !ok {
		fallar(w, 400, errors.New("chat required"))
		return
	}
	if j.Server == types.BroadcastServer {
		// A los estados no se les avisa que escribis.
		responder(w, map[string]any{"ok": true})
		return
	}
	estado, media := types.ChatPresencePaused, types.ChatPresenceMediaText
	switch p.Estado {
	case "typing":
		estado = types.ChatPresenceComposing
	case "recording":
		estado, media = types.ChatPresenceComposing, types.ChatPresenceMediaAudio
	}
	cli.SendChatPresence(ctx, j, estado, media)
	responder(w, map[string]any{"ok": true})
}
