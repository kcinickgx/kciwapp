# Rehace portable/emoji.txt (la tabla del selector de emojis) desde el
# emoji-test.txt oficial de Unicode, quedandose solo con los que Segoe UI
# Emoji sabe dibujar (si no, el selector mostraria cuadraditos).
#
# Formato de salida, una linea por emoji:  <emoji>\t<categoria>\t<nombre>
# Las categorias son las 8 que muestra el selector (emoji.cpp).
#
#   python docs/emoji-tabla.py [version]      (por defecto la ultima)
import os
import struct
import sys
import urllib.request

RAIZ = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SALIDA = os.path.join(RAIZ, 'portable', 'emoji.txt')
FUENTE = r'C:\Windows\Fonts\seguiemj.ttf'
VERSION = sys.argv[1] if len(sys.argv) > 1 else 'latest'
URL = f'https://unicode.org/Public/emoji/{VERSION}/emoji-test.txt'

# group / subgroup de Unicode -> las 8 categorias de emoji.cpp.
CATEGORIAS = {
    'Smileys & Emotion': 0, 'People & Body': 0,
    'Animals & Nature': 1,
    'Food & Drink': 2,
    'Activities': 3,
    'Travel & Places': 4,
    'Objects': 5,
    'Symbols': 6,
    'Flags': 7,
}
# Modificadores de tono de piel: el selector muestra el emoji base.
TONOS = {0x1F3FB, 0x1F3FC, 0x1F3FD, 0x1F3FE, 0x1F3FF}


def puntos_del_font(ruta):
    """Los codepoints que el font tiene en su cmap (formato 12)."""
    d = open(ruta, 'rb').read()
    n = struct.unpack('>H', d[4:6])[0]
    tablas = {}
    for i in range(n):
        off = 12 + 16 * i
        tag = d[off:off + 4].decode('latin1')
        o, l = struct.unpack('>II', d[off + 8:off + 16])
        tablas[tag] = (o, l)
    o, _ = tablas['cmap']
    hay = set()
    for i in range(struct.unpack('>H', d[o + 2:o + 4])[0]):
        _, _, off = struct.unpack('>HHI', d[o + 4 + 8 * i:o + 4 + 8 * i + 8])
        s = o + off
        fmt = struct.unpack('>H', d[s:s + 2])[0]
        if fmt == 12:
            for k in range(struct.unpack('>I', d[s + 12:s + 16])[0]):
                ini, fin, _ = struct.unpack('>III', d[s + 16 + 12 * k:s + 16 + 12 * k + 12])
                hay.update(range(ini, fin + 1))
        elif fmt == 4:
            segx2 = struct.unpack('>H', d[s + 6:s + 8])[0]
            seg = segx2 // 2
            fines = struct.unpack('>%dH' % seg, d[s + 14:s + 14 + segx2])
            inis = struct.unpack('>%dH' % seg, d[s + 16 + segx2:s + 16 + 2 * segx2])
            for a, b in zip(inis, fines):
                if b != 0xFFFF:
                    hay.update(range(a, b + 1))
    return hay


def main():
    print(f'bajando {URL}')
    with urllib.request.urlopen(URL) as r:
        texto = r.read().decode('utf-8')
    hay = puntos_del_font(FUENTE)
    print(f'{len(hay)} codepoints en Segoe UI Emoji')
    grupo = ''
    salida, vistos, sin_font = [], set(), []
    for linea in texto.splitlines():
        if linea.startswith('# group:'):
            grupo = linea.split(':', 1)[1].strip()
            continue
        if not linea or linea.startswith('#'):
            continue
        datos, _, comentario = linea.partition('#')
        campos = datos.split(';')
        if len(campos) < 2 or campos[1].strip() != 'fully-qualified':
            continue
        puntos = [int(x, 16) for x in campos[0].split()]
        if TONOS & set(puntos):
            continue  # los tonos de piel no van en la grilla
        cat = CATEGORIAS.get(grupo)
        if cat is None:
            continue
        emoji = ''.join(chr(p) for p in puntos)
        if emoji in vistos:
            continue
        # El nombre viene como "😀 E1.0 grinning face": se saca la version.
        partes = comentario.strip().split(' ', 2)
        nombre = partes[2] if len(partes) > 2 else ''
        # Todos los codepoints (menos VS16 y ZWJ) tienen que estar en el font.
        falta = [p for p in puntos if p not in hay and p not in (0xFE0F, 0x200D)]
        if falta:
            sin_font.append((emoji, nombre))
            continue
        vistos.add(emoji)
        salida.append(f'{emoji}\t{cat}\t{nombre}')
    with open(SALIDA, 'w', encoding='utf-8', newline='\n') as f:
        f.write('\n'.join(salida) + '\n')
    print(f'{len(salida)} emojis -> {SALIDA} ({len(sin_font)} sin glifo en el font)')
    for e, n in sin_font[:10]:
        print('  sin glifo:', n)


if __name__ == '__main__':
    sys.exit(main())
