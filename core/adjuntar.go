package main

import (
	"log"
	"os"
	"path/filepath"
	"regexp"
	"strings"
	"time"
)

// Segunda pasada del export: los archivos que quedaron en la carpeta son
// media de mensajes que el server ya tenia (por el history sync) pero que
// WhatsApp ya no entrega (vencidos). Se emparejan por tipo + fecha del
// nombre (o el mtime del archivo, para documentos/vcards) con el mensaje
// del chat y se les cuelga el archivo.
//
//	kciwapp-server adjuntar <carpeta> <chat jid>
func adjuntarCLI(args []string) {
	if len(args) < 2 {
		log.Fatalf("uso: kciwapp-server adjuntar <carpeta> <chat jid>")
	}
	carpeta, chat := args[0], args[1]
	local, _ := time.LoadLocation("America/Argentina/Buenos_Aires")
	reArchivo := regexp.MustCompile(`^(\d{8})-([A-Z]+)-(\d{4})-(\d\d)-(\d\d)-(\d\d)-(\d\d)-(\d\d)\.`)
	tipoDe := map[string][]string{"AUDIO": {"nota", "audio"}, "PHOTO": {"imagen"}, "VIDEO": {"video"}, "GIF": {"gif", "video"}, "STICKER": {"figurita"}}
	entradas, _ := os.ReadDir(carpeta)
	pegados, sinMensaje := 0, 0
	var pendientesOnda []int64
	for _, e := range entradas {
		n := e.Name()
		if n == "_chat.txt" || strings.HasSuffix(n, ".log") {
			continue
		}
		var ts time.Time
		var tipos []string
		if g := reArchivo.FindStringSubmatch(n); g != nil {
			ts, _ = time.ParseInLocation("2006-01-02-15-04-05", g[3]+"-"+g[4]+"-"+g[5]+"-"+g[6]+"-"+g[7]+"-"+g[8], local)
			tipos = tipoDe[g[2]]
		} else {
			info, err := e.Info()
			if err != nil {
				continue
			}
			ts = info.ModTime()
			if strings.HasSuffix(strings.ToLower(n), ".vcf") {
				tipos = []string{"contacto"}
			} else {
				tipos = []string{"documento"}
			}
		}
		if len(tipos) == 0 {
			continue
		}
		// El mensaje: mismo tipo, misma fecha (±2 s por los sueltos), sin archivo bajado.
		marcas := strings.Repeat("?,", len(tipos))
		marcas = marcas[:len(marcas)-1]
		args := []any{chat, ts.UnixMilli() - 2000, ts.UnixMilli() + 2000}
		for _, t := range tipos {
			args = append(args, t)
		}
		var msgID, tipo string
		var mediaID int64
		var estado int
		err := db.QueryRow(`SELECT m.id_wa, m.tipo, COALESCE(md.id, 0), COALESCE(md.estado, 0) FROM mensajes m LEFT JOIN media md ON md.id = m.media_id
			WHERE m.chat = ? AND m.ts BETWEEN ? AND ? AND m.tipo IN (`+marcas+`) AND (md.id IS NULL OR md.estado <> 1)
			ORDER BY ABS(m.ts - ?) LIMIT 1`, append(args, ts.UnixMilli())...).Scan(&msgID, &tipo, &mediaID, &estado)
		if err != nil {
			sinMensaje++
			continue
		}
		origen := filepath.Join(carpeta, n)
		info, _ := os.Stat(origen)
		var bytes int64
		if info != nil {
			bytes = info.Size()
		}
		mime := "application/octet-stream"
		switch strings.ToLower(filepath.Ext(n)) {
		case ".opus", ".ogg":
			mime = "audio/ogg; codecs=opus"
		case ".jpg", ".jpeg":
			mime = "image/jpeg"
		case ".png":
			mime = "image/png"
		case ".webp":
			mime = "image/webp"
		case ".mp4":
			mime = "video/mp4"
		case ".vcf":
			mime = "text/vcard"
		}
		if tipo == "contacto" {
			if v, err := os.ReadFile(origen); err == nil {
				db.Exec("UPDATE mensajes SET texto = ? WHERE chat = ? AND id_wa = ? AND texto = ''", n[9:len(n)-4]+"\n"+string(v), chat, msgID)
			}
			os.Rename(origen, origen+".usado")
			pegados++
			continue
		}
		if mediaID == 0 {
			nombreDoc := ""
			if tipo == "documento" && len(n) > 9 {
				nombreDoc = n[9:]
			}
			res, err := db.Exec(`INSERT INTO media (chat, mensaje, mime, nombre, bytes, estado) VALUES (?, ?, ?, ?, ?, 1)`, chat, msgID, mime, nombreDoc, bytes)
			if err != nil {
				continue
			}
			mediaID, _ = res.LastInsertId()
			db.Exec("UPDATE mensajes SET media_id = ? WHERE chat = ? AND id_wa = ?", mediaID, chat, msgID)
		}
		var nombreDoc string
		db.QueryRow("SELECT nombre FROM media WHERE id = ?", mediaID).Scan(&nombreDoc)
		destino := rutaPara(mediaID, mime, nombreDoc)
		os.MkdirAll(filepath.Dir(destino), 0o755)
		if err := os.Rename(origen, destino); err != nil {
			if datos, err := os.ReadFile(origen); err == nil {
				os.WriteFile(destino, datos, 0o644)
				os.Remove(origen)
			}
		}
		db.Exec("UPDATE media SET ruta = ?, estado = 1, bytes = ?, intentos = 0 WHERE id = ?", destino, bytes, mediaID)
		pendientesOnda = append(pendientesOnda, mediaID)
		pegados++
		if pegados%1000 == 0 {
			log.Printf("  %d pegados", pegados)
		}
	}
	log.Printf("archivos pegados a mensajes existentes: %d; sin mensaje que coincida: %d", pegados, sinMensaje)
	evento("historia", chat, map[string]any{"cantidad": pegados})
	// Ondas, duraciones y miniaturas de lo pegado.
	log.Printf("ondas y miniaturas (%d, en paralelo)...", len(pendientesOnda))
	procesarMediaEnParalelo(pendientesOnda, chat)
	log.Printf("listo")
}
