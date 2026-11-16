# GX Whiteboard Studio

**Versione 0.1.0 — release candidate `v0.1.0-rc1`**
Applicazione autonoma C++17/Qt6 per Debian 13 amd64 e XFCE/X11, indipendente da GX-TXT.

## Funzioni

Lavagna digitale con supporto Wacom per pressione, penna e gomma, disegno vettoriale, forme, testo, selezione, Undo/Redo, pan e zoom. Immagini PNG/JPEG/WebP trascinabili direttamente da Thunar, box immagini e webcam ridimensionabili, gestione dell'ordine Z dei media e annotazioni sempre sopra.

Webcam USB, sorgenti IP sperimentali RTSP/HTTP-MJPEG, scene multiple, progetti `.gxwp`, lavagne `.gxwb`, salvataggio automatico, backup e recupero. Registrazione del canvas con FFmpeg H.264 (`libx264` oppure NVIDIA NVENC), microfono selezionabile tramite PulseAudio, Mute, AAC, MKV, conversione MP4 e rapporti diagnostici. Cataloghi gettext, manuali HTML e integrazione desktop in italiano e inglese.

## Pacchetto Debian

Scarica il pacchetto dalla [release candidate 0.1.0-rc1](https://github.com/giuxlab-hub/gx-whiteboard-studio/releases/tag/v0.1.0-rc1).

```bash
sudo apt install ./gx-whiteboard-studio_0.1.0-1_amd64.deb
gx-whiteboard-studio
```

Troverai l'applicazione nel menu Grafica; le associazioni MIME permettono di aprire `.gxwp` e `.gxwb` con un doppio clic da Thunar. Per disinstallare:

```bash
sudo apt remove gx-whiteboard-studio
```

I progetti, i video e le impostazioni XDG non vengono eliminati.

Il pacchetto originale F08 ha SHA-256:

```text
dc8d5724f42cd724c497b0262610d90462504e0310ddd1690b87a7efc004a7d4
```

## Compilare i sorgenti

Richiede Debian 13, CMake 3.20+, C++17, Qt6, Python3 per compilare i cataloghi e, per generare il `.deb`, `dpkg-dev` e `fakeroot`.

```bash
sudo apt install build-essential cmake qt6-base-dev python3 \
    dpkg-dev fakeroot ffmpeg pulseaudio-utils \
    qt6-qpa-plugins qt6-svg-plugins libxcb-cursor0

cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr
cmake --build build --parallel 2
./run-from-source.sh
```

Per ricostruire un pacchetto **di prova**:

```bash
mkdir -p results
DESTDIR="$PWD/package-root" cmake --install build
./tools/build_deb.sh
```

Il `.deb` pubblicato nella release GitHub rimane comunque quello originale della F08 e non un pacchetto ricompilato.

Manuali: [italiano](docs/manual_it.html) · [English](docs/manual_en.html). Dettagli tecnici: [dipendenze Debian](docs/DEBIAN_13.md) · [prove effettuate](docs/QA_STATUS.md).

## Limitazioni della release candidate

1. **La registrazione reale continuativa di 30 minuti non è ancora stata verificata.** La prova avverrà durante l'utilizzo pratico.
2. Le telecamere IP della rete sono state individuate, ma lo **streaming reale non ha funzionato**. L'autenticazione non è stata approfondita su scelta del proprietario.
3. Ogni scena dispone di Undo/Redo indipendente mentre l'applicazione è aperta, ma **la cronologia non viene conservata dopo la chiusura**.

La F08 ha superato build Release, 17 regressioni, quattro suite ASAN/UBSAN, simulazione APT e verifica dei file estratti. Il proprietario ha eseguito prove fisiche su Wacom, microfono della webcam, Mute e trascinamento da Thunar. L'installazione Debian globale non è stata eseguita.

Non è stata trovata una licenza generale già stabilita per il codice. Nessuna licenza viene assegnata per supposizione. La visibilità del repository sarà confermata dal proprietario prima di renderlo pubblico.
