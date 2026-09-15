package main

import (
	"net/http"
	"os"
	"os/exec"
	"path/filepath"
	"strconv"
	"strings"
)

// GET /buscar?q=texto&chat=<opcional>&limite=50: mensajes cuyo texto
// contiene `q`. La collation utf8mb4_unicode_ci ya ignora acentos y
// mayusculas, asi que LIKE alcanza. Del mas nuevo al mas viejo.
func hBuscar(w http.ResponseWriter, r *http.Request) {
	q := strings.TrimSpace(r.URL.Query().Get("q"))
	chat := r.URL.Query().Get("chat")
	limite, _ := strconv.Atoi(r.URL.Query().Get("limite"))
	if limite <= 0 || limite > 1000000 {
		limite = 50
	}
	// Paginado: solo mensajes anteriores a este ts (el ultimo que ya se tiene).
	antes, _ := strconv.ParseInt(r.URL.Query().Get("antes"), 10, 64)
	if antes <= 0 {
		antes = 1 << 62
	}
	if q == "" {
		responder(w, []*Mensaje{})
		return
	}
	patron := "%" + strings.NewReplacer("%", "\\%", "_", "\\_").Replace(q) + "%"
	var filas interface {
		Next() bool
		Scan(...any) error
		Close() error
	}
	var err error
	if chat != "" {
		filas, err = db.Query("SELECT "+columnasMensaje+" "+desdeMensajes+" WHERE m.chat = ? AND m.texto LIKE ? AND m.borrado = 0 AND m.ts < ? ORDER BY m.ts DESC LIMIT ?", chat, patron, antes, limite)
	} else {
		filas, err = db.Query("SELECT "+columnasMensaje+" "+desdeMensajes+" WHERE m.texto LIKE ? AND m.borrado = 0 AND m.ts < ? ORDER BY m.ts DESC LIMIT ?", patron, antes, limite)
	}
	if err != nil {
		fallar(w, 500, err)
		return
	}
	lista := []*Mensaje{}
	for filas.Next() {
		if m, err := leerMensaje(filas); err == nil {
			lista = append(lista, m)
		}
	}
	filas.Close()
	responder(w, lista)
}

// Convierte cualquier audio a Ogg/Opus mono 16 kHz (lo que WhatsApp espera
// para una nota de voz) con ffmpeg. Devuelve los bytes nuevos y la duracion.
func aOggOpus(datos []byte) ([]byte, int, error) {
	dir, err := os.MkdirTemp("", "nota")
	if err != nil {
		return nil, 0, err
	}
	defer os.RemoveAll(dir)
	entrada := filepath.Join(dir, "entrada")
	salida := filepath.Join(dir, "salida.ogg")
	if err := os.WriteFile(entrada, datos, 0o600); err != nil {
		return nil, 0, err
	}
	cmd := exec.Command("ffmpeg", "-y", "-loglevel", "error", "-i", entrada, "-ac", "1", "-ar", "16000",
		"-c:a", "libopus", "-b:a", "24k", "-application", "voip", "-vbr", "on", salida)
	if out, err := cmd.CombinedOutput(); err != nil {
		return nil, 0, &errorTexto{"ffmpeg: " + string(out)}
	}
	res, err := os.ReadFile(salida)
	if err != nil {
		return nil, 0, err
	}
	// La duracion, con ffprobe.
	seg := 0
	if out, err := exec.Command("ffprobe", "-v", "error", "-show_entries", "format=duration", "-of",
		"default=noprint_wrappers=1:nokey=1", salida).Output(); err == nil {
		if f, err := strconv.ParseFloat(strings.TrimSpace(string(out)), 64); err == nil {
			seg = int(f + 0.5)
		}
	}
	return res, seg, nil
}

type errorTexto struct{ s string }

func (e *errorTexto) Error() string { return e.s }
