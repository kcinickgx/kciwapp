package main

import (
	"log"
	"os"
	"os/exec"
	"path/filepath"
)

// Miniatura JPEG de un video con ffmpeg (los videos de la historia no traen
// la suya). Se guarda en media.miniatura y se avisa al cliente.
func miniaturaDeVideo(ruta string) []byte {
	dir, err := os.MkdirTemp("", "mini")
	if err != nil {
		return nil
	}
	defer os.RemoveAll(dir)
	salida := filepath.Join(dir, "mini.jpg")
	cmd := exec.Command("ffmpeg", "-y", "-loglevel", "error", "-ss", "0.5", "-i", ruta, "-frames:v", "1",
		"-vf", "scale=320:-2", "-q:v", "6", salida)
	if err := cmd.Run(); err != nil {
		// Videos de menos de medio segundo: sin el -ss.
		cmd = exec.Command("ffmpeg", "-y", "-loglevel", "error", "-i", ruta, "-frames:v", "1", "-vf", "scale=320:-2", "-q:v", "6", salida)
		if err := cmd.Run(); err != nil {
			return nil
		}
	}
	datos, err := os.ReadFile(salida)
	if err != nil {
		return nil
	}
	return datos
}

// Le pone miniatura a un media de video recien bajado si no la tenia.
func asegurarMiniatura(id int64, chat, msgID, tipo, ruta string) {
	if tipo != "video" && tipo != "gif" {
		return
	}
	var tiene bool
	if db.QueryRow("SELECT miniatura IS NOT NULL FROM media WHERE id = ?", id).Scan(&tiene) != nil || tiene {
		return
	}
	mini := miniaturaDeVideo(ruta)
	if mini == nil {
		return
	}
	db.Exec("UPDATE media SET miniatura = ? WHERE id = ?", mini, id)
	evento("media", chat, map[string]any{"id": id, "mensaje": msgID, "estado": 1, "miniatura": true})
}

// Una sola vez al arrancar: los videos ya bajados sin miniatura.
func rellenarMiniaturas() {
	filas, err := db.Query(`SELECT md.id, md.chat, md.mensaje, m.tipo, md.ruta FROM media md JOIN mensajes m ON m.chat = md.chat AND m.id_wa = md.mensaje
		WHERE md.estado = 1 AND md.miniatura IS NULL AND m.tipo IN ('video', 'gif') AND md.ruta <> ''`)
	if err != nil {
		return
	}
	type fila struct{ id int64; chat, msg, tipo, ruta string }
	var lista []fila
	for filas.Next() {
		var f fila
		if filas.Scan(&f.id, &f.chat, &f.msg, &f.tipo, &f.ruta) == nil {
			lista = append(lista, f)
		}
	}
	filas.Close()
	for _, f := range lista {
		asegurarMiniatura(f.id, f.chat, f.msg, f.tipo, f.ruta)
	}
	if len(lista) > 0 {
		log.Printf("miniaturas de video generadas: %d", len(lista))
	}
}
