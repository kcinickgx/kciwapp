package main

// POST /contacto {chat, jids: [...]}: manda uno o varios contactos (vCard)
// al chat, como hace el telefono al compartir un contacto. Nombre y telefono
// salen de lo que sabemos del contacto (agenda, push, o el numero pelado).

import (
	"errors"
	"fmt"
	"net/http"
	"strings"

	"go.mau.fi/whatsmeow/proto/waE2E"
	"google.golang.org/protobuf/proto"
)

// Nombre para mostrar de un jid: agenda, push, o el telefono.
func nombreDeContacto(jid string) string {
	var agenda, push, tel string
	db.QueryRow("SELECT COALESCE(nombre_agenda,''), COALESCE(nombre_push,''), COALESCE(telefono,'') FROM contactos WHERE jid = ?", jid).Scan(&agenda, &push, &tel)
	if agenda != "" {
		return agenda
	}
	if push != "" {
		return push
	}
	if tel != "" {
		return "+" + tel
	}
	return "+" + strings.SplitN(jid, "@", 2)[0]
}

func vcardDe(jid, nombre string) string {
	tel := strings.SplitN(jid, "@", 2)[0]
	return fmt.Sprintf("BEGIN:VCARD\nVERSION:3.0\nN:;%s;;;\nFN:%s\nTEL;type=CELL;waid=%s:+%s\nEND:VCARD", nombre, nombre, tel, tel)
}

func hContacto(w http.ResponseWriter, r *http.Request) {
	var p struct {
		Chat string   `json:"chat"`
		JIDs []string `json:"jids"`
		Cita string   `json:"cita_id"`
	}
	if err := leerJSON(r, &p); err != nil {
		fallar(w, 400, err)
		return
	}
	j, ok := jidDe(p.Chat)
	if !ok || len(p.JIDs) == 0 {
		fallar(w, 400, errors.New("chat and jids required"))
		return
	}
	ctxInfo := contextoCita(p.Chat, p.Cita)
	var msg *waE2E.Message
	partes := []string{}
	if len(p.JIDs) == 1 {
		nombre := nombreDeContacto(p.JIDs[0])
		vc := vcardDe(p.JIDs[0], nombre)
		partes = append(partes, nombre+"\n"+vc)
		msg = &waE2E.Message{ContactMessage: &waE2E.ContactMessage{DisplayName: proto.String(nombre), Vcard: proto.String(vc), ContextInfo: ctxInfo}}
	} else {
		arr := &waE2E.ContactsArrayMessage{ContextInfo: ctxInfo}
		for _, jid := range p.JIDs {
			nombre := nombreDeContacto(jid)
			vc := vcardDe(jid, nombre)
			partes = append(partes, nombre+"\n"+vc)
			arr.Contacts = append(arr.Contacts, &waE2E.ContactMessage{DisplayName: proto.String(nombre), Vcard: proto.String(vc)})
		}
		arr.DisplayName = proto.String(fmt.Sprintf("%d contacts", len(p.JIDs)))
		msg = &waE2E.Message{ContactsArrayMessage: arr}
	}
	resp, err := cli.SendMessage(ctx, j, msg)
	if err != nil {
		fallar(w, http.StatusBadGateway, err)
		return
	}
	c := contenido{tipo: "contacto", texto: strings.Join(partes, "\n\n"), ctx: ctxInfo}
	responder(w, anotarEnviado(p.Chat, resp, c, ctxInfo, ""))
}
