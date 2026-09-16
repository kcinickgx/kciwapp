# Publica el cliente en el release "current" de GitHub (kcinickgx/kciwapp), de
# donde el setup y el "check for updates" bajan todo. La fuente es la
# instalacion del usuario (C:\Program Files\KciWAPP: mpv, whisper, ffmpeg,
# fondos...; solo se lee) con el exe y el core recien compilados de build\.
#
# Sube como assets (nombre plano: mpv__libmpv-2.dll) solo lo que cambio
# respecto del manifiesto publicado, borra los assets que sobran y al final
# sube el manifiesto kciwapp.md5 ("md5 tamano url ruta" por linea). El modelo
# de whisper no se sube: su URL apunta al repo oficial en Hugging Face. No
# lleva lo que es del usuario (ajustes.json, cuentas.json, datos\) ni los pdb.
# Ademas sube kciwapp-setup.exe, INSTALL.md y, del server, el binario
# estatico (build\server\kciwapp-server, de core/build.sh server) y los
# install*.sh, fuera del manifiesto.
#
#   python publicar.py        (hace falta `gh auth login` hecho)
import hashlib
import json
import os
import shutil
import subprocess
import sys
import tempfile
import urllib.request

RAIZ = os.path.dirname(os.path.abspath(__file__))
ORIGEN = r'C:\Program Files\KciWAPP'  # la instalacion del usuario: solo se lee
REPO = 'kcinickgx/kciwapp'
TAG = 'current'
BASE = f'https://github.com/{REPO}/releases/download/{TAG}/'
MANIFIESTO = 'kciwapp.md5'
INSTALADOR = 'kciwapp-setup.exe'
EXCLUIR = {'ajustes.json', 'cuentas.json', 'servidor.json', 'debug.log', 'kciwapp2.pdb', 'kciwapp2.ilk', 'INSTALL.md', MANIFIESTO, INSTALADOR}
EXCLUIR_DIR = {'datos', 'server'}
# Lo recien compilado se toma de build\ (en la instalacion no se escribe
# nada: el usuario la actualiza a mano). Estos van aunque alla no esten.
FUENTES = {'kciwapp2.exe': os.path.join(RAIZ, 'build', 'kciwapp2.exe'),
           'core/kciwapp-core.exe': os.path.join(RAIZ, 'build', 'core', 'kciwapp-core.exe')}
# Archivos que se bajan de otro lado (no se suben al release).
EXTERNOS = {'whisper/ggml-large-v3-turbo.bin': 'https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-large-v3-turbo.bin'}
# Assets del release que no son del manifiesto pero van igual.
EXTRAS = [(INSTALADOR, os.path.join(RAIZ, 'build', INSTALADOR)),
          ('INSTALL.md', os.path.join(RAIZ, 'docs', 'INSTALL.md')),
          ('kciwapp-server', os.path.join(RAIZ, 'build', 'server', 'kciwapp-server')),
          ('install.sh', os.path.join(RAIZ, 'core', 'install.sh')),
          ('install-slackware.sh', os.path.join(RAIZ, 'core', 'install-slackware.sh'))]
CACHE = os.path.join(RAIZ, '.md5-cache.json')


def md5_de(ruta):
    h = hashlib.md5()
    with open(ruta, 'rb') as f:
        for trozo in iter(lambda: f.read(1 << 20), b''):
            h.update(trozo)
    return h.hexdigest()


def archivos_de(base):
    for d, carpetas, fs in os.walk(base):
        carpetas[:] = [c for c in carpetas if c not in EXCLUIR_DIR]
        for f in fs:
            if f in EXCLUIR or f.endswith(('.viejo', '.nuevo')):
                continue
            yield os.path.relpath(os.path.join(d, f), base).replace('\\', '/')


def asset_de(rel):
    return rel.replace('/', '__')


def gh(*args, **kw):
    return subprocess.run(['gh', *args], capture_output=True, text=True, encoding='utf-8', **kw)


def main():
    if not os.path.isdir(ORIGEN) or len(list(archivos_de(ORIGEN))) < 20:
        print(f'no esta la instalacion en {ORIGEN}: no se publica nada')
        return 1
    try:
        cache = json.load(open(CACHE))
    except Exception:
        cache = {}
    # md5 de lo local, con cache por tamano+mtime (whisper son 2,7 GB).
    lista = []
    for rel in sorted(set(archivos_de(ORIGEN)) | {k for k, v in FUENTES.items() if os.path.exists(v)}):
        ruta = FUENTES.get(rel, os.path.join(ORIGEN, rel))
        st = os.stat(ruta)
        c = cache.get(rel)
        if not c or c['tamano'] != st.st_size or c['mtime'] != st.st_mtime_ns:
            c = {'md5': md5_de(ruta), 'tamano': st.st_size, 'mtime': st.st_mtime_ns}
            cache[rel] = c
        lista.append((rel, c['md5'], st.st_size, ruta))
    json.dump(cache, open(CACHE, 'w'), indent=1)
    # El modelo externo: que el tamano coincida con el de Hugging Face
    # (el cliente verifica el md5 del local, asi que tienen que ser el mismo).
    for rel, url in EXTERNOS.items():
        tam = next((t for r, _, t, _ in lista if r == rel), None)
        if tam is None:
            continue
        with urllib.request.urlopen(urllib.request.Request(url, method='HEAD')) as r:
            remoto = int(r.headers.get('Content-Length', 0))
        if remoto != tam:
            print(f'{rel}: el local mide {tam} y el de Hugging Face {remoto}: no coinciden, no se publica')
            return 1
    # El release y lo que ya tiene.
    if gh('release', 'view', TAG, '-R', REPO).returncode != 0:
        r = gh('release', 'create', TAG, '-R', REPO, '--title', 'kciwapp', '--notes',
               'Rolling release: the setup and the in-app update check download from here. See INSTALL.md.', '--latest')
        if r.returncode != 0:
            print(r.stderr)
            return 1
    r = gh('release', 'view', TAG, '-R', REPO, '--json', 'assets')
    assets = {a['name'] for a in json.loads(r.stdout)['assets']} if r.returncode == 0 else set()
    publicado = {}
    try:
        with urllib.request.urlopen(BASE + MANIFIESTO) as r:
            for linea in r.read().decode('utf-8').splitlines():
                partes = linea.split(' ', 3)
                if len(partes) == 4:
                    publicado[partes[3]] = partes[0]
    except Exception:
        pass
    # Subir lo que cambio, con el nombre plano.
    tmp = tempfile.mkdtemp(prefix='kciwapp-pub-')
    subidos = 0
    try:
        for rel, md5, tamano, ruta in lista:
            if rel in EXTERNOS:
                continue
            nombre = asset_de(rel)
            if publicado.get(rel) == md5 and nombre in assets:
                continue
            copia = os.path.join(tmp, nombre)
            shutil.copyfile(ruta, copia)
            print(f'subiendo {rel} ({tamano // 1024} KB)')
            r = gh('release', 'upload', TAG, copia, '-R', REPO, '--clobber')
            os.remove(copia)
            if r.returncode != 0:
                print(r.stderr)
                return 1
            subidos += 1
        for nombre, ruta in EXTRAS:
            if not os.path.exists(ruta):
                print(f'(sin {ruta}, no se sube)')
                continue
            copia = os.path.join(tmp, nombre)
            shutil.copyfile(ruta, copia)
            r = gh('release', 'upload', TAG, copia, '-R', REPO, '--clobber')
            os.remove(copia)
            if r.returncode != 0:
                print(r.stderr)
                return 1
        # Assets que sobran.
        nuestros = {asset_de(rel) for rel, _, _, _ in lista if rel not in EXTERNOS} | {n for n, _ in EXTRAS} | {MANIFIESTO}
        borrados = 0
        for a in sorted(assets - nuestros):
            print(f'borrando asset {a}')
            gh('release', 'delete-asset', TAG, a, '-R', REPO, '-y')
            borrados += 1
        # El manifiesto al final, cuando ya esta todo.
        texto = ''.join(f'{md5} {tamano} {EXTERNOS.get(rel, BASE + asset_de(rel))} {rel}\n' for rel, md5, tamano, _ in lista)
        copia = os.path.join(tmp, MANIFIESTO)
        with open(copia, 'w', encoding='utf-8', newline='\n') as f:
            f.write(texto)
        r = gh('release', 'upload', TAG, copia, '-R', REPO, '--clobber')
        if r.returncode != 0:
            print(r.stderr)
            return 1
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    total = sum(t for _, _, t, _ in lista)
    print(f'{len(lista)} archivos ({total // (1 << 20)} MB), {subidos} subidos, {borrados} borrados -> {BASE}{MANIFIESTO}')


if __name__ == '__main__':
    sys.exit(main())
