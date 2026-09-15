package main

// Modo multi-cuenta: este proceso no habla con WhatsApp; escucha en el
// puerto publico y rutea cada request, por su X-Token, a un proceso hijo
// (el server normal, en 127.0.0.1:puerto) que tiene su propia base,
// su store.db y su carpeta de media. Un token desconocido crea la cuenta:
// se le arma la base y el config, se levanta el hijo, y el cliente ve el QR.
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

	proxy   *httputil.ReverseProxy `json:"-"`
	lista   bool                   `json:"-"` // el hijo ya respondio /estado
	proceso *os.Process            `json:"-"`
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

// El config del hijo: el server normal con sus rutas.
func escribirConfigHijo(c *cuentaMulti) string {
	dir := filepath.Join(cfg.Dir, "cuentas", fmt.Sprint(c.ID))
	os.MkdirAll(c.Media, 0o755)
	os.MkdirAll(dir, 0o755)
	ruta := filepath.Join(dir, "config.json")
	conf := map[string]any{
		"escucha":        fmt.Sprintf("127.0.0.1:%d", c.Puerto),
		"token":          c.Token,
		"mariadb":        fmt.Sprintf(cfg.MariaDB, c.DB),
		"store":          c.Store,
		"media":          c.Media,
		"historia":       cfg.Historia,
		"bajar_historia": false,
	}
	crudo, _ := json.MarshalIndent(conf, "", "  ")
	os.WriteFile(ruta, crudo, 0o600)
	return ruta
}

// Lanza el hijo y lo relanza si se cae.
func supervisarHijo(c *cuentaMulti) {
	ruta := escribirConfigHijo(c)
	exe, _ := os.Executable()
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
		log.Printf("[%d] hijo pid %d en 127.0.0.1:%d (db %s)", c.ID, cmd.Process.Pid, c.Puerto, c.DB)
		err := cmd.Wait()
		c.lista = false
		c.proceso = nil
		if multiCerrando {
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
	id := 1
	puerto := 9000
	for _, c := range multiCuentas {
		if c.ID >= id {
			id = c.ID + 1
		}
		if c.Puerto >= puerto {
			puerto = c.Puerto
		}
	}
	puerto++
	nombreDB := fmt.Sprintf("whatsapp_%d", id)
	admin, err := sql.Open("mysql", fmt.Sprintf(cfg.MariaDB, ""))
	if err != nil {
		return nil, err
	}
	defer admin.Close()
	if _, err := admin.Exec("CREATE DATABASE IF NOT EXISTS `" + nombreDB + "` CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci"); err != nil {
		return nil, fmt.Errorf("crear base: %w", err)
	}
	dir := filepath.Join(cfg.Dir, "cuentas", fmt.Sprint(id))
	c := &cuentaMulti{ID: id, Token: token, DB: nombreDB, Store: filepath.Join(dir, "store.db"), Media: filepath.Join(dir, "media"),
		Puerto: puerto, Creada: ahoraMs()}
	multiCuentas = append(multiCuentas, c)
	guardarCuentas()
	armarProxy(c)
	go supervisarHijo(c)
	log.Printf("cuenta nueva %d para el token %s...", id, token[:6])
	return c, nil
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
	for _, c := range multiCuentas {
		armarProxy(c)
		go supervisarHijo(c)
	}
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
