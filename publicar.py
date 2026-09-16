# Publica el cliente en H:\kciwapp (= https://kcinick.gxzone.com/kciwapp/),
# de donde el cliente se actualiza solo. La fuente es la instalacion del
# usuario (C:\Program Files\KciWAPP: mpv, whisper, ffmpeg, fondos...; solo se
# lee) con el exe y el core recien compilados de build\; copia lo
# que cambio, borra lo que sobra y escribe el manifiesto kciwapp.md5 ("md5
# tamano ruta" por linea). No lleva lo que es del usuario (ajustes.json,
# cuentas.json, datos\) ni los pdb. Ademas deja INSTALAR.md y, en server\,
# los instaladores (el binario del server lo sube core/build.sh server).
#
#   python publicar.py
import hashlib
import json
import os
import shutil
import sys

RAIZ = os.path.dirname(os.path.abspath(__file__))
ORIGEN = r'C:\Program Files\KciWAPP'  # la instalacion del usuario: solo se lee
DESTINO = r'H:\kciwapp'
MANIFIESTO = 'kciwapp.md5'
INSTALADOR = 'kciwapp-instalar.exe'
EXCLUIR = {'ajustes.json', 'cuentas.json', 'servidor.json', 'debug.log', 'kciwapp2.pdb', 'kciwapp2.ilk', 'INSTALAR.md', MANIFIESTO, INSTALADOR}
EXCLUIR_DIR = {'datos', 'server'}
# Lo recien compilado se toma de build\ (en la instalacion no se escribe
# nada: el usuario la actualiza a mano). Estos van aunque alla no esten.
FUENTES = {'kciwapp2.exe': os.path.join(RAIZ, 'build', 'kciwapp2.exe'),
           'core/kciwapp-core.exe': os.path.join(RAIZ, 'build', 'core', 'kciwapp-core.exe')}
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


def main():
    if not os.path.isdir(ORIGEN) or len(list(archivos_de(ORIGEN))) < 20:
        print(f'no esta la instalacion en {ORIGEN}: no se publica nada (para no vaciar H:)')
        return 1
    if not os.path.isdir(DESTINO):
        os.makedirs(DESTINO)
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
        lista.append((rel, c['md5'], st.st_size))
    json.dump(cache, open(CACHE, 'w'), indent=1)
    # Lo publicado hasta ahora, segun su manifiesto.
    publicado = {}
    try:
        for linea in open(os.path.join(DESTINO, MANIFIESTO), encoding='utf-8'):
            partes = linea.rstrip('\n').split(' ', 2)
            if len(partes) == 3:
                publicado[partes[2]] = partes[0]
    except FileNotFoundError:
        pass
    copiados = 0
    for rel, md5, tamano in lista:
        destino = os.path.join(DESTINO, rel)
        if publicado.get(rel) == md5 and os.path.exists(destino) and os.path.getsize(destino) == tamano:
            continue
        os.makedirs(os.path.dirname(destino), exist_ok=True)
        print(f'copiando {rel} ({tamano // 1024} KB)')
        shutil.copyfile(FUENTES.get(rel, os.path.join(ORIGEN, rel)), destino)
        copiados += 1
    nuestros = {rel for rel, _, _ in lista}
    borrados = 0
    for rel in list(archivos_de(DESTINO)):
        if rel != MANIFIESTO and rel not in nuestros:
            print(f'borrando {rel}')
            os.remove(os.path.join(DESTINO, rel))
            borrados += 1
    # El manifiesto al final, cuando ya esta todo. Solo en H: (el del portable
    # lo escribe el cliente al aplicar una actualizacion; si lo pusieramos aca
    # el portable creeria que ya tiene lo publicado).
    texto = ''.join(f'{md5} {tamano} {rel}\n' for rel, md5, tamano in lista)
    with open(os.path.join(DESTINO, MANIFIESTO), 'w', encoding='utf-8', newline='\n') as f:
        f.write(texto)
    # Lo de al lado: el instalador (fuera del manifiesto), instrucciones e instaladores del server.
    shutil.copyfile(os.path.join(RAIZ, 'build', INSTALADOR), os.path.join(DESTINO, INSTALADOR))
    shutil.copyfile(os.path.join(RAIZ, 'docs', 'INSTALAR.md'), os.path.join(DESTINO, 'INSTALAR.md'))
    os.makedirs(os.path.join(DESTINO, 'server'), exist_ok=True)
    for f in ('instalar.sh', 'instalar-slackware.sh'):
        shutil.copyfile(os.path.join(RAIZ, 'core', f), os.path.join(DESTINO, 'server', f))
    total = sum(t for _, _, t in lista)
    print(f'{len(lista)} archivos ({total // (1 << 20)} MB), {copiados} copiados, {borrados} borrados -> {DESTINO}\\{MANIFIESTO}')


if __name__ == '__main__':
    sys.exit(main())
