package main

// "Lossless": un truco que solo entienden dos kciwapp. WhatsApp recomprime y
// reescala fotos y videos, pero NO toca los documentos. Asi que, con el tilde
// puesto, en vez de mandar la imagen/video como media (que se degrada), la
// metemos tal cual en un .zip y la mandamos como documento: llega intacta,
// byte por byte. Del otro lado kciwapp reconoce ese zip por el nombre, lo
// descomprime solo y lo muestra como imagen/video. Quien use WhatsApp normal
// ve un .zip adjunto; nosotros vemos la foto o el video.

import (
	"archive/zip"
	"bytes"
	"crypto/rand"
	"encoding/hex"
	"io"
	"mime"
	"os"
	"path/filepath"
	"regexp"
	"strings"
)

// Adentro del zip: el archivo con la media y un marcador que confirma que el
// zip es nuestro y guarda el mime original.
const marcadorLossless = ".kciwapp"
const mediaLossless = "media"

// Nombre externo: img-<hash>.zip para fotos, vid-<hash>.zip para videos. El
// hash es al azar para que cada envio sea distinto.
var reLossless = regexp.MustCompile(`(?i)^(img|vid)-[0-9a-f]{6,}\.zip$`)

// esNombreLossless dice si el nombre de un documento tiene la pinta de un zip
// lossless nuestro (heuristica por el nombre; el marcador de adentro lo
// confirma al desempaquetar). video=true si es vid-...zip.
func esNombreLossless(nombre string) (ok bool, video bool) {
	m := reLossless.FindStringSubmatch(nombre)
	if m == nil {
		return false, false
	}
	return true, strings.EqualFold(m[1], "vid")
}

func hashLossless() string {
	var b [6]byte
	rand.Read(b[:])
	return hex.EncodeToString(b[:])
}

// empaquetarLossless mete los datos originales en un zip con el marcador, y
// devuelve el zip y el nombre externo (img-/vid- segun sea video).
func empaquetarLossless(datos []byte, mimeOrig string, video bool) ([]byte, string, error) {
	var buf bytes.Buffer
	zw := zip.NewWriter(&buf)

	// El marcador: su contenido es el mime original, para reconstruirlo exacto.
	if mw, err := zw.CreateHeader(&zip.FileHeader{Name: marcadorLossless, Method: zip.Store}); err != nil {
		return nil, "", err
	} else if _, err := mw.Write([]byte(mimeOrig)); err != nil {
		return nil, "", err
	}

	// La media, sin comprimir (ya es jpg/png/mp4: comprimir no ayuda y tarda).
	ext := extLossless(mimeOrig, video)
	fh := &zip.FileHeader{Name: mediaLossless + ext, Method: zip.Store}
	if fw, err := zw.CreateHeader(fh); err != nil {
		return nil, "", err
	} else if _, err := fw.Write(datos); err != nil {
		return nil, "", err
	}
	if err := zw.Close(); err != nil {
		return nil, "", err
	}
	prefijo := "img-"
	if video {
		prefijo = "vid-"
	}
	return buf.Bytes(), prefijo + hashLossless() + ".zip", nil
}

// desempaquetarLossless saca la media de un zip lossless. ok=false si el zip no
// es nuestro (no tiene el marcador): entonces se trata como un documento comun.
func desempaquetarLossless(zipDatos []byte) (datos []byte, mimeOrig string, ok bool) {
	zr, err := zip.NewReader(bytes.NewReader(zipDatos), int64(len(zipDatos)))
	if err != nil {
		return nil, "", false
	}
	tieneMarcador := false
	for _, f := range zr.File {
		if f.Name == marcadorLossless {
			if rc, err := f.Open(); err == nil {
				b, _ := io.ReadAll(rc)
				rc.Close()
				mimeOrig = strings.TrimSpace(string(b))
			}
			tieneMarcador = true
			break
		}
	}
	if !tieneMarcador {
		return nil, "", false
	}
	for _, f := range zr.File {
		if f.Name == marcadorLossless || f.FileInfo().IsDir() {
			continue
		}
		rc, err := f.Open()
		if err != nil {
			return nil, "", false
		}
		b, err := io.ReadAll(rc)
		rc.Close()
		if err != nil {
			return nil, "", false
		}
		if mimeOrig == "" {
			mimeOrig = mime.TypeByExtension(filepath.Ext(f.Name))
		}
		return b, mimeOrig, true
	}
	return nil, "", false
}

// miniaturaDeBytes: una miniatura JPEG a partir de datos en memoria (los
// escribe a un temp y reusa el de ffmpeg). Para que el documento .zip lossless
// muestre la foto/video como preview, en kciwapp y hasta en WhatsApp comun.
func miniaturaDeBytes(datos []byte, ext string) []byte {
	dir, err := os.MkdirTemp("", "llmini")
	if err != nil {
		return nil
	}
	defer os.RemoveAll(dir)
	ruta := filepath.Join(dir, "in"+ext)
	if os.WriteFile(ruta, datos, 0o644) != nil {
		return nil
	}
	return miniaturaDeVideo(ruta)
}

// extLossless: la extension del archivo interno segun el mime original.
func extLossless(mimeT string, video bool) string {
	base := strings.TrimSpace(strings.Split(mimeT, ";")[0])
	if e, hay := extensiones[base]; hay {
		return e
	}
	if lista, _ := mime.ExtensionsByType(base); len(lista) > 0 {
		return lista[0]
	}
	if video {
		return ".mp4"
	}
	return ".jpg"
}
