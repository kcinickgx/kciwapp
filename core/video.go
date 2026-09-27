package main

// Los videos que se mandan van como los manda el telefono: H.264 (yuv420p,
// perfil baseline) + AAC en un MP4 con faststart, a lo sumo 1280 de lado
// largo. Un HEVC/AV1/VP9, un 4K o un MKV se ven bien aca (mpv aguanta todo)
// pero en el celular o en WhatsApp Web no: se bajan y "no se pueden
// reproducir". Por eso, al mandar, lo que no cumple se reencodea con ffmpeg.

import (
	"encoding/json"
	"log"
	"os"
	"os/exec"
	"path/filepath"
	"strconv"
	"strings"
)

const (
	videoLadoMax = 1280  // igual que el telefono: nada de 4K por WhatsApp
	videoBitsMax = 3_500 // kbit/s a partir de los cuales conviene reencodear
)

type datosVideo struct {
	codecVideo, codecAudio string
	ancho, alto            int
	pixfmt                 string
	segundos               int
	kbits                  int
	hayAudio               bool
}

// ffprobe -print_format json: codecs, tamano, pixfmt y bitrate.
func mirarVideo(ruta string) (datosVideo, error) {
	var d datosVideo
	out, err := exec.Command("ffprobe", "-v", "error", "-print_format", "json",
		"-show_entries", "stream=codec_type,codec_name,width,height,pix_fmt", "-show_entries", "format=duration,bit_rate", ruta).Output()
	if err != nil {
		return d, err
	}
	var r struct {
		Streams []struct {
			CodecType string `json:"codec_type"`
			CodecName string `json:"codec_name"`
			Width     int    `json:"width"`
			Height    int    `json:"height"`
			PixFmt    string `json:"pix_fmt"`
		} `json:"streams"`
		Format struct {
			Duration string `json:"duration"`
			BitRate  string `json:"bit_rate"`
		} `json:"format"`
	}
	if err := json.Unmarshal(out, &r); err != nil {
		return d, err
	}
	for _, s := range r.Streams {
		switch s.CodecType {
		case "video":
			if d.codecVideo == "" {
				d.codecVideo, d.ancho, d.alto, d.pixfmt = s.CodecName, s.Width, s.Height, s.PixFmt
			}
		case "audio":
			d.hayAudio = true
			if d.codecAudio == "" {
				d.codecAudio = s.CodecName
			}
		}
	}
	d.segundos = int(aFloat(r.Format.Duration) + 0.5)
	d.kbits = int(aFloat(r.Format.BitRate) / 1000)
	return d, nil
}

func aFloat(s string) float64 {
	f, err := strconv.ParseFloat(strings.TrimSpace(s), 64)
	if err != nil {
		return 0
	}
	return f
}

// Hace falta reencodear? (codec, pixfmt, tamano o bitrate). El contenedor
// no cuenta: un MKV con H.264/AAC se arregla remuxeando, sin perder nada.
func hayQueReencodear(d datosVideo) bool {
	if d.codecVideo == "" {
		return false // no pudimos mirarlo: se manda tal cual
	}
	if d.codecVideo != "h264" {
		return true
	}
	if d.pixfmt != "" && d.pixfmt != "yuv420p" {
		return true // 10 bits o 4:2:2/4:4:4: el celular no los toca
	}
	if d.ancho > videoLadoMax || d.alto > videoLadoMax {
		return true
	}
	if d.kbits > videoBitsMax {
		return true
	}
	return d.hayAudio && d.codecAudio != "aac"
}

// Lo que sale de normalizarVideo: el archivo listo para mandar.
type videoListo struct {
	datos      []byte
	mime       string
	segundos   int
	ancho      int
	alto       int
	miniatura  []byte // JPEG para que el celular muestre la tapa
	reencodeado bool
}

// Deja el video como lo manda el telefono. Si no hace falta tocarlo (o
// ffmpeg no esta), devuelve lo mismo que entro.
func normalizarVideo(datos []byte, mimeT string) videoListo {
	r := videoListo{datos: datos, mime: mimeT}
	dir, err := os.MkdirTemp("", "video")
	if err != nil {
		return r
	}
	defer os.RemoveAll(dir)
	entrada := filepath.Join(dir, "entrada")
	salida := filepath.Join(dir, "salida.mp4")
	if os.WriteFile(entrada, datos, 0o600) != nil {
		return r
	}
	d, err := mirarVideo(entrada)
	if err != nil {
		return r
	}
	r.segundos, r.ancho, r.alto = d.segundos, d.ancho, d.alto
	if !hayQueReencodear(d) {
		if strings.Contains(mimeT, "mp4") {
			r.miniatura = miniaturaDeVideo(entrada)
			return r
		}
		// Esta todo bien menos el envase (un MKV, por ejemplo): a MP4 sin
		// tocar los streams, que es instantaneo y no pierde calidad.
		if err := exec.Command("ffmpeg", "-y", "-loglevel", "error", "-i", entrada,
			"-c", "copy", "-movflags", "+faststart", salida).Run(); err == nil {
			if nuevos, err := os.ReadFile(salida); err == nil && len(nuevos) > 0 {
				log.Printf("video remuxeado a mp4 (%s, sin reencodear)", d.codecVideo)
				r.datos, r.mime = nuevos, "video/mp4"
				r.miniatura = miniaturaDeVideo(salida)
				r.reencodeado = true
				return r
			}
		}
		r.miniatura = miniaturaDeVideo(entrada)
		return r
	}
	// Lado largo a 1280 como mucho, y a pixeles pares (libx264 los pide).
	escala := "scale='if(gt(max(iw,ih),1280),if(gte(iw,ih),1280,-2),iw)':'if(gt(max(iw,ih),1280),if(gte(iw,ih),-2,1280),ih)'"
	args := []string{"-y", "-loglevel", "error", "-i", entrada,
		"-c:v", "libx264", "-profile:v", "baseline", "-level", "3.1", "-pix_fmt", "yuv420p",
		"-vf", escala + ",format=yuv420p", "-preset", "veryfast", "-crf", "26", "-maxrate", "3000k", "-bufsize", "6000k",
		"-movflags", "+faststart"}
	if d.hayAudio {
		args = append(args, "-c:a", "aac", "-b:a", "128k", "-ac", "2", "-ar", "44100")
	} else {
		args = append(args, "-an")
	}
	args = append(args, salida)
	if out, err := exec.Command("ffmpeg", args...).CombinedOutput(); err != nil {
		log.Printf("video: no se pudo reencodear (%v): %s", err, string(out))
		r.miniatura = miniaturaDeVideo(entrada)
		return r
	}
	nuevos, err := os.ReadFile(salida)
	if err != nil || len(nuevos) == 0 {
		r.miniatura = miniaturaDeVideo(entrada)
		return r
	}
	n, _ := mirarVideo(salida)
	log.Printf("video reencodeado para el telefono: %s %dx%d %d KB -> h264 %dx%d %d KB",
		d.codecVideo, d.ancho, d.alto, len(datos)/1024, n.ancho, n.alto, len(nuevos)/1024)
	r.datos, r.mime, r.segundos, r.ancho, r.alto = nuevos, "video/mp4", n.segundos, n.ancho, n.alto
	r.miniatura = miniaturaDeVideo(salida)
	r.reencodeado = true
	return r
}
