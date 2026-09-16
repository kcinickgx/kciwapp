#!/bin/bash
# El core en Go es uno solo para los dos usos; se compila en la VM (no hay Go
# en Windows). Desde Git Bash en Windows:
#   core/build.sh windows   -> build/core/kciwapp-core.exe (cliente local, SQLite); publicar.py lo sube
#   core/build.sh server    -> deploy en la VM y binario estatico Linux en build/server/ (publicar.py lo sube)
#   core/build.sh actualizar -> sube whatsmeow (y el resto) a la ultima version: go.mod/go.sum
#                              vuelven actualizados; despues build.sh server y windows
set -e
cd "$(dirname "$0")"
VM=root@192.168.5.15
ssh -p 22122 $VM 'mkdir -p /root/kciwapp-server'
scp -P 22122 -q *.go go.mod go.sum $VM:/root/kciwapp-server/
case "${1:-windows}" in
  windows)
    ssh -p 22122 $VM 'cd /root/kciwapp-server && go mod tidy >/dev/null 2>&1; GOOS=windows GOARCH=amd64 CGO_ENABLED=0 go build -ldflags "-s -w -H windowsgui" -o /tmp/kciwapp-core.exe .'
    mkdir -p ../build/core
    scp -P 22122 -q $VM:/tmp/kciwapp-core.exe ../build/core/
    echo "build/core/kciwapp-core.exe listo (publicar.py lo sube a H:)"
    ;;
  actualizar)
    ssh -p 22122 $VM 'cd /root/kciwapp-server && go get -u go.mau.fi/whatsmeow@latest && go get -u ./... && go mod tidy && go build -o /dev/null . && grep whatsmeow go.mod'
    scp -P 22122 -q $VM:/root/kciwapp-server/go.mod $VM:/root/kciwapp-server/go.sum .
    echo "go.mod actualizado; ahora: core/build.sh server && core/build.sh windows"
    ;;
  server)
    ssh -p 22122 $VM 'cd /root/kciwapp-server && go mod tidy >/dev/null 2>&1; CGO_ENABLED=0 go build -ldflags "-s -w" -o /tmp/kciwapp-server-static . && go build -o /tmp/kciwapp-server . && systemctl stop kciwapp-server && cp /tmp/kciwapp-server /opt/kciwapp-server/kciwapp-server && systemctl start kciwapp-server && sleep 3 && systemctl is-active kciwapp-server && git add -A && git commit -qm "build desde kciwapp2/core" || true'
    mkdir -p ../build/server
    scp -P 22122 -q $VM:/tmp/kciwapp-server-static ../build/server/kciwapp-server
    echo "VM actualizada; build/server/kciwapp-server (estatico) listo (publicar.py lo sube)"
    ;;
esac
