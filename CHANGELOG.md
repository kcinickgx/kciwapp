# Changelog

The `current` release on GitHub always carries the latest version; the client
updates itself from it (⋯ → Check for updates). Version = `VERSION` file;
build date and commit show in ⋯ → About.

## 1.0.0 — 2026-09-17

First public release.

- Status tab: the status chat leaves the chat list; a second tab (WhatsApp-style
  icons) lists My status, Recent and Viewed per contact with segmented rings, and
  a viewer on the right with progress bar, auto-advance, reply field and delete
  for your own. Read receipts are sent only for the status being viewed.
- Message translation with a local model (llama.cpp + Qwen2.5-7B, optional
  in the setup like whisper): right-click a message → Translate; the target
  language is in Settings → Translation.
- About dialog (version, build date, commit, links) and a version number;
  playback speed (1x/1.5x/2x) is remembered.
- Business bot messages: buttons, lists, templates, native-flow buttons,
  product and order cards. They render inside the bubble and can be answered
  from the client (quick reply, list row, open link, copy phone/code).
- Consecutive voice notes: when one finishes, the next one starts on its own
  if it was already there and is the very next message.
- Image viewer: the close button always works, even with the photo zoomed or
  panned under it.
- Contact cards (vCard): avatar, name, phone, "Message" button; sending
  contacts from the + menu.
- Quoted photos/videos show a thumbnail; click goes to the original.
- Documents: click = Save as; Open with Windows stays in the menu.
- README with screenshots (demo data) and a download button.

## 2026-09-16

- Installer (`kciwapp-setup.exe`) and self-update from the GitHub `current`
  release (per-file MD5 manifest, only changed files are downloaded, cache
  buster so the check sees a new publish immediately).
- Statuses: post text (background colour and font pickers), photo, video and
  voice statuses from the *status* chat; no reply/typing there.
- Several accounts per client (`cuentas.json`), account menu, provisional
  accounts on the multi-account server until linked.
- "Load all messages" is a plain modal that greys out the chat.
- Interactive contact cards in chats and in the chat-list preview.

## 2026-09-15

- Own notifications (toast window with monitor/corner/duration settings),
  stacked, each with its own timer, silent while kciwapp is in front.
- Search: whole result set at once, in-chat search (Ctrl+F) with n/N
  navigation and highlighting inside bubbles.
- Typing / recording presence both ways, with the "…" and mic bubbles.
- Calls: incoming-call toast with ring and Reject; missed/answered calls as
  chat messages; voice and video calls through a hidden WhatsApp Web
  (WebView2) with an own call window.
- Forward with multi-select, own themed context menu, Copy image / Copy file.
- Window remembers position, size and maximized state; single instance per
  folder.
- Empty screen like the original client; I-beam cursor over text fields.
- Voice note transcription on demand (whisper.cpp, CUDA).
- Albums: 3+ consecutive photos in a grid.
- Message layouts built in background threads; far-away layouts released to
  save RAM; the cache is authoritative and the server only fills gaps.
- Local core (`core\kciwapp-core.exe`, SQLite) or shared server (MariaDB);
  linking from the client with an own QR screen.
- iPhone backup importer (server side) and iOS chat-export importer.

## 2026-09-14

- New client from scratch: C++20, Win32 + Direct2D/DirectWrite, GPU rendered,
  everything drawn by hand (no frameworks). Chats, groups, media, voice notes
  with waveform/seek/speed, video player (mpv), stickers, reactions, replies,
  edits, themes, custom backgrounds, font sizes, settings window, tray.
