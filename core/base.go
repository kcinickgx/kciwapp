package main

import (
	"database/sql"
	"encoding/json"
	"log"
	"sync"
	"time"
)

// Las tablas nuestras en MariaDB. El store de whatsmeow (sesion, claves,
// contactos crudos) va aparte, en SQLite.
var esquema = []string{
	`CREATE TABLE IF NOT EXISTS chats (
		jid        VARCHAR(64) PRIMARY KEY,
		nombre     VARCHAR(255) NOT NULL DEFAULT '',
		es_grupo   TINYINT NOT NULL DEFAULT 0,
		foto       VARCHAR(255) NOT NULL DEFAULT '',
		ultimo_ts  BIGINT NOT NULL DEFAULT 0,
		no_leidos  INT NOT NULL DEFAULT 0,
		archivado  TINYINT NOT NULL DEFAULT 0,
		INDEX (ultimo_ts)
	)`,
	`CREATE TABLE IF NOT EXISTS contactos (
		jid           VARCHAR(64) PRIMARY KEY,
		lid           VARCHAR(64) NOT NULL DEFAULT '',
		telefono      VARCHAR(32) NOT NULL DEFAULT '',
		nombre_push   VARCHAR(255) NOT NULL DEFAULT '',
		nombre_agenda VARCHAR(255) NOT NULL DEFAULT '',
		foto          VARCHAR(255) NOT NULL DEFAULT '',
		INDEX (lid)
	)`,
	`CREATE TABLE IF NOT EXISTS miembros (
		chat   VARCHAR(64) NOT NULL,
		jid    VARCHAR(64) NOT NULL,
		admin  TINYINT NOT NULL DEFAULT 0,
		PRIMARY KEY (chat, jid)
	)`,
	`CREATE TABLE IF NOT EXISTS mensajes (
		id_wa          VARCHAR(64) NOT NULL,
		chat           VARCHAR(64) NOT NULL,
		remitente      VARCHAR(64) NOT NULL,
		propio         TINYINT NOT NULL DEFAULT 0,
		ts             BIGINT NOT NULL,
		tipo           VARCHAR(16) NOT NULL,
		texto          TEXT NOT NULL,
		cita_id        VARCHAR(64) NOT NULL DEFAULT '',
		cita_remitente VARCHAR(64) NOT NULL DEFAULT '',
		cita_texto     TEXT NOT NULL,
		media_id       BIGINT NULL,
		editado        TINYINT NOT NULL DEFAULT 0,
		borrado        TINYINT NOT NULL DEFAULT 0,
		estado         TINYINT NOT NULL DEFAULT 0,
		reenviado      TINYINT NOT NULL DEFAULT 0,
		PRIMARY KEY (chat, id_wa),
		INDEX (chat, ts),
		INDEX (ts)
	)`,
	`CREATE TABLE IF NOT EXISTS reacciones (
		chat      VARCHAR(64) NOT NULL,
		mensaje   VARCHAR(64) NOT NULL,
		remitente VARCHAR(64) NOT NULL,
		emoji     VARCHAR(32) NOT NULL,
		ts        BIGINT NOT NULL,
		PRIMARY KEY (chat, mensaje, remitente)
	)`,
	// Quien recibio/leyo/escucho cada mensaje mio (el "Message info" del
	// telefono): en grupos y estados llega uno por participante.
	`CREATE TABLE IF NOT EXISTS acuses (
		chat         VARCHAR(64) NOT NULL,
		mensaje      VARCHAR(64) NOT NULL,
		participante VARCHAR(64) NOT NULL,
		estado       TINYINT NOT NULL,
		ts           BIGINT NOT NULL,
		PRIMARY KEY (chat, mensaje, participante)
	)`,
	`CREATE TABLE IF NOT EXISTS media (
		id        BIGINT AUTO_INCREMENT PRIMARY KEY,
		chat      VARCHAR(64) NOT NULL,
		mensaje   VARCHAR(64) NOT NULL,
		mime      VARCHAR(128) NOT NULL DEFAULT '',
		nombre    VARCHAR(255) NOT NULL DEFAULT '',
		bytes     BIGINT NOT NULL DEFAULT 0,
		ancho     INT NOT NULL DEFAULT 0,
		alto      INT NOT NULL DEFAULT 0,
		segundos  INT NOT NULL DEFAULT 0,
		ruta      VARCHAR(255) NOT NULL DEFAULT '',
		estado    TINYINT NOT NULL DEFAULT 0,
		intentos  INT NOT NULL DEFAULT 0,
		miniatura MEDIUMBLOB NULL,
		proto     BLOB NULL,
		onda      BLOB NULL,
		INDEX (chat, mensaje),
		INDEX (estado)
	)`,
	`ALTER TABLE media ADD COLUMN IF NOT EXISTS onda BLOB NULL`,
	`CREATE TABLE IF NOT EXISTS eventos (
		seq   BIGINT AUTO_INCREMENT PRIMARY KEY,
		ts    BIGINT NOT NULL,
		tipo  VARCHAR(16) NOT NULL,
		chat  VARCHAR(64) NOT NULL DEFAULT '',
		datos MEDIUMTEXT NOT NULL
	)`,
	`CREATE TABLE IF NOT EXISTS botones (
		chat    VARCHAR(64) NOT NULL,
		mensaje VARCHAR(64) NOT NULL,
		datos   MEDIUMTEXT NOT NULL,
		proto   MEDIUMBLOB NULL,
		PRIMARY KEY (chat, mensaje)
	)`,
	`CREATE TABLE IF NOT EXISTS valores (
		clave VARCHAR(64) PRIMARY KEY,
		valor TEXT NOT NULL
	)`,
}

func crearTablas() {
	lista := esquema
	if db.sqlite {
		lista = esquemaSQLite
	}
	for _, s := range lista {
		if _, err := db.Exec(s); err != nil {
			log.Fatalf("esquema: %v\n%s", err, s)
		}
	}
}

// ---- lo que ve el cliente -----------------------------------------------

type Chat struct {
	JID       string   `json:"jid"`
	Nombre    string   `json:"nombre"`
	EsGrupo   bool     `json:"es_grupo"`
	Foto      string   `json:"foto,omitempty"`
	UltimoTS  int64    `json:"ultimo_ts"`
	NoLeidos  int      `json:"no_leidos"`
	Archivado bool     `json:"archivado"`
	Ultimo    *Mensaje `json:"ultimo,omitempty"`
}

type Contacto struct {
	JID          string `json:"jid"`
	LID          string `json:"lid,omitempty"`
	Telefono     string `json:"telefono"`
	NombrePush   string `json:"nombre_push"`
	NombreAgenda string `json:"nombre_agenda"`
	Foto         string `json:"foto,omitempty"`
}

type Media struct {
	ID       int64  `json:"id"`
	Mime     string `json:"mime"`
	Nombre   string `json:"nombre,omitempty"`
	Bytes    int64  `json:"bytes"`
	Ancho    int    `json:"ancho,omitempty"`
	Alto     int    `json:"alto,omitempty"`
	Segundos int    `json:"segundos,omitempty"`
	// 0 pendiente, 1 bajado, 2 fallo (vencido en WhatsApp)
	Estado int `json:"estado"`
	// Si hay miniatura JPEG para pedir en /miniatura/{id}.
	Miniatura bool `json:"miniatura"`
	// Forma de onda (64 valores 0..100) de las notas de voz.
	Onda []int `json:"onda,omitempty"`
}

type Reaccion struct {
	Remitente string `json:"remitente"`
	Emoji     string `json:"emoji"`
	TS        int64  `json:"ts"`
}

type Mensaje struct {
	ID            string     `json:"id"`
	Chat          string     `json:"chat"`
	Remitente     string     `json:"remitente"`
	Propio        bool       `json:"propio"`
	TS            int64      `json:"ts"`
	Tipo          string     `json:"tipo"`
	Texto         string     `json:"texto"`
	CitaID        string     `json:"cita_id,omitempty"`
	CitaRemitente string     `json:"cita_remitente,omitempty"`
	CitaTexto     string     `json:"cita_texto,omitempty"`
	Editado       bool       `json:"editado"`
	Borrado       bool       `json:"borrado"`
	Estado        int        `json:"estado"`
	Reenviado     bool       `json:"reenviado"`
	Media         *Media     `json:"media,omitempty"`
	Reacciones    []Reaccion `json:"reacciones,omitempty"`
	Botones       *Botones   `json:"botones,omitempty"`
}

const columnasMensaje = `m.id_wa, m.chat, m.remitente, m.propio, m.ts, m.tipo, m.texto,
	m.cita_id, m.cita_remitente, m.cita_texto, m.editado, m.borrado, m.estado, m.reenviado,
	md.id, md.mime, md.nombre, md.bytes, md.ancho, md.alto, md.segundos, md.estado, md.miniatura IS NOT NULL, md.onda`

const desdeMensajes = `FROM mensajes m LEFT JOIN media md ON md.id = m.media_id`

type escaneable interface {
	Scan(dest ...any) error
}

func leerMensaje(f escaneable) (*Mensaje, error) {
	var m Mensaje
	var md Media
	var mdID sql.NullInt64
	var mdMime, mdNombre sql.NullString
	var mdBytes sql.NullInt64
	var mdAncho, mdAlto, mdSeg, mdEstado sql.NullInt64
	var mdMini sql.NullBool
	var mdOnda []byte
	err := f.Scan(&m.ID, &m.Chat, &m.Remitente, &m.Propio, &m.TS, &m.Tipo, &m.Texto,
		&m.CitaID, &m.CitaRemitente, &m.CitaTexto, &m.Editado, &m.Borrado, &m.Estado, &m.Reenviado,
		&mdID, &mdMime, &mdNombre, &mdBytes, &mdAncho, &mdAlto, &mdSeg, &mdEstado, &mdMini, &mdOnda)
	if err != nil {
		return nil, err
	}
	if mdID.Valid {
		md.ID = mdID.Int64
		md.Mime = mdMime.String
		md.Nombre = mdNombre.String
		md.Bytes = mdBytes.Int64
		md.Ancho = int(mdAncho.Int64)
		md.Alto = int(mdAlto.Int64)
		md.Segundos = int(mdSeg.Int64)
		md.Estado = int(mdEstado.Int64)
		md.Miniatura = mdMini.Bool
		for _, b := range mdOnda {
			md.Onda = append(md.Onda, int(b))
		}
		m.Media = &md
	}
	return &m, nil
}

func mensajePorID(chat, id string) *Mensaje {
	f := db.QueryRow("SELECT "+columnasMensaje+" "+desdeMensajes+" WHERE m.chat = ? AND m.id_wa = ?", chat, id)
	m, err := leerMensaje(f)
	if err != nil {
		return nil
	}
	m.Reacciones = reaccionesDe(chat, []string{id})[id]
	m.Botones = botonesDe(chat, []string{id})[id]
	return m
}

func reaccionesDe(chat string, ids []string) map[string][]Reaccion {
	res := map[string][]Reaccion{}
	if len(ids) == 0 {
		return res
	}
	args := make([]any, 0, len(ids)+1)
	args = append(args, chat)
	marcas := ""
	for i, id := range ids {
		if i > 0 {
			marcas += ","
		}
		marcas += "?"
		args = append(args, id)
	}
	filas, err := db.Query("SELECT mensaje, remitente, emoji, ts FROM reacciones WHERE chat = ? AND mensaje IN ("+marcas+") ORDER BY ts", args...)
	if err != nil {
		return res
	}
	defer filas.Close()
	for filas.Next() {
		var id string
		var r Reaccion
		if filas.Scan(&id, &r.Remitente, &r.Emoji, &r.TS) == nil {
			res[id] = append(res[id], r)
		}
	}
	return res
}

// ---- el log de eventos ---------------------------------------------------

var (
	avisoMu sync.Mutex
	avisoCh = make(chan struct{})
)

// Anota un evento en el log y despierta a los long-polls.
func evento(tipo, chat string, datos any) {
	crudo, err := json.Marshal(datos)
	if err != nil {
		return
	}
	if _, err := db.Exec("INSERT INTO eventos (ts, tipo, chat, datos) VALUES (?, ?, ?, ?)",
		ahoraMs(), tipo, chat, string(crudo)); err != nil {
		log.Printf("evento: %v", err)
		return
	}
	if tipo == "escribiendo" {
		// No vale la pena guardarlos: a los dos minutos se van.
		db.Exec("DELETE FROM eventos WHERE tipo = 'escribiendo' AND ts < ?", ahoraMs()-120000)
	}
	avisoMu.Lock()
	close(avisoCh)
	avisoCh = make(chan struct{})
	avisoMu.Unlock()
}

func esperarEvento() <-chan struct{} {
	avisoMu.Lock()
	defer avisoMu.Unlock()
	return avisoCh
}

func ahoraMs() int64 {
	return time.Now().UnixMilli()
}

func valor(clave string) string {
	var v string
	db.QueryRow("SELECT valor FROM valores WHERE clave = ?", clave).Scan(&v)
	return v
}

func guardarValor(clave, v string) {
	db.Exec("INSERT INTO valores (clave, valor) VALUES (?, ?) ON DUPLICATE KEY UPDATE valor = VALUES(valor)", clave, v)
}
