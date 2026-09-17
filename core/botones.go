package main

// Mensajes interactivos de negocios: botones, listas, plantillas, los
// "native flow" nuevos, productos y pedidos. Se guardan como texto (asi los
// ve cualquier cliente) mas una fila en `botones` con el JSON de lo que hay
// que dibujar y el proto original, que hace falta para responder con el
// mismo formato que manda el telefono (POST /boton).

import (
	"encoding/json"
	"errors"
	"fmt"
	"log"
	"net/http"
	"strings"

	"go.mau.fi/whatsmeow/proto/waE2E"
	"google.golang.org/protobuf/proto"
)

// Lo que ve el cliente.
type Boton struct {
	ID       string `json:"id"`
	Texto    string `json:"texto"`
	Tipo     string `json:"tipo"` // responder | url | llamar | copiar | otro
	URL      string `json:"url,omitempty"`
	Telefono string `json:"telefono,omitempty"`
	Codigo   string `json:"codigo,omitempty"`
}

type FilaLista struct {
	ID          string `json:"id"`
	Titulo      string `json:"titulo"`
	Descripcion string `json:"descripcion,omitempty"`
}

type SeccionLista struct {
	Titulo string      `json:"titulo,omitempty"`
	Filas  []FilaLista `json:"filas"`
}

type Botones struct {
	Tipo       string         `json:"tipo"` // botones | lista | plantilla | interactivo | producto | pedido
	Titulo     string         `json:"titulo,omitempty"`
	Pie        string         `json:"pie,omitempty"`
	Botones    []Boton        `json:"botones,omitempty"`
	BotonLista string         `json:"boton_lista,omitempty"` // el texto del boton que abre la lista
	Secciones  []SeccionLista `json:"secciones,omitempty"`
	Respondido string         `json:"respondido,omitempty"` // lo que ya se eligio (texto)
}

// Saca el JSON de botones de un mensaje entrante. nil si no es interactivo.
func botonesDeMensaje(msg *waE2E.Message) *Botones {
	switch {
	case msg.GetButtonsMessage() != nil:
		b := msg.GetButtonsMessage()
		r := &Botones{Tipo: "botones", Pie: b.GetFooterText()}
		for _, x := range b.GetButtons() {
			r.Botones = append(r.Botones, Boton{ID: x.GetButtonID(), Texto: x.GetButtonText().GetDisplayText(), Tipo: "responder"})
		}
		return r
	case msg.GetListMessage() != nil:
		l := msg.GetListMessage()
		r := &Botones{Tipo: "lista", Titulo: l.GetTitle(), Pie: l.GetFooterText(), BotonLista: l.GetButtonText()}
		if r.BotonLista == "" {
			r.BotonLista = "Options"
		}
		for _, s := range l.GetSections() {
			sec := SeccionLista{Titulo: s.GetTitle()}
			for _, f := range s.GetRows() {
				sec.Filas = append(sec.Filas, FilaLista{ID: f.GetRowID(), Titulo: f.GetTitle(), Descripcion: f.GetDescription()})
			}
			r.Secciones = append(r.Secciones, sec)
		}
		return r
	case msg.GetTemplateMessage() != nil:
		t := msg.GetTemplateMessage().GetHydratedTemplate()
		if t == nil {
			return nil
		}
		r := &Botones{Tipo: "plantilla", Titulo: t.GetHydratedTitleText(), Pie: t.GetHydratedFooterText()}
		for _, x := range t.GetHydratedButtons() {
			switch {
			case x.GetQuickReplyButton() != nil:
				q := x.GetQuickReplyButton()
				r.Botones = append(r.Botones, Boton{ID: q.GetID(), Texto: q.GetDisplayText(), Tipo: "responder"})
			case x.GetUrlButton() != nil:
				u := x.GetUrlButton()
				r.Botones = append(r.Botones, Boton{ID: fmt.Sprint(x.GetIndex()), Texto: u.GetDisplayText(), Tipo: "url", URL: u.GetURL()})
			case x.GetCallButton() != nil:
				c := x.GetCallButton()
				r.Botones = append(r.Botones, Boton{ID: fmt.Sprint(x.GetIndex()), Texto: c.GetDisplayText(), Tipo: "llamar", Telefono: c.GetPhoneNumber()})
			}
		}
		return r
	case msg.GetInteractiveMessage() != nil:
		i := msg.GetInteractiveMessage()
		r := &Botones{Tipo: "interactivo", Titulo: i.GetHeader().GetTitle(), Pie: i.GetFooter().GetText()}
		nf := i.GetNativeFlowMessage()
		if nf == nil {
			return nil
		}
		for _, x := range nf.GetButtons() {
			var p map[string]any
			json.Unmarshal([]byte(x.GetButtonParamsJSON()), &p)
			texto, _ := p["display_text"].(string)
			id, _ := p["id"].(string)
			switch x.GetName() {
			case "quick_reply":
				r.Botones = append(r.Botones, Boton{ID: id, Texto: texto, Tipo: "responder"})
			case "cta_url":
				u, _ := p["url"].(string)
				r.Botones = append(r.Botones, Boton{ID: id, Texto: texto, Tipo: "url", URL: u})
			case "cta_call":
				t, _ := p["phone_number"].(string)
				r.Botones = append(r.Botones, Boton{ID: id, Texto: texto, Tipo: "llamar", Telefono: t})
			case "cta_copy":
				c, _ := p["copy_code"].(string)
				r.Botones = append(r.Botones, Boton{ID: id, Texto: texto, Tipo: "copiar", Codigo: c})
			case "single_select":
				r.BotonLista, _ = p["title"].(string)
				if r.BotonLista == "" {
					r.BotonLista = "Options"
				}
				if secs, ok := p["sections"].([]any); ok {
					for _, s := range secs {
						sm, _ := s.(map[string]any)
						sec := SeccionLista{}
						sec.Titulo, _ = sm["title"].(string)
						if filas, ok := sm["rows"].([]any); ok {
							for _, f := range filas {
								fm, _ := f.(map[string]any)
								fl := FilaLista{}
								fl.ID, _ = fm["id"].(string)
								fl.Titulo, _ = fm["title"].(string)
								fl.Descripcion, _ = fm["description"].(string)
								sec.Filas = append(sec.Filas, fl)
							}
						}
						r.Secciones = append(r.Secciones, sec)
					}
				}
			default:
				if texto == "" {
					texto = strings.ReplaceAll(x.GetName(), "_", " ")
				}
				r.Botones = append(r.Botones, Boton{ID: id, Texto: texto, Tipo: "otro"})
			}
		}
		if len(r.Botones) == 0 && len(r.Secciones) == 0 {
			return nil
		}
		return r
	case msg.GetProductMessage() != nil:
		p := msg.GetProductMessage().GetProduct()
		r := &Botones{Tipo: "producto", Titulo: p.GetTitle(), Pie: msg.GetProductMessage().GetFooter()}
		if u := p.GetURL(); u != "" {
			r.Botones = append(r.Botones, Boton{ID: "url", Texto: "View product", Tipo: "url", URL: u})
		}
		return r
	case msg.GetOrderMessage() != nil:
		return &Botones{Tipo: "pedido"}
	}
	return nil
}

func precio(moneda string, milesimas int64) string {
	if milesimas == 0 {
		return ""
	}
	return fmt.Sprintf("%s %.2f", moneda, float64(milesimas)/1000)
}

// Guarda lo interactivo del mensaje recien insertado (si lo es).
func guardarBotones(chat, id string, msg *waE2E.Message) {
	b := botonesDeMensaje(msg)
	if b == nil {
		return
	}
	datos, _ := json.Marshal(b)
	crudo, _ := proto.Marshal(msg)
	if _, err := db.Exec(`INSERT INTO botones (chat, mensaje, datos, proto) VALUES (?, ?, ?, ?)
		ON DUPLICATE KEY UPDATE datos = VALUES(datos), proto = VALUES(proto)`, chat, id, string(datos), crudo); err != nil {
		log.Printf("botones: %v", err)
	}
}

// Los botones de varios mensajes de un chat, por id.
func botonesDe(chat string, ids []string) map[string]*Botones {
	res := map[string]*Botones{}
	if len(ids) == 0 {
		return res
	}
	args := make([]any, 0, len(ids)+1)
	args = append(args, chat)
	marcas := ""
	for i, id := range ids {
		if i > 0 {
			marcas += ","
		}
		marcas += "?"
		args = append(args, id)
	}
	filas, err := db.Query("SELECT mensaje, datos FROM botones WHERE chat = ? AND mensaje IN ("+marcas+")", args...)
	if err != nil {
		return res
	}
	defer filas.Close()
	for filas.Next() {
		var id, datos string
		if filas.Scan(&id, &datos) == nil {
			var b Botones
			if json.Unmarshal([]byte(datos), &b) == nil {
				res[id] = &b
			}
		}
	}
	return res
}

// POST /boton {chat, id, boton | fila}: responde a un mensaje interactivo
// como lo haria el telefono, y anota la respuesta como mensaje propio.
func hBoton(w http.ResponseWriter, r *http.Request) {
	var p struct {
		Chat  string `json:"chat"`
		ID    string `json:"id"`
		Boton string `json:"boton"`
		Fila  string `json:"fila"`
	}
	if err := leerJSON(r, &p); err != nil {
		fallar(w, 400, err)
		return
	}
	j, ok := jidDe(p.Chat)
	orig := mensajePorID(p.Chat, p.ID)
	if !ok || orig == nil {
		fallar(w, 404, errors.New("message not found"))
		return
	}
	var datos string
	var crudo []byte
	if err := db.QueryRow("SELECT datos, proto FROM botones WHERE chat = ? AND mensaje = ?", p.Chat, p.ID).Scan(&datos, &crudo); err != nil {
		fallar(w, 404, errors.New("not an interactive message"))
		return
	}
	var b Botones
	json.Unmarshal([]byte(datos), &b)
	original := &waE2E.Message{}
	if err := proto.Unmarshal(crudo, original); err != nil {
		fallar(w, 500, err)
		return
	}
	ctxInfo := &waE2E.ContextInfo{
		StanzaID:      proto.String(p.ID),
		Participant:   proto.String(orig.Remitente),
		QuotedMessage: original,
	}
	var msg *waE2E.Message
	texto := ""
	if p.Fila != "" {
		// Una fila de la lista.
		var fila *FilaLista
		for _, s := range b.Secciones {
			for k := range s.Filas {
				if s.Filas[k].ID == p.Fila {
					fila = &s.Filas[k]
				}
			}
		}
		if fila == nil {
			fallar(w, 400, errors.New("unknown row"))
			return
		}
		texto = fila.Titulo
		if b.Tipo == "interactivo" {
			params, _ := json.Marshal(map[string]string{"id": fila.ID, "title": fila.Titulo, "description": fila.Descripcion})
			msg = &waE2E.Message{InteractiveResponseMessage: &waE2E.InteractiveResponseMessage{
				Body:        &waE2E.InteractiveResponseMessage_Body{Text: proto.String(fila.Titulo), Format: waE2E.InteractiveResponseMessage_Body_DEFAULT.Enum()},
				ContextInfo: ctxInfo,
				InteractiveResponseMessage: &waE2E.InteractiveResponseMessage_NativeFlowResponseMessage_{NativeFlowResponseMessage: &waE2E.InteractiveResponseMessage_NativeFlowResponseMessage{
					Name: proto.String("menu_options"), ParamsJSON: proto.String(string(params)), Version: proto.Int32(1)}},
			}}
		} else {
			msg = &waE2E.Message{ListResponseMessage: &waE2E.ListResponseMessage{
				Title:             proto.String(fila.Titulo),
				Description:       proto.String(fila.Descripcion),
				ListType:          waE2E.ListResponseMessage_SINGLE_SELECT.Enum(),
				SingleSelectReply: &waE2E.ListResponseMessage_SingleSelectReply{SelectedRowID: proto.String(fila.ID)},
				ContextInfo:       ctxInfo,
			}}
		}
	} else {
		var boton *Boton
		indice := 0
		for k := range b.Botones {
			if b.Botones[k].ID == p.Boton {
				boton = &b.Botones[k]
				indice = k
			}
		}
		if boton == nil || boton.Tipo != "responder" {
			fallar(w, 400, errors.New("unknown button"))
			return
		}
		texto = boton.Texto
		switch b.Tipo {
		case "botones":
			msg = &waE2E.Message{ButtonsResponseMessage: &waE2E.ButtonsResponseMessage{
				SelectedButtonID: proto.String(boton.ID),
				Response:         &waE2E.ButtonsResponseMessage_SelectedDisplayText{SelectedDisplayText: boton.Texto},
				Type:             waE2E.ButtonsResponseMessage_DISPLAY_TEXT.Enum(),
				ContextInfo:      ctxInfo,
			}}
		case "plantilla":
			msg = &waE2E.Message{TemplateButtonReplyMessage: &waE2E.TemplateButtonReplyMessage{
				SelectedID:          proto.String(boton.ID),
				SelectedDisplayText: proto.String(boton.Texto),
				SelectedIndex:       proto.Uint32(uint32(indice)),
				ContextInfo:         ctxInfo,
			}}
		default: // interactivo
			params, _ := json.Marshal(map[string]string{"display_text": boton.Texto, "id": boton.ID})
			msg = &waE2E.Message{InteractiveResponseMessage: &waE2E.InteractiveResponseMessage{
				Body:        &waE2E.InteractiveResponseMessage_Body{Text: proto.String(boton.Texto), Format: waE2E.InteractiveResponseMessage_Body_DEFAULT.Enum()},
				ContextInfo: ctxInfo,
				InteractiveResponseMessage: &waE2E.InteractiveResponseMessage_NativeFlowResponseMessage_{NativeFlowResponseMessage: &waE2E.InteractiveResponseMessage_NativeFlowResponseMessage{
					Name: proto.String("quick_reply"), ParamsJSON: proto.String(string(params)), Version: proto.Int32(1)}},
			}}
		}
	}
	resp, err := cli.SendMessage(ctx, j, msg)
	if err != nil {
		fallar(w, http.StatusBadGateway, err)
		return
	}
	b.Respondido = texto
	nuevos, _ := json.Marshal(&b)
	db.Exec("UPDATE botones SET datos = ? WHERE chat = ? AND mensaje = ?", string(nuevos), p.Chat, p.ID)
	// La respuesta queda como un mensaje nuestro, citando el original.
	c := contenido{tipo: "texto", texto: texto}
	nuevo := anotarEnviado(p.Chat, resp, c, &waE2E.ContextInfo{StanzaID: proto.String(p.ID), Participant: proto.String(orig.Remitente),
		QuotedMessage: &waE2E.Message{Conversation: proto.String(orig.Texto)}}, "")
	responder(w, nuevo)
}
