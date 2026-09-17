package main

// El core del cliente "solo": la misma base pero en SQLite (modernc, sin
// cgo), en portable\datos. El SQL del server esta escrito para MariaDB;
// aca se traduce lo poco que difiere antes de ejecutarlo:
//   INSERT IGNORE            -> INSERT OR IGNORE
//   ON DUPLICATE KEY UPDATE  -> ON CONFLICT DO UPDATE SET, VALUES(x) -> excluded.x
//   GREATEST(                -> MAX(
//   IF(                      -> IIF(

import (
	"context"
	"database/sql"
	"regexp"
	"strings"
)

type baseDatos struct {
	*sql.DB
	sqlite bool
}

var reValues = regexp.MustCompile(`VALUES\((\w+)\)`)
var reIf = regexp.MustCompile(`\bIF\(`)

func (b *baseDatos) traducir(q string) string {
	if !b.sqlite {
		return q
	}
	if strings.Contains(q, "ON DUPLICATE KEY UPDATE") {
		q = strings.Replace(q, "ON DUPLICATE KEY UPDATE", "ON CONFLICT DO UPDATE SET", 1)
		q = reValues.ReplaceAllString(q, "excluded.$1")
	}
	q = strings.ReplaceAll(q, "INSERT IGNORE", "INSERT OR IGNORE")
	q = strings.ReplaceAll(q, "GREATEST(", "MAX(")
	q = reIf.ReplaceAllString(q, "IIF(")
	// Las busquedas escapan % y _ con \ (MariaDB lo entiende solo).
	if strings.Contains(q, " LIKE ?") {
		q = strings.ReplaceAll(q, " LIKE ?", ` LIKE ? ESCAPE '\'`)
	}
	return q
}

func (b *baseDatos) Exec(q string, args ...any) (sql.Result, error) { return b.DB.Exec(b.traducir(q), args...) }
func (b *baseDatos) Query(q string, args ...any) (*sql.Rows, error) { return b.DB.Query(b.traducir(q), args...) }
func (b *baseDatos) QueryRow(q string, args ...any) *sql.Row       { return b.DB.QueryRow(b.traducir(q), args...) }
func (b *baseDatos) Prepare(q string) (*sql.Stmt, error)            { return b.DB.Prepare(b.traducir(q)) }
func (b *baseDatos) ExecContext(ctx context.Context, q string, args ...any) (sql.Result, error) {
	return b.DB.ExecContext(ctx, b.traducir(q), args...)
}

// El esquema en SQLite (el de MariaDB esta en base.go).
var esquemaSQLite = []string{
	`CREATE TABLE IF NOT EXISTS chats (
		jid        TEXT PRIMARY KEY,
		nombre     TEXT NOT NULL DEFAULT '',
		es_grupo   INTEGER NOT NULL DEFAULT 0,
		foto       TEXT NOT NULL DEFAULT '',
		ultimo_ts  INTEGER NOT NULL DEFAULT 0,
		no_leidos  INTEGER NOT NULL DEFAULT 0,
		archivado  INTEGER NOT NULL DEFAULT 0
	)`,
	`CREATE INDEX IF NOT EXISTS ix_chats_ultimo ON chats(ultimo_ts)`,
	`CREATE TABLE IF NOT EXISTS contactos (
		jid           TEXT PRIMARY KEY,
		lid           TEXT NOT NULL DEFAULT '',
		telefono      TEXT NOT NULL DEFAULT '',
		nombre_push   TEXT NOT NULL DEFAULT '',
		nombre_agenda TEXT NOT NULL DEFAULT '',
		foto          TEXT NOT NULL DEFAULT ''
	)`,
	`CREATE INDEX IF NOT EXISTS ix_contactos_lid ON contactos(lid)`,
	`CREATE TABLE IF NOT EXISTS miembros (
		chat   TEXT NOT NULL,
		jid    TEXT NOT NULL,
		admin  INTEGER NOT NULL DEFAULT 0,
		PRIMARY KEY (chat, jid)
	)`,
	`CREATE TABLE IF NOT EXISTS mensajes (
		id_wa          TEXT NOT NULL,
		chat           TEXT NOT NULL,
		remitente      TEXT NOT NULL,
		propio         INTEGER NOT NULL DEFAULT 0,
		ts             INTEGER NOT NULL,
		tipo           TEXT NOT NULL,
		texto          TEXT NOT NULL,
		cita_id        TEXT NOT NULL DEFAULT '',
		cita_remitente TEXT NOT NULL DEFAULT '',
		cita_texto     TEXT NOT NULL,
		media_id       INTEGER NULL,
		editado        INTEGER NOT NULL DEFAULT 0,
		borrado        INTEGER NOT NULL DEFAULT 0,
		estado         INTEGER NOT NULL DEFAULT 0,
		reenviado      INTEGER NOT NULL DEFAULT 0,
		PRIMARY KEY (chat, id_wa)
	)`,
	`CREATE INDEX IF NOT EXISTS ix_mensajes_chat_ts ON mensajes(chat, ts)`,
	`CREATE INDEX IF NOT EXISTS ix_mensajes_ts ON mensajes(ts)`,
	`CREATE TABLE IF NOT EXISTS reacciones (
		chat      TEXT NOT NULL,
		mensaje   TEXT NOT NULL,
		remitente TEXT NOT NULL,
		emoji     TEXT NOT NULL,
		ts        INTEGER NOT NULL,
		PRIMARY KEY (chat, mensaje, remitente)
	)`,
	`CREATE TABLE IF NOT EXISTS media (
		id        INTEGER PRIMARY KEY AUTOINCREMENT,
		chat      TEXT NOT NULL,
		mensaje   TEXT NOT NULL,
		mime      TEXT NOT NULL DEFAULT '',
		nombre    TEXT NOT NULL DEFAULT '',
		bytes     INTEGER NOT NULL DEFAULT 0,
		ancho     INTEGER NOT NULL DEFAULT 0,
		alto      INTEGER NOT NULL DEFAULT 0,
		segundos  INTEGER NOT NULL DEFAULT 0,
		ruta      TEXT NOT NULL DEFAULT '',
		estado    INTEGER NOT NULL DEFAULT 0,
		intentos  INTEGER NOT NULL DEFAULT 0,
		miniatura BLOB NULL,
		proto     BLOB NULL,
		onda      BLOB NULL
	)`,
	`CREATE INDEX IF NOT EXISTS ix_media_msg ON media(chat, mensaje)`,
	`CREATE INDEX IF NOT EXISTS ix_media_estado ON media(estado)`,
	`CREATE TABLE IF NOT EXISTS eventos (
		seq   INTEGER PRIMARY KEY AUTOINCREMENT,
		ts    INTEGER NOT NULL,
		tipo  TEXT NOT NULL,
		chat  TEXT NOT NULL DEFAULT '',
		datos TEXT NOT NULL
	)`,
	`CREATE TABLE IF NOT EXISTS botones (
		chat    TEXT NOT NULL,
		mensaje TEXT NOT NULL,
		datos   TEXT NOT NULL,
		proto   BLOB NULL,
		PRIMARY KEY (chat, mensaje)
	)`,
	`CREATE TABLE IF NOT EXISTS valores (
		clave TEXT PRIMARY KEY,
		valor TEXT NOT NULL
	)`,
}
