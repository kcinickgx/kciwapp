// kciwapp-server: habla WhatsApp con whatsmeow, guarda todo en MariaDB y
// se lo sirve al cliente por HTTP/JSON con un log de eventos por cursor.
package main

import (
	"context"
	"database/sql"
	"encoding/json"
	"log"
	"os"
	"path/filepath"
	"os/signal"
	"sync/atomic"
	"syscall"

	_ "github.com/go-sql-driver/mysql"
	_ "modernc.org/sqlite"
	"go.mau.fi/whatsmeow"
	"go.mau.fi/whatsmeow/proto/waCompanionReg"
	"go.mau.fi/whatsmeow/store"
	"go.mau.fi/whatsmeow/store/sqlstore"
	waLog "go.mau.fi/whatsmeow/util/log"
	"google.golang.org/protobuf/proto"
)

type Config struct {
	Escucha string `json:"escucha"`
	Token   string `json:"token"`
	MariaDB string `json:"mariadb"`
	Store   string `json:"store"`
	Media   string `json:"media"`
	// Si se bajan de a poco los adjuntos que llegan por history sync (los
	// nuevos se bajan siempre).
	BajarHistoria bool `json:"bajar_historia"`
	// Que historia pedir al vincular: "reciente" (lo mismo que WhatsApp Web:
	// los chats recientes), "completa" (hasta 3 anos, texto) o "no".
	Historia string `json:"historia"`
	// Base en SQLite (el core local del cliente): ruta del archivo. Si esta,
	// no se usa mariadb.
	SQLite string `json:"sqlite"`
	// Modo multi-cuenta (ver multi.go): dir base; mariadb con %s para el
	// nombre de la base de cada cuenta.
	Multi bool   `json:"multi"`
	Dir   string `json:"dir"`
}

var (
	cfg Config
	db  *baseDatos
	cli *whatsmeow.Client
	ctx = context.Background()
	// El ultimo QR generado mientras no hay sesion; vacio si no hace falta.
	qrActual atomic.Value
	// Si esta corriendo el ciclo del QR (lo pide el cliente con /vincular).
	vinculando atomic.Bool
	// Si estamos conectados al servidor de WhatsApp ahora mismo.
	conectado atomic.Bool
)

func main() {
	log.SetFlags(log.Ltime)
	// ffmpeg/ffprobe al lado del exe, si estan ahi.
	if exe, err := os.Executable(); err == nil {
		os.Setenv("PATH", filepath.Dir(exe)+string(os.PathListSeparator)+os.Getenv("PATH"))
	}
	// Config: el argumento, o KCIWAPP_CONFIG (para los modos CLI contra
	// una cuenta del modo multi), o el default.
	ruta := "/opt/kciwapp-server/config.json"
	if v := os.Getenv("KCIWAPP_CONFIG"); v != "" {
		ruta = v
	}
	if len(os.Args) > 1 && os.Args[1] != "importar" && os.Args[1] != "adjuntar" && os.Args[1] != "backup" {
		ruta = os.Args[1]
	}
	crudo, err := os.ReadFile(ruta)
	if err != nil {
		log.Fatalf("config: %v", err)
	}
	cfg.BajarHistoria = true
	cfg.Historia = "reciente"
	if err := json.Unmarshal(crudo, &cfg); err != nil {
		log.Fatalf("config: %v", err)
	}
	if cfg.Multi {
		multiCLI()
		return
	}
	if cfg.Historia != "reciente" && cfg.Historia != "completa" && cfg.Historia != "no" {
		log.Fatalf("config: historia debe ser reciente, completa o no")
	}
	qrActual.Store("")

	if cfg.SQLite != "" {
		// WAL y busy_timeout: varios hilos leen y escriben la misma base.
		os.MkdirAll(filepath.Dir(cfg.SQLite), 0o755)
		bd, err := sql.Open("sqlite", "file:"+cfg.SQLite+"?_pragma=busy_timeout(10000)&_pragma=journal_mode(WAL)&_pragma=synchronous(NORMAL)&_pragma=foreign_keys(0)")
		if err != nil {
			log.Fatalf("sqlite: %v", err)
		}
		bd.SetMaxOpenConns(4)
		if err := bd.Ping(); err != nil {
			log.Fatalf("sqlite: %v", err)
		}
		db = &baseDatos{DB: bd, sqlite: true}
	} else {
		bd, err := sql.Open("mysql", cfg.MariaDB)
		if err != nil {
			log.Fatalf("mariadb: %v", err)
		}
		bd.SetMaxOpenConns(8)
		if err := bd.Ping(); err != nil {
			log.Fatalf("mariadb: %v", err)
		}
		db = &baseDatos{DB: bd}
	}
	crearTablas()
	if err := os.MkdirAll(cfg.Media, 0o755); err != nil {
		log.Fatalf("media: %v", err)
	}

	// Como se presenta el dispositivo companero, y que historia pide al
	// vincular: la reciente (como WhatsApp Web) o la completa (tres anos es
	// el tope que da WhatsApp).
	store.DeviceProps.Os = proto.String("kciwapp")
	store.DeviceProps.RequireFullSync = proto.Bool(cfg.Historia == "completa")
	if cfg.Historia == "completa" {
		store.DeviceProps.HistorySyncConfig = &waCompanionReg.DeviceProps_HistorySyncConfig{
			FullSyncDaysLimit:   proto.Uint32(1095),
			FullSyncSizeMbLimit: proto.Uint32(50000),
			StorageQuotaMb:      proto.Uint32(50000),
		}
	}

	contenedor, err := sqlstore.New(ctx, "sqlite", "file:"+cfg.Store+"?_pragma=foreign_keys(1)&_pragma=busy_timeout(10000)", waLog.Stdout("Store", "WARN", true))
	if err != nil {
		log.Fatalf("store: %v", err)
	}
	dispositivo, err := contenedor.GetFirstDevice(ctx)
	if err != nil {
		log.Fatalf("store: %v", err)
	}
	cli = whatsmeow.NewClient(dispositivo, waLog.Stdout("WA", "INFO", true))
	// Modo importar: solo la base, sin conectarse a WhatsApp.
	if len(os.Args) > 1 && os.Args[1] == "importar" {
		importarCLI(os.Args[2:])
		return
	}
	if len(os.Args) > 1 && os.Args[1] == "adjuntar" {
		adjuntarCLI(os.Args[2:])
		return
	}
	if len(os.Args) > 1 && os.Args[1] == "backup" {
		backupCLI(os.Args[2:])
		return
	}
	cli.AddEventHandler(manejarEvento)

	go servirHTTP()
	go bajadorPendientes()
	go buscadorFotos()
	go rellenarOndas()
	go rellenarMiniaturas()

	if cli.Store.ID == nil {
		// Sin sesion: no se hace nada hasta que un cliente pida vincular
		// (POST /vincular); ahi arranca el ciclo del QR.
		log.Printf("sin vincular: esperando que el cliente pida el QR")
	} else {
		log.Printf("sesion de %s", cli.Store.ID)
		if err := cli.Connect(); err != nil {
			log.Fatalf("connect: %v", err)
		}
	}

	senal := make(chan os.Signal, 1)
	signal.Notify(senal, os.Interrupt, syscall.SIGTERM)
	<-senal
	log.Printf("cerrando")
	cli.Disconnect()
}

// Ciclo del QR: se pide a WhatsApp un canal de codigos y se guarda el
// ultimo en qrActual hasta que alguien escanee (o el canal se cierre a los
// ~3 minutos; entonces se corta y el cliente vuelve a pedir).
func empezarVinculacion() {
	if cli.Store.ID != nil || !vinculando.CompareAndSwap(false, true) {
		return
	}
	go func() {
		defer vinculando.Store(false)
		defer qrActual.Store("")
		canal, err := cli.GetQRChannel(ctx)
		if err != nil {
			log.Printf("qr: %v", err)
			return
		}
		if err := cli.Connect(); err != nil {
			log.Printf("connect: %v", err)
			return
		}
		for item := range canal {
			if item.Event == "code" {
				qrActual.Store(item.Code)
				log.Printf("QR nuevo")
			} else {
				qrActual.Store("")
				log.Printf("QR: %s %v", item.Event, item.Error)
			}
		}
		if cli.Store.ID == nil {
			cli.Disconnect()
			log.Printf("nadie escaneo; el cliente puede volver a pedir el QR")
		}
	}()
}
