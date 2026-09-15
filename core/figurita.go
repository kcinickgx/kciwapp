package main

import (
	"os"
	"os/exec"
	"path/filepath"
)

// Convierte cualquier imagen a un sticker de WhatsApp: WebP de 512x512 con
// fondo transparente (la imagen entera, centrada, sin deformar).
func aFigurita(datos []byte) ([]byte, error) {
	dir, err := os.MkdirTemp("", "figurita")
	if err != nil {
		return nil, err
	}
	defer os.RemoveAll(dir)
	entrada := filepath.Join(dir, "entrada")
	salida := filepath.Join(dir, "salida.webp")
	if err := os.WriteFile(entrada, datos, 0o600); err != nil {
		return nil, err
	}
	cmd := exec.Command("ffmpeg", "-y", "-loglevel", "error", "-i", entrada,
		"-vf", "scale=512:512:force_original_aspect_ratio=decrease,pad=512:512:-1:-1:color=0x00000000,format=rgba",
		"-c:v", "libwebp", "-lossless", "0", "-q:v", "85", "-frames:v", "1", salida)
	if out, err := cmd.CombinedOutput(); err != nil {
		return nil, &errorTexto{"ffmpeg: " + string(out)}
	}
	return os.ReadFile(salida)
}
