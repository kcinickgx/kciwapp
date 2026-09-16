# Publica el cliente en H:\kciwapp (= https://kcinick.gxzone.com/kciwapp/),
# de donde el cliente se actualiza solo: copia lo que cambio de
# release\cliente, borra lo que sobra y escribe el manifiesto kciwapp.md5
# ("md5 tamano ruta" por linea). No lleva lo que es del usuario
# (ajustes.json, cuentas.json, datos\).
#
#   python publicar.py
import hashlib
import json
import os
import shutil
import sys

ORIGEN = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'release', 'cliente')
DESTINO = r'H:\kciwapp'
MANIFIESTO = 'kciwapp.md5'
EXCLUIR = {'ajustes.json', 'cuentas.json', 'servidor.json', 'debug.log', MANIFIESTO}
EXCLUIR_DIR = {'datos'}
CACHE = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'release', 'md5-cache.json')


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
    if not os.path.isdir(DESTINO):
        os.makedirs(DESTINO)
    try:
        cache = json.load(open(CACHE))
    except Exception:
        cache = {}
    # md5 de lo local, con cache por tamano+mtime (whisper son 2,7 GB).
    lista = []
    for rel in sorted(archivos_de(ORIGEN)):
        ruta = os.path.join(ORIGEN, rel)
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
        shutil.copyfile(os.path.join(ORIGEN, rel), destino)
        copiados += 1
    nuestros = {rel for rel, _, _ in lista}
    borrados = 0
    for rel in list(archivos_de(DESTINO)):
        if rel != MANIFIESTO and rel not in nuestros:
            print(f'borrando {rel}')
            os.remove(os.path.join(DESTINO, rel))
            borrados += 1
    # El manifiesto al final, cuando ya esta todo; la misma copia va en
    # release\cliente (el cliente compara contra la suya, sin releer nada).
    texto = ''.join(f'{md5} {tamano} {rel}\n' for rel, md5, tamano in lista)
    for carpeta in (DESTINO, ORIGEN):
        with open(os.path.join(carpeta, MANIFIESTO), 'w', encoding='utf-8', newline='\n') as f:
            f.write(texto)
    total = sum(t for _, _, t in lista)
    print(f'{len(lista)} archivos ({total // (1 << 20)} MB), {copiados} copiados, {borrados} borrados -> {DESTINO}\\{MANIFIESTO}')


if __name__ == '__main__':
    sys.exit(main())
