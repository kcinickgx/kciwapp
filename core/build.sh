#!/bin/bash
# El core en Go es uno solo para los dos usos; se compila en la VM (no hay Go
# en Windows). Desde Git Bash en Windows:
#   core/build.sh windows   -> portable/core/kciwapp-core.exe (cliente local, SQLite)
#   core/build.sh server    -> binario estatico Linux en release/server/ y deploy en la VM
set -e
cd "$(dirname "$0")"
VM=root@192.168.5.15
ssh -p 22122 $VM 'mkdir -p /root/kciwapp-server'
scp -P 22122 -q *.go go.mod go.sum $VM:/root/kciwapp-server/
case "${1:-windows}" in
  windows)
    ssh -p 22122 $VM 'cd /root/kciwapp-server && go mod tidy >/dev/null 2>&1; GOOS=windows GOARCH=amd64 CGO_ENABLED=0 go build -ldflags "-s -w -H windowsgui" -o /tmp/kciwapp-core.exe .'
    mkdir -p ../release/cliente/core
    scp -P 22122 -q $VM:/tmp/kciwapp-core.exe ../release/cliente/core/
    echo "release/cliente/core/kciwapp-core.exe listo"
    ;;
  server)
    ssh -p 22122 $VM 'cd /root/kciwapp-server && go mod tidy >/dev/null 2>&1; CGO_ENABLED=0 go build -ldflags "-s -w" -o /tmp/kciwapp-server-static . && go build -o /tmp/kciwapp-server . && systemctl stop kciwapp-server && cp /tmp/kciwapp-server /opt/kciwapp-server/kciwapp-server && systemctl start kciwapp-server && sleep 3 && systemctl is-active kciwapp-server && git add -A && git commit -qm "build desde kciwapp2/core" || true'
    mkdir -p ../release/server
    scp -P 22122 -q $VM:/tmp/kciwapp-server-static ../release/server/kciwapp-server
    echo "VM actualizada; release/server/kciwapp-server (estatico) listo"
    ;;
esac
