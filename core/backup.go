package main

// Importa el backup de iPhone (ChatStorage.sqlite + Message/Media) a la base.
//
//	kciwapp-server backup <carpeta> [jid del chat]
//
// La carpeta tiene ChatStorage.sqlite y Message/Media/<jid>/x/y/archivo.
// Se cruza por ZSTANZAID = id_wa: lo que ya esta no se duplica, y si el
// mensaje existente no tenia media y en el backup esta el archivo, se le
// pega. Los chats importados antes del export (ids imp-N) se cruzan por
// fecha + propio + texto/tipo. Sin argumento de chat va todo.

import (
	"database/sql"
	"fmt"
	"log"
	"os"
	"path/filepath"
	"strings"
	"time"

	"go.mau.fi/whatsmeow/types"
)

// Segundos entre el epoch de Core Data (2001) y el de Unix.
const epochCoreData = 978307200

type msgBackup struct {
	pk         int64
	stanza     string
	fromJID    sql.NullString
	fromMe     int
	tipo       int
	fecha      float64
	texto      sql.NullString
	miembro    sql.NullInt64
	estado     int
	eventoTipo int
	// media
	mediaPK    sql.NullInt64
	ruta       sql.NullString
	mime       sql.NullString
	titulo     sql.NullString
	vcardNom   sql.NullString
	segundos   sql.NullFloat64
	lat, lon   sql.NullFloat64
	bytes      sql.NullInt64
}

type existente struct {
	id     string
	ts     int64
	propio bool
	tipo   string
	texto  string
	media  bool
	// Si tiene media: su id y si esta bajada (estado 1).
	mediaID     int64
	mediaBajada bool
}

func backupCLI(args []string) {
	if len(args) < 1 {
		log.Fatalf("uso: kciwapp-server backup <carpeta> [chat jid]")
	}
	carpeta := args[0]
	soloChat := ""
	if len(args) > 1 {
		soloChat = args[1]
	}
	if cli.Store.ID == nil {
		log.Fatalf("no hay sesion vinculada (necesito saber mi jid)")
	}
	yo := cli.Store.ID.ToNonAD().String()

	src, err := sql.Open("sqlite3", "file:"+filepath.Join(carpeta, "ChatStorage.sqlite")+"?mode=ro&_busy_timeout=5000")
	if err != nil {
		log.Fatalf("ChatStorage: %v", err)
	}
	defer src.Close()

	// LID -> telefono: lo que sepa el store, mas lo que se deduce del backup
	// (mensajes de un chat 1:1 con telefono cuyo remitente es un LID).
	lids := map[string]string{}
	filas, err := src.Query(`SELECT DISTINCT m.ZFROMJID, s.ZCONTACTJID FROM ZWAMESSAGE m JOIN ZWACHATSESSION s ON s.Z_PK = m.ZCHATSESSION
		WHERE m.ZFROMJID LIKE '%@lid' AND s.ZCONTACTJID LIKE '%@s.whatsapp.net' AND s.ZSESSIONTYPE = 0 AND m.ZISFROMME = 0`)
	if err != nil {
		log.Fatalf("lids: %v", err)
	}
	for filas.Next() {
		var lid, pn string
		if filas.Scan(&lid, &pn) == nil {
			lids[lid] = pn
		}
	}
	filas.Close()
	log.Printf("lids deducidos del backup: %d", len(lids))
	norm := func(s string) string {
		if s == "" {
			return ""
		}
		if pn, ok := lids[s]; ok {
			return pn
		}
		j, err := types.ParseJID(s)
		if err != nil {
			return s
		}
		return normalizar(j)
	}

	// Miembros de grupo (Z_PK -> jid).
	miembros := map[int64]string{}
	filas, _ = src.Query(`SELECT Z_PK, ZMEMBERJID FROM ZWAGROUPMEMBER WHERE ZMEMBERJID IS NOT NULL`)
	for filas.Next() {
		var pk int64
		var jid string
		if filas.Scan(&pk, &jid) == nil {
			miembros[pk] = norm(jid)
		}
	}
	filas.Close()

	// Sesiones: 0 = persona, 1 = grupo. El resto (difusion, estados,
	// comunidades) no.
	type sesion struct {
		pk     int64
		jid    string
		nombre string
		grupo  bool
		cuenta int64
	}
	var sesiones []sesion
	filas, err = src.Query(`SELECT Z_PK, ZCONTACTJID, COALESCE(ZPARTNERNAME, ''), ZSESSIONTYPE, COALESCE(ZMESSAGECOUNTER, 0)
		FROM ZWACHATSESSION WHERE ZSESSIONTYPE IN (0, 1) AND ZCONTACTJID IS NOT NULL ORDER BY ZMESSAGECOUNTER DESC`)
	if err != nil {
		log.Fatalf("sesiones: %v", err)
	}
	for filas.Next() {
		var s sesion
		var tipo int
		if filas.Scan(&s.pk, &s.jid, &s.nombre, &tipo, &s.cuenta) != nil {
			continue
		}
		if strings.HasSuffix(s.jid, "@status") || strings.HasPrefix(s.jid, "status@") || strings.HasSuffix(s.jid, "@broadcast") {
			continue
		}
		s.grupo = tipo == 1
		s.jid = norm(s.jid)
		if soloChat != "" && s.jid != soloChat {
			continue
		}
		sesiones = append(sesiones, s)
	}
	filas.Close()
	log.Printf("%d chats en el backup", len(sesiones))

	consulta := `SELECT m.Z_PK, COALESCE(m.ZSTANZAID, ''), m.ZFROMJID, COALESCE(m.ZISFROMME, 0), COALESCE(m.ZMESSAGETYPE, 0), COALESCE(m.ZMESSAGEDATE, 0),
		m.ZTEXT, m.ZGROUPMEMBER, COALESCE(m.ZMESSAGESTATUS, 0), COALESCE(m.ZGROUPEVENTTYPE, 0),
		mi.Z_PK, mi.ZMEDIALOCALPATH, mi.ZVCARDSTRING, mi.ZTITLE, mi.ZVCARDNAME, mi.ZMOVIEDURATION, mi.ZLATITUDE, mi.ZLONGITUDE, mi.ZFILESIZE
		FROM ZWAMESSAGE m LEFT JOIN ZWAMEDIAITEM mi ON mi.ZMESSAGE = m.Z_PK
		WHERE m.ZCHATSESSION = ? ORDER BY m.ZMESSAGEDATE, m.Z_PK`

	var totalNuevos, totalMedia, totalPegados, totalIguales int64
	var idsMedia []int64
	inicio := time.Now()
	for _, s := range sesiones {
		if s.cuenta == 0 {
			continue
		}
		nuevos, media, pegados, iguales, ids := importarSesion(src, consulta, s.pk, s.jid, s.nombre, s.grupo, yo, carpeta, miembros, norm)
		totalNuevos += nuevos
		totalMedia += media
		totalPegados += pegados
		totalIguales += iguales
		idsMedia = append(idsMedia, ids...)
		log.Printf("%-40s nuevos %7d  ya estaban %7d  media nueva %6d  pegada %5d", recortar(s.nombre+" ("+s.jid+")", 40), nuevos, iguales, media, pegados)
	}
	log.Printf("total: %d mensajes nuevos, %d ya estaban, %d media nueva, %d media pegada a mensajes existentes (%s)",
		totalNuevos, totalIguales, totalMedia, totalPegados, time.Since(inicio).Round(time.Second))
	if len(idsMedia) > 0 {
		log.Printf("ondas / miniaturas / duraciones de %d archivos...", len(idsMedia))
		procesarMediaEnParalelo(idsMedia, "")
	}
	// Marca para que los clientes tiren su cache de mensajes y recarguen.
	guardarValor("importacion", fmt.Sprintf("%d", ahoraMs()))
	log.Printf("listo")
}

func recortar(s string, n int) string {
	r := []rune(s)
	if len(r) > n {
		return string(r[:n])
	}
	return s
}

// Tipo nuestro a partir del ZMESSAGETYPE de iOS. "" = no se importa.
func tipoDeBackup(m *msgBackup) (tipo, texto string, borrado bool) {
	t := ""
	if m.texto.Valid {
		t = m.texto.String
	}
	mime := ""
	if m.mime.Valid {
		mime = m.mime.String
	}
	switch m.tipo {
	case 0, 7, 25, 31, 32, 34: // texto, link, botones/listas de negocios
		if strings.TrimSpace(t) == "" {
			return "", "", false
		}
		return "texto", t, false
	case 1, 20, 38: // imagen (20 = ver una vez, 38 = ?)
		return "imagen", t, false
	case 2, 23:
		return "video", t, false
	case 11:
		return "gif", t, false
	case 3:
		if strings.Contains(mime, "opus") || strings.Contains(mime, "ogg") {
			return "nota", "", false
		}
		return "audio", "", false
	case 4:
		nom := ""
		if m.vcardNom.Valid {
			nom = m.vcardNom.String
		}
		return "contacto", strings.TrimSpace(nom + "\n" + mime), false
	case 5:
		if m.lat.Valid && m.lon.Valid && (m.lat.Float64 != 0 || m.lon.Float64 != 0) {
			return "ubicacion", fmt.Sprintf("%.6f,%.6f", m.lat.Float64, m.lon.Float64), false
		}
		return "", "", false
	case 6:
		return "sistema", eventoGrupo(m.eventoTipo), false
	case 8:
		return "documento", "", false
	case 10:
		return "sistema", "\U0001F4DE Call", false
	case 14:
		return "texto", "", true
	case 15:
		return "figurita", "", false
	}
	return "", "", false
}

func eventoGrupo(t int) string {
	switch t {
	case 1:
		return "joined the group"
	case 2:
		return "left the group"
	case 3:
		return "was added"
	case 4:
		return "was removed"
	case 5:
		return "changed the subject"
	case 6:
		return "changed the group icon"
	case 12:
		return "Messages are end-to-end encrypted"
	}
	return "Group event"
}

func importarSesion(src *sql.DB, consulta string, pk int64, chat, nombre string, grupo bool, yo, carpeta string,
	miembros map[int64]string, norm func(string) string) (nuevos, mediaNueva, pegados, iguales int64, idsMedia []int64) {
	// Lo que ya hay de este chat.
	porID := map[string]*existente{}
	porSegundo := map[int64][]*existente{} // solo los imp-N
	filas, err := db.Query(`SELECT m.id_wa, m.ts, m.propio, m.tipo, m.texto, m.media_id IS NOT NULL, COALESCE(m.media_id, 0), COALESCE(md.estado, 0) = 1
		FROM mensajes m LEFT JOIN media md ON md.id = m.media_id WHERE m.chat = ?`, chat)
	if err != nil {
		log.Printf("%s: %v", chat, err)
		return
	}
	for filas.Next() {
		e := &existente{}
		if filas.Scan(&e.id, &e.ts, &e.propio, &e.tipo, &e.texto, &e.media, &e.mediaID, &e.mediaBajada) != nil {
			continue
		}
		porID[e.id] = e
		if strings.HasPrefix(e.id, "imp-") {
			porSegundo[e.ts/1000] = append(porSegundo[e.ts/1000], e)
		}
	}
	filas.Close()
	hayImp := len(porSegundo) > 0

	asegurarChat(chat)
	if !grupo && nombre != "" {
		db.Exec("UPDATE contactos SET nombre_agenda = ? WHERE jid = ? AND nombre_agenda = ''", nombre, chat)
	} else if grupo && nombre != "" {
		db.Exec("UPDATE chats SET nombre = ? WHERE jid = ? AND nombre = ''", nombre, chat)
	}

	filas, err = src.Query(consulta, pk)
	if err != nil {
		log.Printf("%s: %v", chat, err)
		return
	}
	defer filas.Close()

	tx, err := db.Begin()
	if err != nil {
		log.Printf("tx: %v", err)
		return
	}
	insMsg, _ := tx.Prepare(`INSERT IGNORE INTO mensajes (id_wa, chat, remitente, propio, ts, tipo, texto, cita_id, cita_remitente, cita_texto, media_id, editado, borrado, estado, reenviado)
		VALUES (?, ?, ?, ?, ?, ?, ?, '', '', '', ?, 0, ?, ?, 0)`)
	var enTx int
	var ultimoTS int64
	commit := func() {
		insMsg.Close()
		tx.Commit()
		tx, _ = db.Begin()
		insMsg, _ = tx.Prepare(`INSERT IGNORE INTO mensajes (id_wa, chat, remitente, propio, ts, tipo, texto, cita_id, cita_remitente, cita_texto, media_id, editado, borrado, estado, reenviado)
			VALUES (?, ?, ?, ?, ?, ?, ?, '', '', '', ?, 0, ?, ?, 0)`)
		enTx = 0
	}

	for filas.Next() {
		var m msgBackup
		if err := filas.Scan(&m.pk, &m.stanza, &m.fromJID, &m.fromMe, &m.tipo, &m.fecha, &m.texto, &m.miembro, &m.estado, &m.eventoTipo,
			&m.mediaPK, &m.ruta, &m.mime, &m.titulo, &m.vcardNom, &m.segundos, &m.lat, &m.lon, &m.bytes); err != nil {
			log.Printf("fila: %v", err)
			continue
		}
		tipo, texto, borrado := tipoDeBackup(&m)
		if tipo == "" || m.stanza == "" {
			continue
		}
		ts := int64((m.fecha + epochCoreData) * 1000)
		propio := m.fromMe == 1
		conArchivo := m.ruta.Valid && m.ruta.String != ""
		var archivo string
		if conArchivo {
			archivo = filepath.Join(carpeta, "Message", filepath.FromSlash(m.ruta.String))
			if _, err := os.Stat(archivo); err != nil {
				conArchivo = false
			}
		}
		esMedia := tipo == "imagen" || tipo == "video" || tipo == "gif" || tipo == "nota" || tipo == "audio" || tipo == "documento" || tipo == "figurita"

		// Ya esta (por id, o por fecha+propio+texto si es de los imp-N)?
		var ex *existente
		if e, ok := porID[m.stanza]; ok {
			ex = e
		} else if hayImp {
			for d := int64(-2); d <= 2 && ex == nil; d++ {
				for _, e := range porSegundo[ts/1000+d] {
					if e.propio != propio {
						continue
					}
					if esMedia {
						if e.tipo == tipo || (e.tipo == "audio" && tipo == "nota") || (e.tipo == "nota" && tipo == "audio") {
							ex = e
							break
						}
					} else if strings.TrimSpace(e.texto) == strings.TrimSpace(texto) {
						ex = e
						break
					}
				}
			}
		}
		if ex != nil {
			iguales++
			if esMedia && conArchivo {
				if !ex.media {
					if mid, ok := guardarMediaBackup(chat, ex.id, &m, tipo, archivo); ok {
						db.Exec("UPDATE mensajes SET media_id = ? WHERE chat = ? AND id_wa = ?", mid, chat, ex.id)
						ex.media, ex.mediaBajada = true, true
						pegados++
						idsMedia = append(idsMedia, mid)
					}
				} else if !ex.mediaBajada {
					// Tenia la fila pero nunca se bajo (o vencio): el archivo del backup la completa.
					if rellenarMediaBackup(ex.mediaID, &m, tipo, archivo) {
						ex.mediaBajada = true
						pegados++
						idsMedia = append(idsMedia, ex.mediaID)
					}
				}
			}
			continue
		}

		// Nuevo.
		remitente := chat
		if propio {
			remitente = yo
		} else if grupo {
			if m.miembro.Valid {
				if j, ok := miembros[m.miembro.Int64]; ok && j != "" {
					remitente = j
				}
			}
			if remitente == chat && m.fromJID.Valid && !strings.HasSuffix(m.fromJID.String, "@g.us") {
				remitente = norm(m.fromJID.String)
			}
		}
		estado := 0
		if propio {
			switch {
			case m.estado >= 8:
				estado = 3
			case m.estado >= 6:
				estado = 2
			default:
				estado = 1
			}
		}
		var mediaID any
		if esMedia {
			if conArchivo {
				if mid, ok := guardarMediaBackup(chat, m.stanza, &m, tipo, archivo); ok {
					mediaID = mid
					mediaNueva++
					idsMedia = append(idsMedia, mid)
				}
			} else {
				// Sin archivo: queda como vencido, con lo que se sabe.
				mime, nombre, seg := datosMediaBackup(&m, tipo)
				res, err := db.Exec(`INSERT INTO media (chat, mensaje, mime, nombre, bytes, segundos, estado) VALUES (?, ?, ?, ?, ?, ?, 2)`,
					chat, m.stanza, mime, nombre, m.bytes.Int64, seg)
				if err == nil {
					mediaID, _ = res.LastInsertId()
				}
			}
		}
		if _, err := insMsg.Exec(m.stanza, chat, remitente, propio, ts, tipo, texto, mediaID, borrado, estado); err != nil {
			log.Printf("insert %s: %v", m.stanza, err)
			continue
		}
		nuevos++
		if ts > ultimoTS {
			ultimoTS = ts
		}
		porID[m.stanza] = &existente{id: m.stanza, ts: ts, propio: propio, tipo: tipo, texto: texto, media: mediaID != nil}
		enTx++
		if enTx >= 2000 {
			commit()
		}
	}
	insMsg.Close()
	tx.Commit()
	if ultimoTS > 0 {
		db.Exec("UPDATE chats SET ultimo_ts = GREATEST(ultimo_ts, ?) WHERE jid = ?", ultimoTS, chat)
	}
	return
}

func datosMediaBackup(m *msgBackup, tipo string) (mime, nombre string, seg int) {
	if m.mime.Valid {
		mime = m.mime.String
	}
	if tipo == "documento" && m.titulo.Valid {
		nombre = m.titulo.String
	}
	if m.segundos.Valid {
		seg = int(m.segundos.Float64 + 0.5)
	}
	if mime == "" {
		switch tipo {
		case "imagen":
			mime = "image/jpeg"
		case "video", "gif":
			mime = "video/mp4"
		case "nota":
			mime = "audio/ogg; codecs=opus"
		case "audio":
			mime = "audio/mpeg"
		case "figurita":
			mime = "image/webp"
		default:
			mime = "application/octet-stream"
		}
	}
	return
}

// Copia el archivo del backup a nuestra carpeta de media y crea la fila.
func guardarMediaBackup(chat, msgID string, m *msgBackup, tipo, archivo string) (int64, bool) {
	mime, nombre, seg := datosMediaBackup(m, tipo)
	info, err := os.Stat(archivo)
	if err != nil {
		return 0, false
	}
	// Para que rutaPara elija bien la extension.
	nombreRuta := nombre
	if nombreRuta == "" {
		nombreRuta = filepath.Base(archivo)
	}
	res, err := db.Exec(`INSERT INTO media (chat, mensaje, mime, nombre, bytes, segundos, estado) VALUES (?, ?, ?, ?, ?, ?, 1)`,
		chat, msgID, mime, nombre, info.Size(), seg)
	if err != nil {
		log.Printf("media %s: %v", msgID, err)
		return 0, false
	}
	mid, _ := res.LastInsertId()
	destino := rutaPara(mid, mime, nombreRuta)
	os.MkdirAll(filepath.Dir(destino), 0o755)
	if err := os.Link(archivo, destino); err != nil {
		datos, err := os.ReadFile(archivo)
		if err != nil || os.WriteFile(destino, datos, 0o644) != nil {
			db.Exec("DELETE FROM media WHERE id = ?", mid)
			return 0, false
		}
	}
	db.Exec("UPDATE media SET ruta = ? WHERE id = ?", destino, mid)
	return mid, true
}

// Completa una fila de media que existia sin archivo (pendiente o vencida).
func rellenarMediaBackup(mid int64, m *msgBackup, tipo, archivo string) bool {
	mime, nombre, _ := datosMediaBackup(m, tipo)
	info, err := os.Stat(archivo)
	if err != nil {
		return false
	}
	var mimeViejo, nombreViejo string
	db.QueryRow("SELECT mime, nombre FROM media WHERE id = ?", mid).Scan(&mimeViejo, &nombreViejo)
	if mimeViejo != "" {
		mime = mimeViejo
	}
	if nombreViejo != "" {
		nombre = nombreViejo
	}
	nombreRuta := nombre
	if nombreRuta == "" {
		nombreRuta = filepath.Base(archivo)
	}
	destino := rutaPara(mid, mime, nombreRuta)
	os.MkdirAll(filepath.Dir(destino), 0o755)
	os.Remove(destino)
	if err := os.Link(archivo, destino); err != nil {
		datos, err := os.ReadFile(archivo)
		if err != nil || os.WriteFile(destino, datos, 0o644) != nil {
			return false
		}
	}
	db.Exec("UPDATE media SET ruta = ?, estado = 1, bytes = ?, intentos = 0 WHERE id = ?", destino, info.Size(), mid)
	return true
}
