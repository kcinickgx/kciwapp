package main

// Modo multi-cuenta: este proceso no habla con WhatsApp; escucha en el
// puerto publico y rutea cada request, por su X-Token, a un proceso hijo
// (el server normal, en 127.0.0.1:puerto) que tiene su propia base,
// su store.db y su carpeta de media. Un token desconocido levanta un hijo
// PROVISORIO en <dir>/provisorias/<puerto>/ con SQLite (nada en MariaDB ni
// en cuentas/) para que el cliente vea el QR; recien cuando WhatsApp queda
// vinculado se crea la cuenta de verdad (base, carpeta, cuentas.json), se
// muda el store.db con la sesion y el hijo arranca en serio. Un provisorio
// que nadie usa por 5 minutos se borra: generar tokens no deja basura.
//
// Config (multi.json):
//   {"multi": true, "escucha": ":8080", "dir": "/opt/kciwapp-server",
//    "mariadb": "wa:PASS@tcp(127.0.0.1:3306)/%s?parseTime=true&charset=utf8mb4",
//    "historia": "reciente"}
// Las cuentas quedan en <dir>/cuentas.json y cada hijo en <dir>/cuentas/<id>/.

import (
	"context"
	"database/sql"
	"encoding/json"
	"fmt"
	"log"
	"net/http"
	"net/http/httputil"
	"net/url"
	"os"
	"os/exec"
	"os/signal"
	"path/filepath"
	"sort"
	"strings"
	"sync"
	"syscall"
	"time"
)

type cuentaMulti struct {
	ID     int    `json:"id"`
	Token  string `json:"token"`
	DB     string `json:"db"`
	Store  string `json:"store"`
	Media  string `json:"media"`
	Puerto int    `json:"puerto"`
	Creada int64  `json:"creada"`

	proxy      *httputil.ReverseProxy `json:"-"`
	lista      bool                   `json:"-"` // el hijo ya respondio /estado
	proceso    *os.Process            `json:"-"`
	provisoria bool                   `json:"-"` // todavia sin vincular: SQLite en tmp, fuera de cuentas.json
	tmp        string                 `json:"-"` // la carpeta del provisorio
	eliminada  bool                   `json:"-"` // se borro: el supervisor no la relanza
	gen        int                    `json:"-"` // sube al promover: el supervisor viejo se retira
	ultimoUso  time.Time              `json:"-"`
}

var (
	multiMu       sync.Mutex
	multiCuentas  []*cuentaMulti
	multiCerrando bool
)

func rutaCuentas() string { return filepath.Join(cfg.Dir, "cuentas.json") }

func guardarCuentas() {
	crudo, _ := json.MarshalIndent(multiCuentas, "", "  ")
	tmp := rutaCuentas() + ".tmp"
	os.WriteFile(tmp, crudo, 0o600)
	os.Rename(tmp, rutaCuentas())
}

// El config del hijo: el server normal con sus rutas. El provisorio va con
// SQLite en su carpeta temporal.
func escribirConfigHijo(c *cuentaMulti) string {
	dir := filepath.Join(cfg.Dir, "cuentas", fmt.Sprint(c.ID))
	if c.provisoria {
		dir = c.tmp
	}
	os.MkdirAll(c.Media, 0o755)
	os.MkdirAll(dir, 0o755)
	ruta := filepath.Join(dir, "config.json")
	conf := map[string]any{
		"escucha":        fmt.Sprintf("127.0.0.1:%d", c.Puerto),
		"token":          c.Token,
		"store":          c.Store,
		"media":          c.Media,
		"historia":       cfg.Historia,
		"bajar_historia": false,
	}
	if c.provisoria {
		conf["sqlite"] = filepath.Join(dir, "provisoria.sqlite3")
	} else {
		conf["mariadb"] = fmt.Sprintf(cfg.MariaDB, c.DB)
	}
	crudo, _ := json.MarshalIndent(conf, "", "  ")
	os.WriteFile(ruta, crudo, 0o600)
	return ruta
}

// Lanza el hijo y lo relanza si se cae.
func supervisarHijo(c *cuentaMulti) {
	ruta := escribirConfigHijo(c)
	exe, _ := os.Executable()
	gen := c.gen
	for {
		cmd := exec.Command(exe, ruta)
		cmd.Stdout = prefijo{fmt.Sprintf("[%d] ", c.ID)}
		cmd.Stderr = cmd.Stdout
		inicio := time.Now()
		if err := cmd.Start(); err != nil {
			log.Printf("[%d] no arranca: %v", c.ID, err)
			time.Sleep(10 * time.Second)
			continue
		}
		c.proceso = cmd.Process
		if c.provisoria {
			log.Printf("provisorio pid %d en 127.0.0.1:%d", cmd.Process.Pid, c.Puerto)
		} else {
			log.Printf("[%d] hijo pid %d en 127.0.0.1:%d (db %s)", c.ID, cmd.Process.Pid, c.Puerto, c.DB)
		}
		err := cmd.Wait()
		c.lista = false
		c.proceso = nil
		if multiCerrando || c.eliminada || c.gen != gen {
			return
		}
		log.Printf("[%d] hijo termino: %v", c.ID, err)
		if time.Since(inicio) < 5*time.Second {
			time.Sleep(10 * time.Second)
		} else {
			time.Sleep(2 * time.Second)
		}
	}
}

// Escribe las lineas del hijo en nuestro log, con su prefijo.
type prefijo struct{ p string }

func (p prefijo) Write(b []byte) (int, error) {
	for _, l := range strings.Split(strings.TrimRight(string(b), "\n"), "\n") {
		// El hijo ya pone la hora; se saca para no duplicarla.
		if len(l) > 9 && l[2] == ':' && l[5] == ':' && l[8] == ' ' {
			l = l[9:]
		}
		if l != "" {
			log.Print(p.p + l)
		}
	}
	return len(b), nil
}

func esperarHijo(c *cuentaMulti) bool {
	if c.lista {
		return true
	}
	cl := http.Client{Timeout: 2 * time.Second}
	for i := 0; i < 40; i++ {
		r, err := cl.Get(fmt.Sprintf("http://127.0.0.1:%d/estado", c.Puerto))
		if err == nil {
			r.Body.Close()
			if r.StatusCode == 200 {
				c.lista = true
				return true
			}
		}
		time.Sleep(500 * time.Millisecond)
	}
	return false
}

func armarProxy(c *cuentaMulti) {
	u, _ := url.Parse(fmt.Sprintf("http://127.0.0.1:%d", c.Puerto))
	c.proxy = httputil.NewSingleHostReverseProxy(u)
	c.proxy.FlushInterval = 100 * time.Millisecond
}

// Crea la cuenta de un token nuevo: base, config, hijo.
func crearCuenta(token string) (*cuentaMulti, error) {
	puerto := 9000
	for _, c := range multiCuentas {
		if c.Puerto >= puerto {
			puerto = c.Puerto
		}
	}
	puerto++
	tmp := filepath.Join(cfg.Dir, "provisorias", fmt.Sprint(puerto))
	os.RemoveAll(tmp)
	c := &cuentaMulti{Token: token, Store: filepath.Join(tmp, "store.db"), Media: filepath.Join(tmp, "media"),
		Puerto: puerto, Creada: ahoraMs(), provisoria: true, tmp: tmp, ultimoUso: time.Now()}
	multiCuentas = append(multiCuentas, c)
	armarProxy(c)
	go supervisarHijo(c)
	log.Printf("provisorio en :%d para el token %s... (sin cuenta hasta que se vincule)", puerto, token[:6])
	return c, nil
}

// El provisorio se vinculo: recien ahora la cuenta de verdad. Se para el
// hijo, se crea la base y la carpeta, se muda el store.db (ahi esta la
// sesion) y el hijo arranca con MariaDB. Con multiMu tomado.
func promoverCuenta(c *cuentaMulti) error {
	c.gen++
	if c.proceso != nil {
		c.proceso.Signal(syscall.SIGTERM)
	}
	for i := 0; i < 50 && c.proceso != nil; i++ {
		time.Sleep(100 * time.Millisecond)
	}
	id := 1
	for _, x := range multiCuentas {
		if !x.provisoria && x.ID >= id {
			id = x.ID + 1
		}
	}
	nombreDB := fmt.Sprintf("whatsapp_%d", id)
	admin, err := sql.Open("mysql", fmt.Sprintf(cfg.MariaDB, ""))
	if err != nil {
		return err
	}
	defer admin.Close()
	if _, err := admin.Exec("CREATE DATABASE IF NOT EXISTS `" + nombreDB + "` CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci"); err != nil {
		return fmt.Errorf("crear base: %w", err)
	}
	dir := filepath.Join(cfg.Dir, "cuentas", fmt.Sprint(id))
	os.MkdirAll(dir, 0o755)
	// store.db y sus -wal/-shm (ahi esta la sesion recien vinculada).
	store := filepath.Join(dir, "store.db")
	partes, _ := filepath.Glob(c.Store + "*")
	for _, p := range partes {
		if err := os.Rename(p, store+strings.TrimPrefix(p, c.Store)); err != nil {
			return fmt.Errorf("mudar %s: %w", filepath.Base(p), err)
		}
	}
	os.RemoveAll(c.tmp)
	c.ID, c.DB, c.Store, c.Media = id, nombreDB, store, filepath.Join(dir, "media")
	c.provisoria, c.lista, c.tmp = false, false, ""
	guardarCuentas()
	go supervisarHijo(c)
	log.Printf("[%d] vinculada: cuenta creada (db %s) para el token %s...", id, nombreDB, c.Token[:6])
	return nil
}

// El hijo dice si WhatsApp ya esta vinculado.
func hijoLogueado(c *cuentaMulti) bool {
	cl := http.Client{Timeout: 3 * time.Second}
	r, err := cl.Get(fmt.Sprintf("http://127.0.0.1:%d/estado", c.Puerto))
	if err != nil {
		return false
	}
	defer r.Body.Close()
	var e struct {
		Logueado bool `json:"logueado"`
	}
	json.NewDecoder(r.Body).Decode(&e)
	return e.Logueado
}

// Borra un provisorio: el hijo y su carpeta temporal. Con multiMu tomado.
func destruirProvisorio(c *cuentaMulti) {
	c.eliminada = true
	if c.proceso != nil {
		c.proceso.Signal(syscall.SIGTERM)
	}
	for i, x := range multiCuentas {
		if x == c {
			multiCuentas = append(multiCuentas[:i], multiCuentas[i+1:]...)
			break
		}
	}
	for i := 0; i < 50 && c.proceso != nil; i++ {
		time.Sleep(100 * time.Millisecond)
	}
	os.RemoveAll(c.tmp)
}

// Cada 3 s: el provisorio que se vinculo pasa a cuenta de verdad; el que
// nadie usa hace 5 minutos sin vincular, se borra.
func vigilarProvisorias() {
	for {
		time.Sleep(3 * time.Second)
		multiMu.Lock()
		for _, c := range append([]*cuentaMulti(nil), multiCuentas...) {
			if !c.provisoria || !c.lista {
				continue
			}
			if hijoLogueado(c) {
				if err := promoverCuenta(c); err != nil {
					log.Printf("provisorio :%d vinculado pero no se pudo crear la cuenta: %v", c.Puerto, err)
					destruirProvisorio(c)
				}
			} else if time.Since(c.ultimoUso) > 5*time.Minute {
				log.Printf("provisorio :%d sin vincular ni uso: se borra", c.Puerto)
				destruirProvisorio(c)
			}
		}
		multiMu.Unlock()
	}
}

// Al arrancar: bases whatsapp_N y carpetas cuentas/N que no son de ninguna
// cuenta (provisorias que quedaron de un cierre) se borran, si estan vacias.
func limpiarHuerfanas() {
	conocidas := map[string]bool{}
	for _, c := range multiCuentas {
		conocidas[c.DB] = true
		conocidas[fmt.Sprint(c.ID)] = true
	}
	admin, err := sql.Open("mysql", fmt.Sprintf(cfg.MariaDB, ""))
	if err != nil {
		return
	}
	defer admin.Close()
	filas, err := admin.Query("SHOW DATABASES LIKE 'whatsapp\\_%'")
	if err != nil {
		return
	}
	var huerfanas []string
	for filas.Next() {
		var n string
		if filas.Scan(&n) == nil && !conocidas[n] {
			huerfanas = append(huerfanas, n)
		}
	}
	filas.Close()
	for _, n := range huerfanas {
		var mensajes int
		if admin.QueryRow("SELECT COUNT(*) FROM `"+n+"`.mensajes").Scan(&mensajes) == nil && mensajes > 0 {
			log.Printf("base %s sin cuenta pero con %d mensajes: se deja", n, mensajes)
			continue
		}
		admin.Exec("DROP DATABASE IF EXISTS `" + n + "`")
		log.Printf("base %s sin cuenta: borrada", n)
	}
	entradas, _ := os.ReadDir(filepath.Join(cfg.Dir, "cuentas"))
	for _, e := range entradas {
		if e.IsDir() && !conocidas[e.Name()] {
			os.RemoveAll(filepath.Join(cfg.Dir, "cuentas", e.Name()))
			log.Printf("carpeta cuentas/%s sin cuenta: borrada", e.Name())
		}
	}
}

func multiHTTP(w http.ResponseWriter, r *http.Request) {
	t := r.Header.Get("X-Token")
	if t == "" {
		t = r.URL.Query().Get("token")
	}
	if t == "" {
		http.Error(w, "token", http.StatusUnauthorized)
		return
	}
	multiMu.Lock()
	var c *cuentaMulti
	for _, x := range multiCuentas {
		if x.Token == t {
			c = x
			break
		}
	}
	if c == nil {
		// Token nuevo: si es largo como para no ser un tanteo, es una cuenta.
		if len(t) < 20 {
			multiMu.Unlock()
			http.Error(w, "token", http.StatusUnauthorized)
			return
		}
		var err error
		c, err = crearCuenta(t)
		if err != nil {
			multiMu.Unlock()
			log.Printf("crear cuenta: %v", err)
			http.Error(w, "cannot create account", 500)
			return
		}
	}
	c.ultimoUso = time.Now()
	multiMu.Unlock()
	if !esperarHijo(c) {
		http.Error(w, "account starting, retry", http.StatusServiceUnavailable)
		return
	}
	c.proxy.ServeHTTP(w, r)
}

func multiCLI() {
	if cfg.Dir == "" || !strings.Contains(cfg.MariaDB, "%s") {
		log.Fatalf("multi: hacen falta dir y mariadb con %%s para el nombre de la base")
	}
	if cfg.Historia == "" {
		cfg.Historia = "reciente"
	}
	os.MkdirAll(filepath.Join(cfg.Dir, "cuentas"), 0o755)
	if crudo, err := os.ReadFile(rutaCuentas()); err == nil {
		json.Unmarshal(crudo, &multiCuentas)
	}
	sort.Slice(multiCuentas, func(i, j int) bool { return multiCuentas[i].ID < multiCuentas[j].ID })
	os.RemoveAll(filepath.Join(cfg.Dir, "provisorias"))
	limpiarHuerfanas()
	for _, c := range multiCuentas {
		armarProxy(c)
		go supervisarHijo(c)
	}
	go vigilarProvisorias()
	log.Printf("multi: %d cuentas, escuchando en %s", len(multiCuentas), cfg.Escucha)
	srv := &http.Server{Addr: cfg.Escucha, Handler: http.HandlerFunc(multiHTTP), ReadHeaderTimeout: 10 * time.Second}
	go func() {
		if err := srv.ListenAndServe(); err != nil && err != http.ErrServerClosed {
			log.Fatalf("http: %v", err)
		}
	}()
	senal := make(chan os.Signal, 1)
	signal.Notify(senal, os.Interrupt, syscall.SIGTERM)
	<-senal
	log.Printf("cerrando")
	multiMu.Lock()
	multiCerrando = true
	for _, c := range multiCuentas {
		if c.proceso != nil {
			c.proceso.Signal(syscall.SIGTERM)
		}
	}
	multiMu.Unlock()
	ctx2, cancel := context.WithTimeout(context.Background(), 3*time.Second)
	defer cancel()
	srv.Shutdown(ctx2)
	time.Sleep(2 * time.Second)
}
