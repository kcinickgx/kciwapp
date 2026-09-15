package main

import (
	"bufio"
	"fmt"
	"log"
	"os"
	"os/exec"
	"path/filepath"
	"regexp"
	"runtime"
	"strconv"
	"strings"
	"sync"
	"time"
)

func execCommandOutput(nombre string, args ...string) (string, error) {
	out, err := exec.Command(nombre, args...).Output()
	return string(out), err
}

// Importa un export de WhatsApp de iOS ("Export chat" con media): la
// carpeta tiene _chat.txt y los archivos con prefijo de orden
// (00000237-AUDIO-2018-09-20-17-22-00.opus, 00017493-Maxi.vcf ...).
//
//	kciwapp-server importar <carpeta> <jid del chat> <mi nombre en el export>
//
// Entran solo los mensajes anteriores al mas viejo que el server ya tenga de
// ese chat, asi no se duplica nada. Los ids son "imp-<orden>".
func importarCLI(args []string) {
	if len(args) < 3 {
		log.Fatalf("uso: kciwapp-server importar <carpeta> <chat jid> <mi nombre>")
	}
	carpeta, chat, yo := args[0], args[1], args[2]
	f, err := os.Open(filepath.Join(carpeta, "_chat.txt"))
	if err != nil {
		log.Fatalf("no se puede abrir _chat.txt: %v", err)
	}
	defer f.Close()

	// Hasta donde importar: lo anterior a lo que ya hay.
	var tope int64 = 1 << 62
	db.QueryRow("SELECT COALESCE(MIN(ts), ?) FROM mensajes WHERE chat = ? AND id_wa NOT LIKE 'imp-%'", tope, chat).Scan(&tope)
	log.Printf("importando %s a %s (yo = %s), solo antes de %s", carpeta, chat, yo, time.UnixMilli(tope).Format("2006-01-02 15:04"))

	// [dd/mm/aaaa, hh:mm:ss] Nombre: texto   (con o sin el LRM adelante)
	re := regexp.MustCompile(`^\x{200e}?\[(\d\d)/(\d\d)/(\d{4}), (\d\d):(\d\d):(\d\d)\] ([^:]+): (.*)$`)
	local, _ := time.LoadLocation("America/Argentina/Buenos_Aires")

	type msg struct {
		orden  int
		ts     int64
		quien  string
		texto  string
	}
	// Los archivos: los con fecha (AUDIO/PHOTO/VIDEO/STICKER-aaaa-mm-dd-hh-mm-ss)
	// se emparejan por tipo + fecha exacta del mensaje; los otros (documentos,
	// vcards: <orden>-nombre) por el corrimiento entre su numero y el orden
	// del mensaje, que se va aprendiendo de los con fecha.
	porFecha := map[string][]string{}
	type suelto struct {
		idx    int
		nombre string
	}
	var sueltos []suelto
	reArchivo := regexp.MustCompile(`^(\d{8})-([A-Z]+)-(\d{4}-\d\d-\d\d-\d\d-\d\d-\d\d)\.`)
	entradas, _ := os.ReadDir(carpeta)
	for _, e := range entradas {
		n := e.Name()
		if g := reArchivo.FindStringSubmatch(n); g != nil {
			clave := g[2] + "-" + g[3]
			porFecha[clave] = append(porFecha[clave], n)
		} else if len(n) > 9 && n[8] == '-' {
			if k, err := strconv.Atoi(n[:8]); err == nil {
				sueltos = append(sueltos, suelto{k, n})
			}
		}
	}
	corrimiento := 0
	usados := map[string]bool{}
	log.Printf("%d archivos con fecha, %d sueltos", len(porFecha), len(sueltos))
	tipoArchivo := map[string]string{"nota": "AUDIO", "imagen": "PHOTO", "video": "VIDEO", "gif": "GIF", "figurita": "STICKER"}
	buscarArchivo := func(m *msg, tipoMedia string) string {
		if pref, ok := tipoArchivo[tipoMedia]; ok {
			clave := pref + "-" + time.UnixMilli(m.ts).In(local).Format("2006-01-02-15-04-05")
			lista := porFecha[clave]
			if len(lista) == 0 {
				return ""
			}
			n := lista[0]
			porFecha[clave] = lista[1:]
			if k, err := strconv.Atoi(n[:8]); err == nil {
				corrimiento = k - m.orden
			}
			return n
		}
		// Sueltos: el mas cercano a orden + corrimiento, con tolerancia.
		mejor, mejorDist := "", 1<<30
		for _, x := range sueltos {
			if usados[x.nombre] {
				continue
			}
			d := x.idx - (m.orden + corrimiento)
			if d < 0 {
				d = -d
			}
			if d < mejorDist {
				mejor, mejorDist = x.nombre, d
			}
		}
		if mejorDist > 50 {
			return ""
		}
		usados[mejor] = true
		return mejor
	}

	var actual *msg
	orden := -1
	total, saltados, conMedia := 0, 0, 0

	limpiar := func(s string) string {
		s = strings.TrimRight(s, "\r")
		s = strings.ReplaceAll(s, "‎", "")
		return s
	}

	procesar := func(m *msg) {
		if m == nil {
			return
		}
		texto := strings.TrimSpace(m.texto)
		if strings.Contains(texto, "Messages and calls are end-to-end encrypted") {
			return
		}
		if m.ts >= tope {
			saltados++
			return
		}
		propio := m.quien == yo
		remitente := chat
		if propio {
			remitente = cli.Store.ID.ToNonAD().String()
		}
		id := fmt.Sprintf("imp-%d", m.orden)
		editado := false
		if strings.HasSuffix(texto, "<This message was edited>") {
			editado = true
			texto = strings.TrimSpace(strings.TrimSuffix(texto, "<This message was edited>"))
		}
		borrado := texto == "This message was deleted." || texto == "You deleted this message."
		tipo := "texto"
		var mediaID any
		if !borrado {
			mime, tipoMedia := "", ""
			switch {
			case strings.HasSuffix(texto, "audio omitted"):
				tipoMedia, mime = "nota", "audio/ogg; codecs=opus"
			case strings.HasSuffix(texto, "image omitted"):
				tipoMedia, mime = "imagen", "image/jpeg"
			case strings.HasSuffix(texto, "video omitted"):
				tipoMedia, mime = "video", "video/mp4"
			case strings.HasSuffix(texto, "GIF omitted"):
				tipoMedia, mime = "gif", "video/mp4"
			case strings.HasSuffix(texto, "sticker omitted"):
				tipoMedia, mime = "figurita", "image/webp"
			case strings.HasSuffix(texto, "document omitted"):
				tipoMedia, mime = "documento", "application/octet-stream"
			case strings.HasSuffix(texto, "Contact card omitted"):
				tipoMedia = "contacto"
			}
			if tipoMedia != "" {
				texto = ""
				nombre := buscarArchivo(m, tipoMedia)
				if tipoMedia == "contacto" {
					tipo = "contacto"
					if nombre != "" {
						if v, err := os.ReadFile(filepath.Join(carpeta, nombre)); err == nil {
							texto = strings.TrimSuffix(strings.TrimPrefix(nombre, nombre[:9]), ".vcf") + "\n" + string(v)
						}
					}
				} else if nombre == "" {
					// El celu no exporto el archivo (lo habia borrado): queda el aviso.
					tipo = tipoMedia
					texto = ""
					mediaID = nil
				} else {
					tipo = tipoMedia
					origen := filepath.Join(carpeta, nombre)
					ext := filepath.Ext(nombre)
					if ext == ".opus" {
						mime = "audio/ogg; codecs=opus"
					} else if ext == ".jpg" || ext == ".jpeg" {
						mime = "image/jpeg"
					} else if ext == ".webp" {
						mime = "image/webp"
					} else if ext == ".mp4" {
						mime = "video/mp4"
					} else if ext == ".png" {
						mime = "image/png"
					} else if tipoMedia == "documento" {
						mime = "application/octet-stream"
					}
					info, _ := os.Stat(origen)
					var bytes int64
					if info != nil {
						bytes = info.Size()
					}
					nombreDoc := ""
					if tipoMedia == "documento" {
						nombreDoc = nombre[9:]
					}
					res, err := db.Exec(`INSERT INTO media (chat, mensaje, mime, nombre, bytes, estado) VALUES (?, ?, ?, ?, ?, 1)`,
						chat, id, mime, nombreDoc, bytes)
					if err == nil {
						mid, _ := res.LastInsertId()
						destino := rutaPara(mid, mime, nombreDoc)
						os.MkdirAll(filepath.Dir(destino), 0o755)
						if err := os.Rename(origen, destino); err != nil {
							// Distinto filesystem: copiar.
							if datos, err := os.ReadFile(origen); err == nil {
								os.WriteFile(destino, datos, 0o644)
							}
						}
						db.Exec("UPDATE media SET ruta = ? WHERE id = ?", destino, mid)
						mediaID = mid
						conMedia++
					}
				}
			}
		}
		estado := 0
		if propio {
			estado = 3
		}
		_, err := db.Exec(`INSERT IGNORE INTO mensajes (id_wa, chat, remitente, propio, ts, tipo, texto, cita_id, cita_remitente, cita_texto, media_id, editado, borrado, estado, reenviado)
			VALUES (?, ?, ?, ?, ?, ?, ?, '', '', '', ?, ?, ?, ?, 0)`,
			id, chat, remitente, propio, m.ts, tipo, texto, mediaID, editado, borrado, estado)
		if err != nil {
			log.Printf("mensaje %d: %v", m.orden, err)
			return
		}
		total++
		if total%5000 == 0 {
			log.Printf("  %d mensajes...", total)
		}
	}

	sc := bufio.NewScanner(f)
	sc.Buffer(make([]byte, 1<<20), 1<<24)
	for sc.Scan() {
		linea := limpiar(sc.Text())
		if g := re.FindStringSubmatch(sc.Text()); g != nil {
			procesar(actual)
			orden++
			d, _ := strconv.Atoi(g[1])
			mo, _ := strconv.Atoi(g[2])
			y, _ := strconv.Atoi(g[3])
			h, _ := strconv.Atoi(g[4])
			mi, _ := strconv.Atoi(g[5])
			s, _ := strconv.Atoi(g[6])
			t := time.Date(y, time.Month(mo), d, h, mi, s, 0, local)
			actual = &msg{orden: orden, ts: t.UnixMilli(), quien: strings.TrimSpace(g[7]), texto: limpiar(g[8])}
		} else if actual != nil {
			// Continuacion de un mensaje de varias lineas.
			actual.texto += "\n" + linea
		}
	}
	procesar(actual)
	asegurarChat(chat)
	log.Printf("listo: %d mensajes importados (%d con media), %d saltados por ser posteriores a lo que ya habia", total, conMedia, saltados)
	evento("historia", chat, map[string]any{"cantidad": total})
	// Las ondas y miniaturas se calculan al arrancar el server (rellenarOndas no
	// sirve aca porque no hay proto): se hace aca mismo, con ffmpeg.
	log.Printf("calculando ondas de audio y miniaturas de video (tarda)...")
	filas, err := db.Query(`SELECT md.id, md.ruta, m.tipo FROM media md JOIN mensajes m ON m.chat = md.chat AND m.id_wa = md.mensaje
		WHERE m.chat = ? AND m.id_wa LIKE 'imp-%' AND md.onda IS NULL AND md.miniatura IS NULL AND md.ruta <> ''`, chat)
	if err == nil {
		type fila struct{ id int64; ruta, tipo string }
		var lista []fila
		for filas.Next() {
			var x fila
			if filas.Scan(&x.id, &x.ruta, &x.tipo) == nil {
				lista = append(lista, x)
			}
		}
		filas.Close()
		var ids []int64
		for _, x := range lista {
			ids = append(ids, x.id)
		}
		procesarMediaEnParalelo(ids, chat)
	}
	log.Printf("importacion terminada")
}

// Ondas, duraciones y miniaturas de una lista de media, con un worker por
// nucleo (cada uno lanza su ffmpeg).
func procesarMediaEnParalelo(ids []int64, chat string) {
	trabajos := make(chan int64)
	var hechos int64
	var mu sync.Mutex
	var wg sync.WaitGroup
	for w := 0; w < runtime.NumCPU(); w++ {
		wg.Add(1)
		go func() {
			defer wg.Done()
			for id := range trabajos {
				var ruta, tipo string
				var seg int
				var tieneOnda, tieneMini bool
				if db.QueryRow(`SELECT md.ruta, m.tipo, md.segundos, md.onda IS NOT NULL, md.miniatura IS NOT NULL FROM media md
					JOIN mensajes m ON m.chat = md.chat AND m.id_wa = md.mensaje WHERE md.id = ?`, id).Scan(&ruta, &tipo, &seg, &tieneOnda, &tieneMini) != nil {
					continue
				}
				if tipo == "nota" || tipo == "audio" {
					if !tieneOnda {
						if datos, err := os.ReadFile(ruta); err == nil {
							if onda := ondaDe(datos); onda != nil {
								db.Exec("UPDATE media SET onda = ? WHERE id = ?", onda, id)
							}
						}
					}
					if seg == 0 {
						if s := duracionDe(ruta); s > 0 {
							db.Exec("UPDATE media SET segundos = ? WHERE id = ?", s, id)
						}
					}
				} else if tipo == "video" || tipo == "gif" {
					if !tieneMini {
						if mini := miniaturaDeVideo(ruta); mini != nil {
							db.Exec("UPDATE media SET miniatura = ? WHERE id = ?", mini, id)
						}
					}
					if seg == 0 {
						if s := duracionDe(ruta); s > 0 {
							db.Exec("UPDATE media SET segundos = ? WHERE id = ?", s, id)
						}
					}
				}
				mu.Lock()
				hechos++
				if hechos%1000 == 0 {
					log.Printf("  %d / %d", hechos, len(ids))
				}
				mu.Unlock()
			}
		}()
	}
	for _, id := range ids {
		trabajos <- id
	}
	close(trabajos)
	wg.Wait()
	_ = chat
}

// Duracion en segundos de un audio/video, con ffprobe.
func duracionDe(ruta string) int {
	out, err := execCommandOutput("ffprobe", "-v", "error", "-show_entries", "format=duration", "-of", "default=noprint_wrappers=1:nokey=1", ruta)
	if err != nil {
		return 0
	}
	f, err := strconv.ParseFloat(strings.TrimSpace(out), 64)
	if err != nil {
		return 0
	}
	return int(f + 0.5)
}
