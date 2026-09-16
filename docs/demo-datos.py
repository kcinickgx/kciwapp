# Llena el SQLite del core de una instalacion demo (demo-run\datos\core.sqlite3)
# con chats, contactos y mensajes inventados, para sacar capturas del README
# sin exponer nada real. Correr con la demo cerrada; despues abrirla con --demo.
#
#   python docs\demo-datos.py [carpeta-datos]
import io, os, random, sqlite3, sys, time
from PIL import Image, ImageDraw, ImageFont

DATOS = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'demo-run', 'datos')
DATOS = os.path.abspath(DATOS)
DB = os.path.join(DATOS, 'core.sqlite3')
MEDIA = os.path.join(DATOS, 'media-core')
FOTOS = os.path.join(DATOS, 'fotos-demo')
os.makedirs(MEDIA, exist_ok=True)
os.makedirs(FOTOS, exist_ok=True)
random.seed(7)

YO = '15550001234@s.whatsapp.net'
AHORA = int(time.time() * 1000)
MIN = 60_000
HORA = 60 * MIN
DIA = 24 * HORA

def fuente(tam):
    for f in ('segoeui.ttf', 'arial.ttf'):
        try:
            return ImageFont.truetype(f, tam)
        except Exception:
            pass
    return ImageFont.load_default()

# Retratos de placeholder (pravatar.cc = fotos libres de Unsplash); si no hay
# red, un circulo con la inicial.
import urllib.request
def avatar(ruta, inicial, color, foto_n=None):
    if foto_n is not None:
        try:
            req = urllib.request.Request('https://i.pravatar.cc/256?img=%d' % foto_n, headers={'User-Agent': 'kciwapp-demo'})
            datos = urllib.request.urlopen(req, timeout=20).read()
            Image.open(io.BytesIO(datos)).convert('RGB').save(ruta, 'JPEG', quality=88)
            return
        except Exception as e:
            print('sin foto para', inicial, e)
    im = Image.new('RGB', (256, 256), color)
    d = ImageDraw.Draw(im)
    f = fuente(130)
    w = d.textlength(inicial, font=f)
    d.text(((256 - w) / 2, 40), inicial, fill=(255, 255, 255), font=f)
    im.save(ruta, 'JPEG', quality=88)

def foto(ruta, ancho, alto, semilla, titulo):
    # Un degrade con figuras: alcanza para ver que es una foto.
    r = random.Random(semilla)
    im = Image.new('RGB', (ancho, alto))
    px = im.load()
    c1 = (r.randint(30, 120), r.randint(60, 160), r.randint(90, 200))
    c2 = (r.randint(150, 250), r.randint(100, 220), r.randint(60, 200))
    for y in range(alto):
        t = y / alto
        col = tuple(int(c1[k] * (1 - t) + c2[k] * t) for k in range(3))
        for x in range(ancho):
            px[x, y] = col
    d = ImageDraw.Draw(im)
    for _ in range(6):
        x0, y0 = r.randint(0, ancho), r.randint(0, alto)
        rad = r.randint(30, max(40, ancho // 4))
        d.ellipse((x0 - rad, y0 - rad, x0 + rad, y0 + rad), fill=(r.randint(0, 255), r.randint(0, 255), r.randint(0, 255)))
    d.text((16, alto - 40), titulo, fill=(255, 255, 255), font=fuente(24))
    im.save(ruta, 'JPEG', quality=85)
    mini = im.copy()
    mini.thumbnail((320, 320))
    b = io.BytesIO()
    mini.save(b, 'JPEG', quality=70)
    return b.getvalue()

contactos = [
    # jid, nombre, color avatar
    ('15550002001@s.whatsapp.net', 'Alice Johnson', (76, 110, 175)),
    ('15550002002@s.whatsapp.net', 'Bob Martinez', (170, 90, 60)),
    ('15550002003@s.whatsapp.net', 'Mom', (150, 70, 130)),
    ('15550002004@s.whatsapp.net', 'Dave Chen', (60, 140, 110)),
    ('15550002005@s.whatsapp.net', 'Emma Wilson', (200, 120, 50)),
    ('15550002006@s.whatsapp.net', 'Frank', (90, 90, 160)),
    ('15550002007@s.whatsapp.net', 'Grace Lee', (40, 130, 150)),
    ('15550002008@s.whatsapp.net', 'Henry Adams', (140, 100, 60)),
]
grupos = [
    ('120363000000000001@g.us', 'Dev Team', ['15550002001@s.whatsapp.net', '15550002004@s.whatsapp.net', '15550002007@s.whatsapp.net']),
    ('120363000000000002@g.us', 'Weekend Hike', ['15550002002@s.whatsapp.net', '15550002005@s.whatsapp.net', '15550002006@s.whatsapp.net', '15550002008@s.whatsapp.net']),
]
nombre_de = {j: n for j, n, _ in contactos}
nombre_de[YO] = 'You'

# Conversaciones: (chat, [(quien, tipo, texto, hace_ms, extra)])
A, B, M, D, E, F, G, H = [c[0] for c in contactos]
DEV, HIKE = grupos[0][0], grupos[1][0]
conv = {
    A: [
        (A, 'texto', 'Hey! Did you get a chance to look at the draft?', 3 * DIA + 2 * HORA),
        (YO, 'texto', 'Yes, went through it last night. Looks solid overall.', 3 * DIA + HORA),
        (YO, 'texto', 'Two things: the intro is a bit long, and the numbers in section 3 need a source.', 3 * DIA + 58 * MIN),
        (A, 'texto', 'Fair. I will trim the intro and add the citations today.', 3 * DIA + 40 * MIN),
        (A, 'foto', 'Chart for section 3, does this work?', 2 * DIA + 5 * HORA, 'chart'),
        (YO, 'texto', 'Perfect, that is exactly what I meant 👌', 2 * DIA + 4 * HORA + 50 * MIN),
        (A, 'texto', 'Great. Sending the final version tomorrow morning.', 2 * DIA + 4 * HORA),
        (YO, 'texto', 'Sounds good. Coffee after?', 2 * DIA + 3 * HORA + 55 * MIN),
        (A, 'texto', 'Always ☕', 2 * DIA + 3 * HORA + 50 * MIN),
        (A, 'texto', 'Final version attached, let me know what you think!', 22 * MIN),
        (A, 'documento', '', 21 * MIN, 'Q3-report-final.pdf'),
    ],
    DEV: [
        (D, 'texto', 'Morning! Deploy window is 10:00 today, anything pending?', 6 * HORA),
        (G, 'texto', 'The search fix is merged. Tests green.', 5 * HORA + 40 * MIN),
        (A, 'texto', 'I still need a review on #418 (the scroll thing)', 5 * HORA + 30 * MIN),
        (YO, 'texto', 'On it. Give me 20 min.', 5 * HORA + 25 * MIN),
        (YO, 'texto', 'Reviewed, two small comments, otherwise LGTM 👍', 4 * HORA + 50 * MIN),
        (A, 'texto', 'Thanks! Addressed both.', 4 * HORA + 30 * MIN),
        (D, 'foto', 'Dashboard after the deploy 🎉', 2 * HORA, 'dashboard'),
        (G, 'texto', 'Latency down 40%, nice work everyone', 1 * HORA + 50 * MIN),
        (D, 'texto', 'Retro on Friday at 3?', 35 * MIN),
        (YO, 'texto', 'Works for me', 30 * MIN),
    ],
    M: [
        (M, 'texto', 'Are you coming for lunch on Sunday?', DIA + 6 * HORA),
        (YO, 'texto', 'Of course! Should I bring anything?', DIA + 5 * HORA),
        (M, 'texto', 'Just yourself. And maybe that lemon cake 😄', DIA + 4 * HORA + 30 * MIN),
        (YO, 'texto', 'Deal 🍋', DIA + 4 * HORA),
        (M, 'sistema', '\U0001F4DE Missed voice call', 3 * HORA + 10 * MIN),
        (M, 'texto', 'Call me when you can, nothing urgent', 3 * HORA),
    ],
    B: [
        (B, 'texto', 'Do you still have the number of that electrician?', 4 * DIA),
        (YO, 'contacto', 'Mike the electrician\nBEGIN:VCARD\nVERSION:3.0\nN:;Mike;;;\nFN:Mike the electrician\nTEL;type=CELL;waid=15550003333:+1 555 000 3333\nEND:VCARD', 4 * DIA - 10 * MIN),
        (B, 'texto', 'You are a lifesaver, thanks!', 4 * DIA - 12 * MIN),
        (B, 'texto', 'He fixed it in an hour 💪', 2 * DIA + 2 * HORA),
    ],
    HIKE: [
        (E, 'texto', 'Weather looks great for Saturday ☀️', 5 * DIA),
        (F, 'texto', 'Trailhead at 7am? Parking fills up fast', 5 * DIA - 20 * MIN),
        (H, 'texto', '7 is early but ok 😴', 5 * DIA - 15 * MIN),
        (YO, 'texto', 'I will bring the sandwiches', 5 * DIA - HORA),
        (E, 'foto', 'View from the top!', 4 * DIA + 8 * HORA, 'mountain'),
        (B, 'texto', 'Incredible 😍', 4 * DIA + 7 * HORA + 50 * MIN),
        (YO, 'texto', 'Best hike of the year', 4 * DIA + 7 * HORA + 40 * MIN),
    ],
    E: [
        (E, 'texto', 'Happy birthday!! 🎂🎉', 6 * DIA),
        (YO, 'texto', 'Thank you Emma! ❤️', 6 * DIA - HORA),
    ],
    G: [
        (YO, 'texto', 'Can you send me the slides from yesterday?', 8 * DIA),
        (G, 'texto', 'Sure, here you go', 8 * DIA - 30 * MIN),
        (G, 'documento', '', 8 * DIA - 29 * MIN, 'kickoff-slides.pptx'),
        (YO, 'texto', 'Got it, thanks', 8 * DIA - 20 * MIN),
    ],
    H: [
        (H, 'texto', 'Game tonight?', 12 * DIA),
        (YO, 'texto', 'Cannot tonight, next week for sure', 12 * DIA - 2 * HORA),
    ],
    F: [
        (F, 'texto', 'Left the keys with your neighbor', 20 * DIA),
        (YO, 'texto', 'Perfect, thanks Frank', 20 * DIA - HORA),
    ],
}
reacciones = [
    (A, 'Perfect, that is exactly what I meant 👌', A, '❤️'),
    (DEV, 'Latency down 40%, nice work everyone', YO, '🔥'),
    (DEV, 'Latency down 40%, nice work everyone', A, '🔥'),
    (HIKE, 'View from the top!', YO, '😍'),
    (HIKE, 'View from the top!', H, '👏'),
    (M, 'Deal 🍋', M, '😂'),
]
no_leidos = {A: 2, DEV: 1}

con = sqlite3.connect(DB)
cur = con.cursor()
for t in ('mensajes', 'reacciones', 'media', 'chats', 'contactos', 'miembros', 'eventos'):
    cur.execute(f'DELETE FROM {t}')
# Numeros de retrato de pravatar (elegidos para que coincidan con los nombres).
retratos = [47, 12, 32, 60, 25, 8, 44, 68]
for (jid, nombre, color), n_foto in zip(contactos, retratos):
    ruta = os.path.join(FOTOS, jid.split('@')[0] + '.jpg')
    avatar(ruta, nombre[0], color, n_foto)
    cur.execute('INSERT INTO contactos (jid, telefono, nombre_agenda, foto) VALUES (?, ?, ?, ?)', (jid, '+' + jid.split('@')[0], nombre, ruta))
cur.execute('INSERT INTO contactos (jid, telefono, nombre_agenda) VALUES (?, ?, ?)', (YO, '+15550001234', 'You'))
for jid, nombre, miembros in grupos:
    ruta = os.path.join(FOTOS, jid.split('@')[0] + '.jpg')
    avatar(ruta, nombre[0], (80, 80, 80))
    cur.execute('INSERT INTO chats (jid, nombre, es_grupo, foto) VALUES (?, ?, 1, ?)', (jid, nombre, ruta))
    for m in miembros + [YO]:
        cur.execute('INSERT INTO miembros (chat, jid, admin) VALUES (?, ?, ?)', (jid, m, 1 if m == YO else 0))
for jid, nombre, _ in contactos:
    cur.execute('INSERT INTO chats (jid, nombre, es_grupo) VALUES (?, ?, 0)', (jid, nombre))

ids = {}
n = 0
for chat, lista in conv.items():
    ultimo = 0
    for quien, tipo, texto, hace, *extra in lista:
        n += 1
        mid = 'DEMO%06d' % n
        ts = AHORA - hace
        propio = 1 if quien == YO else 0
        estado = 3 if propio else 0  # leido (tildes azules)
        media_id = None
        if tipo == 'foto':
            ruta = os.path.join(MEDIA, mid + '.jpg')
            mini = foto(ruta, 1280, 960, n, extra[0])
            cur.execute('INSERT INTO media (chat, mensaje, mime, nombre, bytes, ancho, alto, ruta, estado, miniatura) VALUES (?, ?, ?, ?, ?, ?, ?, ?, 1, ?)',
                        (chat, mid, 'image/jpeg', '', os.path.getsize(ruta), 1280, 960, ruta, mini))
            media_id = cur.lastrowid
            tipo = 'imagen'
        elif tipo == 'documento':
            ruta = os.path.join(MEDIA, extra[0])
            with open(ruta, 'wb') as f:
                f.write(os.urandom(240_000))
            cur.execute('INSERT INTO media (chat, mensaje, mime, nombre, bytes, ruta, estado) VALUES (?, ?, ?, ?, ?, ?, 1)',
                        (chat, mid, 'application/octet-stream', extra[0], os.path.getsize(ruta), ruta))
            media_id = cur.lastrowid
        cur.execute('INSERT INTO mensajes (id_wa, chat, remitente, propio, ts, tipo, texto, cita_texto, media_id, estado) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)',
                    (mid, chat, quien, propio, ts, tipo, texto, '', media_id, estado))
        ids[(chat, texto)] = (mid, ts)
        ultimo = max(ultimo, ts)
    cur.execute('UPDATE chats SET ultimo_ts = ?, no_leidos = ? WHERE jid = ?', (ultimo, no_leidos.get(chat, 0), chat))
for chat, texto, quien, emoji in reacciones:
    mid, ts = ids[(chat, texto)]
    cur.execute('INSERT INTO reacciones (chat, mensaje, remitente, emoji, ts) VALUES (?, ?, ?, ?, ?)', (chat, mid, quien, emoji, ts + MIN))
con.commit()
print('chats', cur.execute('select count(*) from chats').fetchone()[0], 'mensajes', n)
