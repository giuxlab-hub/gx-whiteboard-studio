# GX Whiteboard Studio

**Versione 0.1.1 — release GitHub `v0.1.1`**
Applicazione autonoma C++17/Qt6 per Debian 13 amd64 e XFCE/X11, indipendente da GX-TXT.

## Funzioni

Lavagna digitale con supporto Wacom per pressione, penna e gomma, disegno vettoriale, forme, testo, selezione, Undo/Redo, pan e zoom. Immagini PNG/JPEG/WebP trascinabili direttamente da Thunar, box immagini e webcam ridimensionabili, gestione dell'ordine Z dei media e annotazioni sempre sopra.

Webcam USB, sorgenti IP sperimentali RTSP/HTTP-MJPEG, scene multiple, progetti `.gxwp`, lavagne `.gxwb`, salvataggio automatico, backup e recupero. Registrazione del canvas con FFmpeg H.264 (`libx264` oppure NVIDIA NVENC), microfono selezionabile tramite PulseAudio, Mute, AAC, MKV, conversione MP4 e rapporti diagnostici. Cataloghi gettext, manuali HTML e integrazione desktop in italiano e inglese.

## Novità della versione 0.1.1

- **Rimuovere miniature dalla libreria:** fai clic destro direttamente sulla miniatura e scegli **Rimuovi dalla libreria**. Scompare soltanto il riferimento dalla galleria; il file originale e le immagini già incorporate nelle scene non vengono cancellati. La modifica viene conservata nelle preferenze XDG tramite QSettings e sopravvive al riavvio; l'immagine può essere aggiunta nuovamente.
- **Guide offline complete:** **Aiuto → Guide → Manuale completo (italiano)** oppure **Manuale completo (inglese)**. Entrambi i manuali HTML ora contengono indice e 13 capitoli.
- **Informazioni:** versione **0.1.1**, autore **GiuxLab**, copyright **(C) 2026 GiuxLab**, licenza **GPL-3.0-or-later**.

## Pacchetto Debian

Scarica il pacchetto dalla [release v0.1.1](https://github.com/giuxlab-hub/gx-whiteboard-studio/releases/tag/v0.1.1).

```bash
sudo apt install ./gx-whiteboard-studio_0.1.1-1_amd64.deb
gx-whiteboard-studio
```

Troverai l'applicazione nel menu Grafica; le associazioni MIME permettono di aprire `.gxwp` e `.gxwb` con un doppio clic da Thunar. Per disinstallare:

```bash
sudo apt remove gx-whiteboard-studio
```

I progetti, i video e le impostazioni XDG non vengono eliminati.

Il pacchetto originale F08_1 0.1.1-1 ha SHA-256:

```text
2372e116f95c00c07be0264b6661e1930899d37550dd0e9cd3b1c870469f0a8a
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

La F08_1 ha superato build Release, 18 regressioni, verifiche ASAN/UBSAN, simulazione APT e verifica dei file estratti. Il proprietario ha eseguito prove fisiche su Wacom, microfono della webcam, Mute e trascinamento da Thunar. L'installazione Debian globale non è stata eseguita.

**Licenza:** GX Whiteboard Studio è pubblicato con **GNU General Public License versione 3 (GPL-3.0-or-later)**, come stabilito dal proprietario. Il testo completo è in [LICENSE](LICENSE). Il repository è pubblico.

Il pacchetto Debian 0.1.1-1 mantiene la licenza GPL-3.0-or-later, le intestazioni SPDX, il copyright Debian e il testo integrale della licenza. Aggiorna le versioni precedenti con la nuova versione applicativa 0.1.1.
