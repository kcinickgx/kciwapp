package main

// Llamadas: whatsmeow solo trae la senalizacion (quien llama, si es video,
// cuando se acepta/corta) y puede rechazar. No hay audio ni video. Con eso
// alcanza para avisar la entrante y dejar "Missed video call" en el chat.

import (
	"fmt"
	"log"
	"sync"

	waBinary "go.mau.fi/whatsmeow/binary"
	"go.mau.fi/whatsmeow/types"
	"go.mau.fi/whatsmeow/types/events"
)

type llamada struct {
	chat, de string
	video    bool
	propia   bool  // la iniciamos nosotros (desde otro dispositivo, p. ej. WhatsApp Web)
	ts       int64 // cuando entro (ms)
	aceptada int64 // cuando se atendio (ms), 0 si no
	cerrada  bool
}

var (
	llamadasMu sync.Mutex
	llamadas   = map[string]*llamada{}
)

func esVideo(n *waBinary.Node) bool {
	if n == nil {
		return false
	}
	return n.GetChildByTag("video").Tag == "video"
}

// El que llama, como numero (si viene como LID, el PN alternativo).
func quienLlama(m types.BasicCallMeta) string {
	if m.CallCreator.Server == types.HiddenUserServer && !m.CallCreatorAlt.IsEmpty() {
		return normalizar(m.CallCreatorAlt)
	}
	if !m.CallCreator.IsEmpty() {
		return normalizar(m.CallCreator)
	}
	return normalizar(m.From)
}

func chatDeLlamada(m types.BasicCallMeta) string {
	if !m.GroupJID.IsEmpty() {
		return normalizar(m.GroupJID)
	}
	return quienLlama(m)
}

// Nuestro propio jid (telefono), para saber si la llamada la iniciamos nosotros.
func miJID() string {
	if cli.Store.ID == nil {
		return ""
	}
	return cli.Store.ID.ToNonAD().String()
}

func llamadaEntrante(m types.BasicCallMeta, video bool) {
	l := &llamada{chat: chatDeLlamada(m), de: quienLlama(m), video: video, ts: m.Timestamp.UnixMilli()}
	if l.de == miJID() {
		// Saliente (hecha desde WhatsApp Web u otro dispositivo): el chat es
		// el destinatario, y no es "entrante" para nadie.
		l.propia = true
		if !m.GroupJID.IsEmpty() {
			l.chat = normalizar(m.GroupJID)
		} else {
			l.chat = normalizar(m.From)
		}
	}
	if l.ts <= 0 {
		l.ts = ahoraMs()
	}
	llamadasMu.Lock()
	if _, ya := llamadas[m.CallID]; ya {
		llamadasMu.Unlock()
		return
	}
	llamadas[m.CallID] = l
	llamadasMu.Unlock()
	if l.propia {
		log.Printf("llamada saliente a %s (video=%v) id=%s", l.chat, video, m.CallID)
		return
	}
	log.Printf("llamada entrante de %s (video=%v) id=%s", l.de, video, m.CallID)
	evento("llamada", l.chat, map[string]any{"estado": "entrante", "id": m.CallID, "de": l.de, "video": video})
}

func llamadaAceptada(m types.BasicCallMeta) {
	llamadasMu.Lock()
	if l, ok := llamadas[m.CallID]; ok && l.aceptada == 0 {
		l.aceptada = ahoraMs()
	}
	llamadasMu.Unlock()
	evento("llamada", chatDeLlamada(m), map[string]any{"estado": "atendida", "id": m.CallID})
}

// Corto o rechazo: queda el mensaje en el chat (perdida o con duracion).
func llamadaTerminada(m types.BasicCallMeta, motivo string) {
	llamadasMu.Lock()
	l, ok := llamadas[m.CallID]
	if ok && !l.cerrada {
		l.cerrada = true
	} else {
		ok = false
	}
	llamadasMu.Unlock()
	if !ok {
		return
	}
	go func() {
		// Se limpia despues, por si llegan repetidos.
		llamadasMu.Lock()
		defer llamadasMu.Unlock()
		if len(llamadas) > 200 {
			for k, v := range llamadas {
				if v.cerrada {
					delete(llamadas, k)
				}
			}
		}
	}()
	tipo := "voice"
	icono := "\U0001F4DE"
	if l.video {
		tipo, icono = "video", "\U0001F4F9"
	}
	log.Printf("llamada %s terminada: motivo=%q aceptada=%v propia=%v", m.CallID, motivo, l.aceptada > 0, l.propia)
	// Solo lo que se sabe seguro: duracion si vimos el accept, o perdida si
	// se corto por timeout (nadie atendio en ningun dispositivo). Atendida en
	// otro dispositivo, rechazada, cancelada: sin mensaje.
	texto := ""
	if l.aceptada > 0 {
		seg := (ahoraMs() - l.aceptada) / 1000
		texto = fmt.Sprintf("%s %s call · %d:%02d", icono, capitalizar(tipo), seg/60, seg%60)
	} else if motivo == "timeout" && !l.propia {
		texto = fmt.Sprintf("%s Missed %s call", icono, tipo)
	} else if motivo == "timeout" && l.propia {
		texto = fmt.Sprintf("%s %s call (no answer)", icono, capitalizar(tipo))
	}
	if texto != "" {
		estado := 0
		if l.propia {
			estado = 2
		}
		insertarMensaje(&Mensaje{
			ID: "call-" + m.CallID, Chat: l.chat, Remitente: l.de, Propio: l.propia, TS: l.ts,
			Tipo: "sistema", Texto: texto, Estado: estado,
		}, false, "")
	}
	evento("llamada", l.chat, map[string]any{"estado": "fin", "id": m.CallID})
}

func capitalizar(s string) string {
	if s == "" {
		return s
	}
	return string(s[0]-32) + s[1:]
}

// Rechaza una llamada entrante (lo que hace el boton Reject del cliente).
func rechazarLlamada(id string) error {
	llamadasMu.Lock()
	l, ok := llamadas[id]
	llamadasMu.Unlock()
	if !ok {
		return fmt.Errorf("unknown call")
	}
	j, _ := jidDe(l.de)
	if err := cli.RejectCall(ctx, j, id); err != nil {
		return err
	}
	llamadaTerminada(types.BasicCallMeta{CallID: id}, "reject")
	return nil
}

func manejarLlamada(e any) bool {
	switch v := e.(type) {
	case *events.CallOffer:
		llamadaEntrante(v.BasicCallMeta, esVideo(v.Data))
	case *events.CallOfferNotice:
		llamadaEntrante(v.BasicCallMeta, v.Media == "video")
	case *events.CallAccept:
		llamadaAceptada(v.BasicCallMeta)
	case *events.CallTerminate:
		if v.Data != nil {
			log.Printf("terminate: %v", v.Data.Attrs)
		}
		llamadaTerminada(v.BasicCallMeta, v.Reason)
	case *events.CallReject:
		if v.Data != nil {
			log.Printf("reject: %v", v.Data.Attrs)
		}
		llamadaTerminada(v.BasicCallMeta, "reject")
	default:
		return false
	}
	return true
}
