#!/bin/bash
# Compila el core para Windows (sin cgo: modernc/sqlite) en la VM con Go,
# copiando los fuentes de esta carpeta. Uso desde Windows (Git Bash):
#   ssh -p 22122 root@192.168.5.15 'mkdir -p /root/kciwapp-core' && scp -P 22122 core/*.go core/go.mod core/go.sum root@192.168.5.15:/root/kciwapp-core/
#   ssh -p 22122 root@192.168.5.15 'cd /root/kciwapp-core && GOOS=windows GOARCH=amd64 CGO_ENABLED=0 go build -ldflags "-s -w -H windowsgui" -o /tmp/kciwapp-core.exe .'
#   scp -P 22122 root@192.168.5.15:/tmp/kciwapp-core.exe portable/core/
set -e
cd "$(dirname "$0")"
ssh -p 22122 root@192.168.5.15 'mkdir -p /root/kciwapp-core'
scp -P 22122 -q *.go go.mod go.sum root@192.168.5.15:/root/kciwapp-core/
ssh -p 22122 root@192.168.5.15 'cd /root/kciwapp-core && go mod tidy >/dev/null 2>&1; GOOS=windows GOARCH=amd64 CGO_ENABLED=0 go build -ldflags "-s -w -H windowsgui" -o /tmp/kciwapp-core.exe .'
scp -P 22122 -q root@192.168.5.15:/tmp/kciwapp-core.exe ../portable/core/
echo "portable/core/kciwapp-core.exe listo"
