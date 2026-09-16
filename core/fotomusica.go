package main

// Los estados "foto con musica" de WhatsApp llegan como video: un mp4 de
// 15 segundos con un puñado de frames iguales y la cancion de audio. El
// telefono lo muestra como una foto; aca tambien: se saca el frame como
// JPEG, el media pasa a imagen y el mensaje a tipo "imagen".

import (
	"log"
	"os"
	"os/exec"
	"strconv"
	"strings"
)

// Es un video quieto? (10 frames o menos, y de por lo menos 3 segundos)
func videoQuieto(ruta string) bool {
	out, err := exec.Command("ffprobe", "-v", "error", "-select_streams", "v:0", "-count_packets",
		"-show_entries", "stream=nb_read_packets", "-show_entries", "format=duration", "-of", "default=noprint_wrappers=1", ruta).Output()
	if err != nil {
		return false
	}
	frames, dur := -1, 0.0
	for _, l := range strings.Split(string(out), "\n") {
		k, v, ok := strings.Cut(strings.TrimSpace(l), "=")
		if !ok {
			continue
		}
		switch k {
		case "nb_read_packets":
			frames, _ = strconv.Atoi(v)
		case "duration":
			dur, _ = strconv.ParseFloat(v, 64)
		}
	}
	return frames > 0 && frames <= 10 && dur >= 3
}

// Convierte un video quieto recien bajado en una imagen. Devuelve true si
// lo hizo (el media ya no es video).
func convertirFotoConMusica(id int64, chat, msgID, ruta string) bool {
	if !videoQuieto(ruta) {
		return false
	}
	jpg := strings.TrimSuffix(ruta, ".mp4") + ".jpg"
	if err := exec.Command("ffmpeg", "-y", "-loglevel", "error", "-i", ruta, "-frames:v", "1", "-q:v", "2", jpg).Run(); err != nil {
		log.Printf("media %d: foto con musica, ffmpeg fallo: %v", id, err)
		return false
	}
	info, err := os.Stat(jpg)
	if err != nil {
		return false
	}
	mini := miniaturaDeVideo(jpg)
	if mini != nil {
		db.Exec("UPDATE media SET mime = 'image/jpeg', ruta = ?, segundos = 0, bytes = ?, miniatura = ? WHERE id = ?", jpg, info.Size(), mini, id)
	} else {
		db.Exec("UPDATE media SET mime = 'image/jpeg', ruta = ?, segundos = 0, bytes = ? WHERE id = ?", jpg, info.Size(), id)
	}
	db.Exec("UPDATE mensajes SET tipo = 'imagen' WHERE chat = ? AND id_wa = ?", chat, msgID)
	os.Remove(ruta)
	log.Printf("media %d: foto con musica, queda como imagen", id)
	evento("media", chat, map[string]any{"id": id, "mensaje": msgID, "estado": 1, "miniatura": mini != nil, "tipo": "imagen", "mime": "image/jpeg"})
	return true
}

// Una sola vez al arrancar: los videos ya bajados que en realidad son fotos.
func rellenarFotosConMusica() {
	filas, err := db.Query(`SELECT md.id, md.chat, md.mensaje, md.ruta FROM media md JOIN mensajes m ON m.chat = md.chat AND m.id_wa = md.mensaje
		WHERE md.estado = 1 AND m.tipo = 'video' AND md.ruta LIKE '%.mp4' AND md.segundos >= 3`)
	if err != nil {
		return
	}
	type fila struct {
		id                int64
		chat, msg, ruta string
	}
	var lista []fila
	for filas.Next() {
		var f fila
		if filas.Scan(&f.id, &f.chat, &f.msg, &f.ruta) == nil {
			lista = append(lista, f)
		}
	}
	filas.Close()
	n := 0
	for _, f := range lista {
		if convertirFotoConMusica(f.id, f.chat, f.msg, f.ruta) {
			n++
		}
	}
	if n > 0 {
		log.Printf("fotos con musica convertidas: %d de %d videos", n, len(lista))
	}
}
