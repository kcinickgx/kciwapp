package main

import (
	"bytes"
	"encoding/binary"
	"math"
	"os"
	"os/exec"
	"path/filepath"
)

// La forma de onda como la usa WhatsApp: 64 valores de 0 a 100. Se calcula
// decodificando el audio con ffmpeg a PCM mono de 8 kHz y tomando el RMS de
// cada uno de los 64 tramos.
func ondaDe(datos []byte) []byte {
	dir, err := os.MkdirTemp("", "onda")
	if err != nil {
		return nil
	}
	defer os.RemoveAll(dir)
	entrada := filepath.Join(dir, "entrada")
	if err := os.WriteFile(entrada, datos, 0o600); err != nil {
		return nil
	}
	cmd := exec.Command("ffmpeg", "-loglevel", "error", "-i", entrada, "-ac", "1", "-ar", "8000", "-f", "s16le", "-")
	var out bytes.Buffer
	cmd.Stdout = &out
	if err := cmd.Run(); err != nil {
		return nil
	}
	muestras := out.Len() / 2
	if muestras < 64 {
		return nil
	}
	pcm := out.Bytes()
	onda := make([]float64, 64)
	tramo := muestras / 64
	maximo := 0.0
	for i := 0; i < 64; i++ {
		suma := 0.0
		for k := i * tramo; k < (i+1)*tramo; k++ {
			v := float64(int16(binary.LittleEndian.Uint16(pcm[k*2:]))) / 32768.0
			suma += v * v
		}
		onda[i] = math.Sqrt(suma / float64(tramo))
		if onda[i] > maximo {
			maximo = onda[i]
		}
	}
	res := make([]byte, 64)
	if maximo == 0 {
		return res
	}
	for i := range onda {
		res[i] = byte(math.Min(100, math.Round(onda[i]/maximo*100)))
	}
	return res
}
