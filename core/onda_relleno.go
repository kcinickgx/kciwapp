package main

import (
	"log"

	"go.mau.fi/whatsmeow/proto/waE2E"
	"google.golang.org/protobuf/proto"
)

// Una sola vez: las notas de voz guardadas antes de que existiera la
// columna `onda` la tienen adentro del proto; se saca de ahi.
func rellenarOndas() {
	filas, err := db.Query(`SELECT md.id, md.proto FROM media md JOIN mensajes m ON m.chat = md.chat AND m.id_wa = md.mensaje
		WHERE md.onda IS NULL AND m.tipo IN ('nota', 'audio') AND md.proto IS NOT NULL`)
	if err != nil {
		return
	}
	type fila struct {
		id    int64
		crudo []byte
	}
	var lista []fila
	for filas.Next() {
		var f fila
		if filas.Scan(&f.id, &f.crudo) == nil {
			lista = append(lista, f)
		}
	}
	filas.Close()
	n := 0
	for _, f := range lista {
		var a waE2E.AudioMessage
		if proto.Unmarshal(f.crudo, &a) != nil || len(a.GetWaveform()) == 0 {
			continue
		}
		db.Exec("UPDATE media SET onda = ? WHERE id = ?", a.GetWaveform(), f.id)
		n++
	}
	if n > 0 {
		log.Printf("ondas rellenadas: %d de %d", n, len(lista))
	}
}
