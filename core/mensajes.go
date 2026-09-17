package main

import (
	"fmt"
	"log"
	"strings"
	"time"

	"go.mau.fi/whatsmeow/appstate"
	"go.mau.fi/whatsmeow/proto/waE2E"
	"go.mau.fi/whatsmeow/proto/waHistorySync"
	"go.mau.fi/whatsmeow/proto/waWeb"
	"go.mau.fi/whatsmeow/types"
	"go.mau.fi/whatsmeow/types/events"
	"google.golang.org/protobuf/proto"
)

// ---- JIDs -----------------------------------------------------------------

// Un JID como lo guardamos: sin agente ni dispositivo, y si es un LID del
// que conocemos el telefono, el telefono. Asi cada persona es una sola.
func normalizar(j types.JID) string {
	j = j.ToNonAD()
	if j.Server == types.HiddenUserServer {
		if pn, err := cli.Store.LIDs.GetPNForLID(ctx, j); err == nil && !pn.IsEmpty() {
			return pn.ToNonAD().String()
		}
	}
	return j.String()
}

// El chat y el remitente de un mensaje, ya normalizados. Los mensajes de
// listas de difusion van al chat de la persona que los mando.
func origen(info *types.MessageInfo) (chat, remitente string) {
	c := info.Chat
	if c.Server == types.HiddenUserServer && !info.RecipientAlt.IsEmpty() && info.RecipientAlt.Server == types.DefaultUserServer {
		c = info.RecipientAlt
	}
	s := info.Sender
	if s.Server == types.HiddenUserServer && !info.SenderAlt.IsEmpty() && info.SenderAlt.Server == types.DefaultUserServer {
		s = info.SenderAlt
	}
	if c.Server == types.BroadcastServer && c.User != "status" {
		if info.IsFromMe {
			return "", ""
		}
		c = s
	}
	return normalizar(c), normalizar(s)
}

func jidDe(s string) (types.JID, bool) {
	j, err := types.ParseJID(s)
	if err != nil || j.IsEmpty() {
		return types.JID{}, false
	}
	return j, true
}

// ---- los eventos de whatsmeow --------------------------------------------

func manejarEvento(e any) {
	if manejarLlamada(e) {
		return
	}
	switch v := e.(type) {
	case *events.Message:
		guardarMensaje(v, false, 0)
	case *events.Receipt:
		acuse(v)
	case *events.ChatPresence:
		// composing (texto o audio) o paused. Es efimero: el cliente lo
		// muestra unos segundos y evento() borra los viejos.
		estado := "nada"
		if v.State == types.ChatPresenceComposing {
			estado = "escribiendo"
			if v.Media == types.ChatPresenceMediaAudio {
				estado = "grabando"
			}
		}
		evento("escribiendo", normalizar(v.Chat), map[string]any{"quien": normalizar(v.Sender), "estado": estado})
	case *events.HistorySync:
		if cfg.Historia != "no" {
			go historia(v.Data)
		}
	case *events.MediaRetry:
		go reintentoMedia(v)
	case *events.Connected:
		conectado.Store(true)
		qrActual.Store("")
		log.Printf("conectado")
		evento("sesion", "", map[string]any{"conectado": true})
		// "Online": sin esto WhatsApp no manda la presencia (typing) de los demas.
		if err := cli.SendPresence(ctx, types.PresenceAvailable); err != nil {
			log.Printf("presencia: %v", err)
		}
		go refrescarGrupos()
		go refrescarContactos()
	case *events.PairSuccess:
		log.Printf("vinculado como %s (%s)", v.ID, v.BusinessName)
	case *events.Disconnected:
		conectado.Store(false)
		evento("sesion", "", map[string]any{"conectado": false})
	case *events.LoggedOut:
		conectado.Store(false)
		log.Printf("SESION CERRADA desde el telefono (%v); hay que volver a vincular", v.Reason)
		evento("sesion", "", map[string]any{"conectado": false, "cerrada": true})
	case *events.StreamReplaced:
		log.Printf("otro cliente se conecto con esta sesion")
	case *events.PushName:
		if v.NewPushName != "" {
			actualizarContacto(normalizar(v.JID), "nombre_push", v.NewPushName)
		}
	case *events.Contact:
		if v.Action != nil && v.Action.GetFullName() != "" {
			actualizarContacto(normalizar(v.JID), "nombre_agenda", v.Action.GetFullName())
		}
	case *events.GroupInfo:
		chat := normalizar(v.JID)
		if v.Name != nil {
			db.Exec("UPDATE chats SET nombre = ? WHERE jid = ?", v.Name.Name, chat)
			evento("chat", chat, chatDe(chat))
		}
		if len(v.Join) > 0 || len(v.Leave) > 0 || len(v.Promote) > 0 || len(v.Demote) > 0 {
			go refrescarGrupo(v.JID)
		}
	case *events.DeleteForMe:
		chat := normalizar(v.ChatJID)
		db.Exec("UPDATE mensajes SET borrado = 1 WHERE chat = ? AND id_wa = ?", chat, v.MessageID)
		evento("borrado", chat, map[string]any{"id": v.MessageID})
	case *events.AppStateSyncComplete:
		if v.Name == appstate.WAPatchCriticalUnblockLow {
			go refrescarContactos()
		}
	case *events.OfflineSyncCompleted:
		log.Printf("puesto al dia: %d cosas", v.Count)
	case *events.UndecryptableMessage:
		chat, rem := origen(&v.Info)
		if chat == "" {
			return
		}
		insertarMensaje(&Mensaje{
			ID: v.Info.ID, Chat: chat, Remitente: rem, Propio: v.Info.IsFromMe,
			TS: v.Info.Timestamp.UnixMilli(), Tipo: "sistema",
			Texto: "Waiting for this message. This may take a while.",
		}, false, v.Info.PushName)
	}
}

// ---- mensajes -------------------------------------------------------------

// Que trae un mensaje: tipo, texto, el sub-mensaje con media (si hay) y el
// contexto (cita, reenvio).
type contenido struct {
	tipo   string
	texto  string
	media  *Media
	proto  proto.Message // el sub-mensaje descargable, para bajarlo despues
	ctx    *waE2E.ContextInfo
	mini   []byte
	onda   []byte
}

func clasificar(msg *waE2E.Message) (c contenido) {
	switch {
	case msg.GetConversation() != "":
		c.tipo, c.texto = "texto", msg.GetConversation()
	case msg.GetExtendedTextMessage() != nil:
		t := msg.GetExtendedTextMessage()
		c.tipo, c.texto, c.ctx = "texto", t.GetText(), t.GetContextInfo()
	case msg.GetImageMessage() != nil:
		i := msg.GetImageMessage()
		c.tipo, c.texto, c.ctx, c.proto, c.mini = "imagen", i.GetCaption(), i.GetContextInfo(), i, i.GetJPEGThumbnail()
		c.media = &Media{Mime: i.GetMimetype(), Bytes: int64(i.GetFileLength()), Ancho: int(i.GetWidth()), Alto: int(i.GetHeight())}
	case msg.GetVideoMessage() != nil:
		v := msg.GetVideoMessage()
		c.tipo, c.texto, c.ctx, c.proto, c.mini = "video", v.GetCaption(), v.GetContextInfo(), v, v.GetJPEGThumbnail()
		if v.GetGifPlayback() {
			c.tipo = "gif"
		}
		c.media = &Media{Mime: v.GetMimetype(), Bytes: int64(v.GetFileLength()), Ancho: int(v.GetWidth()), Alto: int(v.GetHeight()), Segundos: int(v.GetSeconds())}
	case msg.GetPtvMessage() != nil:
		v := msg.GetPtvMessage()
		c.tipo, c.ctx, c.proto, c.mini = "video", v.GetContextInfo(), v, v.GetJPEGThumbnail()
		c.media = &Media{Mime: v.GetMimetype(), Bytes: int64(v.GetFileLength()), Ancho: int(v.GetWidth()), Alto: int(v.GetHeight()), Segundos: int(v.GetSeconds())}
	case msg.GetAudioMessage() != nil:
		a := msg.GetAudioMessage()
		c.tipo, c.ctx, c.proto = "audio", a.GetContextInfo(), a
		if a.GetPTT() {
			c.tipo = "nota"
		}
		c.media = &Media{Mime: a.GetMimetype(), Bytes: int64(a.GetFileLength()), Segundos: int(a.GetSeconds())}
		c.onda = a.GetWaveform()
	case msg.GetDocumentMessage() != nil:
		d := msg.GetDocumentMessage()
		c.tipo, c.texto, c.ctx, c.proto, c.mini = "documento", d.GetCaption(), d.GetContextInfo(), d, d.GetJPEGThumbnail()
		c.media = &Media{Mime: d.GetMimetype(), Nombre: d.GetFileName(), Bytes: int64(d.GetFileLength())}
		if c.media.Nombre == "" {
			c.media.Nombre = d.GetTitle()
		}
	case msg.GetStickerMessage() != nil:
		s := msg.GetStickerMessage()
		c.tipo, c.ctx, c.proto = "figurita", s.GetContextInfo(), s
		c.media = &Media{Mime: s.GetMimetype(), Bytes: int64(s.GetFileLength()), Ancho: int(s.GetWidth()), Alto: int(s.GetHeight())}
	case msg.GetLocationMessage() != nil:
		l := msg.GetLocationMessage()
		c.tipo, c.ctx = "ubicacion", l.GetContextInfo()
		c.texto = fmt.Sprintf("%.6f,%.6f", l.GetDegreesLatitude(), l.GetDegreesLongitude())
		if l.GetName() != "" {
			c.texto = l.GetName() + "\n" + c.texto
		}
	case msg.GetLiveLocationMessage() != nil:
		l := msg.GetLiveLocationMessage()
		c.tipo, c.ctx = "ubicacion", l.GetContextInfo()
		c.texto = fmt.Sprintf("%.6f,%.6f", l.GetDegreesLatitude(), l.GetDegreesLongitude())
	case msg.GetContactMessage() != nil:
		k := msg.GetContactMessage()
		c.tipo, c.texto, c.ctx = "contacto", k.GetDisplayName()+"\n"+k.GetVcard(), k.GetContextInfo()
	case msg.GetContactsArrayMessage() != nil:
		k := msg.GetContactsArrayMessage()
		c.tipo, c.ctx = "contacto", k.GetContextInfo()
		partes := []string{}
		for _, uno := range k.GetContacts() {
			partes = append(partes, uno.GetDisplayName()+"\n"+uno.GetVcard())
		}
		c.texto = strings.Join(partes, "\n\n")
	case msg.GetPollCreationMessageV3() != nil || msg.GetPollCreationMessageV2() != nil || msg.GetPollCreationMessage() != nil:
		p := msg.GetPollCreationMessageV3()
		if p == nil {
			p = msg.GetPollCreationMessageV2()
		}
		if p == nil {
			p = msg.GetPollCreationMessage()
		}
		c.tipo, c.ctx = "encuesta", p.GetContextInfo()
		c.texto = p.GetName()
		for _, o := range p.GetOptions() {
			c.texto += "\n• " + o.GetOptionName()
		}
	case msg.GetGroupInviteMessage() != nil:
		g := msg.GetGroupInviteMessage()
		c.tipo, c.ctx = "texto", g.GetContextInfo()
		c.texto = "Group invite: " + g.GetGroupName()
		if g.GetCaption() != "" {
			c.texto += "\n" + g.GetCaption()
		}
	case msg.GetEventMessage() != nil:
		e := msg.GetEventMessage()
		c.tipo, c.ctx, c.texto = "texto", e.GetContextInfo(), "Event: "+e.GetName()
		if e.GetDescription() != "" {
			c.texto += "\n" + e.GetDescription()
		}
	case msg.GetButtonsMessage() != nil:
		b := msg.GetButtonsMessage()
		c.tipo, c.ctx, c.texto = "texto", b.GetContextInfo(), b.GetContentText()
	case msg.GetListMessage() != nil:
		l := msg.GetListMessage()
		c.tipo, c.ctx, c.texto = "texto", l.GetContextInfo(), l.GetTitle()+"\n"+l.GetDescription()
	case msg.GetTemplateMessage() != nil:
		t := msg.GetTemplateMessage()
		c.tipo, c.ctx = "texto", t.GetContextInfo()
		c.texto = t.GetHydratedTemplate().GetHydratedContentText()
	case msg.GetInteractiveMessage() != nil:
		i := msg.GetInteractiveMessage()
		c.tipo, c.ctx = "texto", i.GetContextInfo()
		c.texto = strings.TrimSpace(i.GetHeader().GetTitle() + "\n" + i.GetBody().GetText())
	case msg.GetProductMessage() != nil:
		pm := msg.GetProductMessage()
		p := pm.GetProduct()
		c.tipo, c.ctx = "texto", pm.GetContextInfo()
		c.texto = strings.TrimSpace("*" + p.GetTitle() + "*\n" + precio(p.GetCurrencyCode(), p.GetPriceAmount1000()) + "\n" + p.GetDescription())
		if pm.GetBody() != "" {
			c.texto += "\n\n" + pm.GetBody()
		}
		if im := p.GetProductImage(); im != nil && im.GetURL() != "" {
			c.tipo = "imagen"
			c.media = &Media{Mime: im.GetMimetype(), Bytes: int64(im.GetFileLength()), Ancho: int(im.GetWidth()), Alto: int(im.GetHeight())}
			c.proto, c.mini = im, im.GetJPEGThumbnail()
		}
	case msg.GetOrderMessage() != nil:
		o := msg.GetOrderMessage()
		c.tipo, c.ctx = "texto", o.GetContextInfo()
		c.texto = strings.TrimSpace(fmt.Sprintf("\U0001F6D2 Order: %s\n%d items · %s\n%s", o.GetOrderTitle(), o.GetItemCount(), precio(o.GetTotalCurrencyCode(), o.GetTotalAmount1000()), o.GetMessage()))
	case msg.GetCallLogMesssage() != nil:
		c.tipo, c.texto = "sistema", "Call"
	}
	return
}

// Guarda un mensaje que llego (en vivo o por history sync). Devuelve si se
// guardo algo nuevo.
func guardarMensaje(evt *events.Message, deHistoria bool, estadoInicial int) bool {
	msg := evt.Message
	if msg == nil {
		return false
	}
	chat, remitente := origen(&evt.Info)
	if chat == "" {
		return false
	}
	ts := evt.Info.Timestamp.UnixMilli()

	// Los protocolares: borrado y edicion.
	if pm := msg.GetProtocolMessage(); pm != nil {
		id := pm.GetKey().GetID()
		switch pm.GetType() {
		case waE2E.ProtocolMessage_REVOKE:
			if id != "" {
				db.Exec("UPDATE mensajes SET borrado = 1 WHERE chat = ? AND id_wa = ?", chat, id)
				evento("borrado", chat, map[string]any{"id": id})
			}
		case waE2E.ProtocolMessage_MESSAGE_EDIT:
			nuevo := clasificar(pm.GetEditedMessage())
			if id != "" && nuevo.tipo != "" {
				db.Exec("UPDATE mensajes SET texto = ?, editado = 1 WHERE chat = ? AND id_wa = ?", nuevo.texto, chat, id)
				evento("editado", chat, map[string]any{"id": id, "texto": nuevo.texto})
			}
		}
		return false
	}
	if r := msg.GetReactionMessage(); r != nil {
		id := r.GetKey().GetID()
		if id == "" {
			return false
		}
		if r.GetText() == "" {
			db.Exec("DELETE FROM reacciones WHERE chat = ? AND mensaje = ? AND remitente = ?", chat, id, remitente)
		} else {
			db.Exec(`INSERT INTO reacciones (chat, mensaje, remitente, emoji, ts) VALUES (?, ?, ?, ?, ?)
				ON DUPLICATE KEY UPDATE emoji = VALUES(emoji), ts = VALUES(ts)`, chat, id, remitente, r.GetText(), ts)
		}
		evento("reaccion", chat, map[string]any{"id": id, "remitente": remitente, "emoji": r.GetText()})
		return false
	}

	c := clasificar(msg)
	if c.tipo == "" {
		return false
	}
	m := &Mensaje{
		ID: evt.Info.ID, Chat: chat, Remitente: remitente, Propio: evt.Info.IsFromMe,
		TS: ts, Tipo: c.tipo, Texto: c.texto, Estado: estadoInicial,
	}
	if c.ctx != nil {
		m.CitaID = c.ctx.GetStanzaID()
		if p, err := types.ParseJID(c.ctx.GetParticipant()); err == nil && !p.IsEmpty() {
			m.CitaRemitente = normalizar(p)
		}
		if q := c.ctx.GetQuotedMessage(); q != nil {
			qc := clasificar(q)
			m.CitaTexto = qc.texto
			if m.CitaTexto == "" && qc.tipo != "" {
				m.CitaTexto = nombreTipo(qc.tipo)
			}
		}
		m.Reenviado = c.ctx.GetIsForwarded()
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
		res, err := db.Exec(`INSERT INTO media (chat, mensaje, mime, nombre, bytes, ancho, alto, segundos, miniatura, proto, onda)
			VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)`,
			chat, m.ID, c.media.Mime, c.media.Nombre, c.media.Bytes, c.media.Ancho, c.media.Alto, c.media.Segundos, mini, crudo, onda)
		if err != nil {
			log.Printf("media: %v", err)
		} else {
			c.media.ID, _ = res.LastInsertId()
			c.media.Miniatura = len(c.mini) > 0
			for _, b := range c.onda {
				c.media.Onda = append(c.media.Onda, int(b))
			}
			m.Media = c.media
		}
	}
	nuevo := insertarMensaje(m, deHistoria, evt.Info.PushName)
	if nuevo {
		guardarBotones(chat, m.ID, msg)
	}
	if nuevo && m.Media != nil && !deHistoria {
		pedirBajada(m.Media.ID)
	}
	if !nuevo && m.Media != nil {
		// Ya estaba (repetido del history sync): la fila de media sobra.
		db.Exec("DELETE FROM media WHERE id = ?", m.Media.ID)
	}
	return nuevo
}

func nombreTipo(tipo string) string {
	switch tipo {
	case "imagen":
		return "Photo"
	case "video", "gif":
		return "Video"
	case "audio":
		return "Audio"
	case "nota":
		return "Voice message"
	case "documento":
		return "Document"
	case "figurita":
		return "Sticker"
	case "ubicacion":
		return "Location"
	case "contacto":
		return "Contact"
	case "encuesta":
		return "Poll"
	}
	return ""
}

// Inserta la fila y mantiene el chat (ultimo_ts, no leidos, nombre).
// Devuelve false si el mensaje ya estaba.
func insertarMensaje(m *Mensaje, deHistoria bool, nombrePush string) bool {
	var mediaID any
	if m.Media != nil {
		mediaID = m.Media.ID
	}
	res, err := db.Exec(`INSERT IGNORE INTO mensajes
		(id_wa, chat, remitente, propio, ts, tipo, texto, cita_id, cita_remitente, cita_texto, media_id, estado, reenviado)
		VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)`,
		m.ID, m.Chat, m.Remitente, m.Propio, m.TS, m.Tipo, m.Texto, m.CitaID, m.CitaRemitente, m.CitaTexto, mediaID, m.Estado, m.Reenviado)
	if err != nil {
		log.Printf("mensaje: %v", err)
		return false
	}
	if n, _ := res.RowsAffected(); n == 0 {
		return false
	}
	if nombrePush != "" && !m.Propio {
		actualizarContacto(m.Remitente, "nombre_push", nombrePush)
	}
	asegurarChat(m.Chat)
	noLeido := 0
	if !m.Propio && !deHistoria {
		noLeido = 1
	}
	db.Exec("UPDATE chats SET ultimo_ts = GREATEST(ultimo_ts, ?), no_leidos = no_leidos + ? WHERE jid = ?", m.TS, noLeido, m.Chat)
	if !deHistoria {
		evento("mensaje", m.Chat, m)
	}
	return true
}

func asegurarChat(jid string) {
	j, _ := jidDe(jid)
	esGrupo := j.Server == types.GroupServer
	db.Exec("INSERT IGNORE INTO chats (jid, es_grupo) VALUES (?, ?)", jid, esGrupo)
	if j.Server == types.DefaultUserServer {
		db.Exec("INSERT IGNORE INTO contactos (jid, telefono) VALUES (?, ?)", jid, j.User)
	}
}

func actualizarContacto(jid, columna, valor string) {
	j, ok := jidDe(jid)
	if !ok || j.Server == types.GroupServer || j.Server == types.BroadcastServer {
		return
	}
	var actual string
	db.QueryRow("SELECT "+columna+" FROM contactos WHERE jid = ?", jid).Scan(&actual)
	if actual == valor {
		return
	}
	db.Exec("INSERT INTO contactos (jid, telefono, "+columna+") VALUES (?, ?, ?) ON DUPLICATE KEY UPDATE "+columna+" = VALUES("+columna+")",
		jid, j.User, valor)
	evento("contacto", "", contactoDe(jid))
}

// ---- acuses ---------------------------------------------------------------

func acuse(r *events.Receipt) {
	chat := normalizar(r.Chat)
	if r.Chat.Server == types.BroadcastServer && r.Chat.User != "status" {
		chat = normalizar(r.Sender)
	}
	estado := 0
	switch r.Type {
	case types.ReceiptTypeDelivered:
		estado = 2
	case types.ReceiptTypeRead, types.ReceiptTypePlayed:
		estado = 3
	case types.ReceiptTypeReadSelf, types.ReceiptTypePlayedSelf:
		// Lo lei desde el telefono: el chat queda sin no-leidos.
		db.Exec("UPDATE chats SET no_leidos = 0 WHERE jid = ?", chat)
		evento("leido", chat, map[string]any{})
		return
	default:
		return
	}
	for _, id := range r.MessageIDs {
		res, err := db.Exec("UPDATE mensajes SET estado = ? WHERE chat = ? AND id_wa = ? AND propio = 1 AND estado < ?", estado, chat, id, estado)
		if err != nil {
			continue
		}
		if n, _ := res.RowsAffected(); n > 0 {
			evento("acuse", chat, map[string]any{"id": id, "estado": estado})
		}
	}
}

// ---- history sync ---------------------------------------------------------

func historia(d *waHistorySync.HistorySync) {
	if d == nil {
		return
	}
	log.Printf("history sync %s: %d chats, %d pushnames", d.GetSyncType(), len(d.GetConversations()), len(d.GetPushnames()))
	for _, pn := range d.GetPushnames() {
		if j, err := types.ParseJID(pn.GetID()); err == nil && pn.GetPushname() != "" {
			actualizarContacto(normalizar(j), "nombre_push", pn.GetPushname())
		}
	}
	total := 0
	for _, conv := range d.GetConversations() {
		jid, err := types.ParseJID(conv.GetID())
		if err != nil {
			continue
		}
		chat := normalizar(jid)
		n := 0
		for _, hm := range conv.GetMessages() {
			wm := hm.GetMessage()
			if wm == nil || wm.GetMessage() == nil {
				continue
			}
			evt, err := cli.ParseWebMessage(jid, wm)
			if err != nil {
				continue
			}
			estado := 0
			if evt.Info.IsFromMe {
				estado = estadoDeWeb(wm.GetStatus())
			}
			if guardarMensaje(evt, true, estado) {
				n++
			}
		}
		total += n
		asegurarChat(chat)
		if jid.Server == types.GroupServer && conv.GetName() != "" {
			db.Exec("UPDATE chats SET nombre = ? WHERE jid = ? AND nombre = ''", conv.GetName(), chat)
		}
		if conv.GetConversationTimestamp() > 0 {
			db.Exec("UPDATE chats SET ultimo_ts = GREATEST(ultimo_ts, ?) WHERE jid = ?", int64(conv.GetConversationTimestamp())*1000, chat)
		}
		if conv.GetArchived() {
			db.Exec("UPDATE chats SET archivado = 1 WHERE jid = ?", chat)
		}
		if n > 0 {
			evento("historia", chat, map[string]any{"cantidad": n})
		}
	}
	log.Printf("history sync %s: %d mensajes nuevos", d.GetSyncType(), total)
	evento("chats", "", map[string]any{})
}

func estadoDeWeb(s waWeb.WebMessageInfo_Status) int {
	switch s {
	case waWeb.WebMessageInfo_SERVER_ACK:
		return 1
	case waWeb.WebMessageInfo_DELIVERY_ACK:
		return 2
	case waWeb.WebMessageInfo_READ, waWeb.WebMessageInfo_PLAYED:
		return 3
	}
	return 0
}

// ---- contactos y grupos ---------------------------------------------------

func refrescarContactos() {
	todos, err := cli.Store.Contacts.GetAllContacts(ctx)
	if err != nil {
		log.Printf("contactos: %v", err)
		return
	}
	n := 0
	for jid, c := range todos {
		if jid.Server != types.DefaultUserServer && jid.Server != types.HiddenUserServer {
			continue
		}
		clave := normalizar(jid)
		j, _ := jidDe(clave)
		if j.Server != types.DefaultUserServer {
			continue
		}
		nombre := c.FullName
		if nombre == "" {
			nombre = c.FirstName
		}
		if nombre == "" && c.BusinessName != "" {
			nombre = c.BusinessName
		}
		lid := ""
		if jid.Server == types.HiddenUserServer {
			lid = jid.String()
		} else if l, err := cli.Store.LIDs.GetLIDForPN(ctx, jid); err == nil && !l.IsEmpty() {
			lid = l.ToNonAD().String()
		}
		db.Exec(`INSERT INTO contactos (jid, lid, telefono, nombre_push, nombre_agenda) VALUES (?, ?, ?, ?, ?)
			ON DUPLICATE KEY UPDATE
			lid = IF(VALUES(lid) = '', lid, VALUES(lid)),
			nombre_push = IF(VALUES(nombre_push) = '', nombre_push, VALUES(nombre_push)),
			nombre_agenda = IF(VALUES(nombre_agenda) = '', nombre_agenda, VALUES(nombre_agenda))`,
			clave, lid, j.User, c.PushName, nombre)
		n++
	}
	log.Printf("contactos: %d", n)
	evento("contactos", "", map[string]any{})
}

func refrescarGrupos() {
	grupos, err := cli.GetJoinedGroups(ctx)
	if err != nil {
		log.Printf("grupos: %v", err)
		return
	}
	for _, g := range grupos {
		guardarGrupo(g)
	}
	log.Printf("grupos: %d", len(grupos))
	evento("chats", "", map[string]any{})
}

func refrescarGrupo(jid types.JID) {
	g, err := cli.GetGroupInfo(ctx, jid)
	if err != nil {
		return
	}
	guardarGrupo(g)
	evento("chat", normalizar(jid), chatDe(normalizar(jid)))
}

func guardarGrupo(g *types.GroupInfo) {
	chat := normalizar(g.JID)
	db.Exec(`INSERT INTO chats (jid, nombre, es_grupo) VALUES (?, ?, 1)
		ON DUPLICATE KEY UPDATE nombre = IF(VALUES(nombre) = '', nombre, VALUES(nombre))`, chat, g.Name)
	db.Exec("DELETE FROM miembros WHERE chat = ?", chat)
	for _, p := range g.Participants {
		j := p.JID
		if !p.PhoneNumber.IsEmpty() {
			j = p.PhoneNumber
		}
		quien := normalizar(j)
		db.Exec("INSERT IGNORE INTO miembros (chat, jid, admin) VALUES (?, ?, ?)", chat, quien, p.IsAdmin || p.IsSuperAdmin)
		if !p.LID.IsEmpty() && !p.PhoneNumber.IsEmpty() {
			db.Exec("INSERT INTO contactos (jid, telefono, lid) VALUES (?, ?, ?) ON DUPLICATE KEY UPDATE lid = VALUES(lid)",
				quien, p.PhoneNumber.User, p.LID.ToNonAD().String())
		} else {
			asegurarChatContacto(quien)
		}
	}
}

func asegurarChatContacto(jid string) {
	if j, ok := jidDe(jid); ok && j.Server == types.DefaultUserServer {
		db.Exec("INSERT IGNORE INTO contactos (jid, telefono) VALUES (?, ?)", jid, j.User)
	}
}

// ---- lecturas para la API -------------------------------------------------

func chatDe(jid string) *Chat {
	f := db.QueryRow(`SELECT c.jid, c.es_grupo, c.foto, c.ultimo_ts, c.no_leidos, c.archivado,
		IF(c.es_grupo, c.nombre, COALESCE(NULLIF(k.nombre_agenda, ''), NULLIF(k.nombre_push, ''), c.nombre, '')), COALESCE(k.foto, '')
		FROM chats c LEFT JOIN contactos k ON k.jid = c.jid WHERE c.jid = ?`, jid)
	var c Chat
	var fotoContacto string
	if err := f.Scan(&c.JID, &c.EsGrupo, &c.Foto, &c.UltimoTS, &c.NoLeidos, &c.Archivado, &c.Nombre, &fotoContacto); err != nil {
		return nil
	}
	if c.Foto == "" {
		c.Foto = fotoContacto
	}
	return &c
}

func contactoDe(jid string) *Contacto {
	var c Contacto
	err := db.QueryRow("SELECT jid, lid, telefono, nombre_push, nombre_agenda, foto FROM contactos WHERE jid = ?", jid).
		Scan(&c.JID, &c.LID, &c.Telefono, &c.NombrePush, &c.NombreAgenda, &c.Foto)
	if err != nil {
		return nil
	}
	return &c
}

func esperarConexion(d time.Duration) bool {
	tope := time.Now().Add(d)
	for !conectado.Load() && time.Now().Before(tope) {
		time.Sleep(200 * time.Millisecond)
	}
	return conectado.Load()
}
