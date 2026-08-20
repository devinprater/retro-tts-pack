#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
DATA_HOME=${XDG_DATA_HOME:-"$HOME/.local/share"}
CONFIG_HOME=${XDG_CONFIG_HOME:-"$HOME/.config"}
INSTALL_DIR=${RETRO_TTS_INSTALL_DIR:-"$DATA_HOME/retro-tts-pack"}
USER_BIN="$HOME/.local/bin"
CLI="$USER_BIN/retro-tts"
SPEECHD_DIR="$CONFIG_HOME/speech-dispatcher"
MODULE_DIR="$SPEECHD_DIR/modules"
SYSTEMD_DIR="$CONFIG_HOME/systemd/user"

say() { printf '%s\n' "$*"; }
warn() { printf 'WARNING: %s\n' "$*" >&2; }
die() { printf 'ERROR: %s\n' "$*" >&2; exit 1; }

download_assets=ask
for argument in "$@"; do
    case "$argument" in
        --download-assets) download_assets=yes ;;
        --no-download-assets) download_assets=no ;;
        --help|-h)
            say "Usage: ./install.sh [--download-assets|--no-download-assets]"
            exit 0
            ;;
        *) die "unknown option: $argument" ;;
    esac
done

architecture=$(uname -m)
case "$architecture" in
    x86_64|aarch64) ;;
    *) die "this binary pack supports x86_64 and aarch64 Linux only" ;;
esac
command -v python3 >/dev/null 2>&1 || die "python3 is required"
python3 -c 'import sys; raise SystemExit(sys.version_info < (3, 10))' ||
    die "Python 3.10 or newer is required"
command -v speech-dispatcher >/dev/null 2>&1 ||
    warn "Speech Dispatcher is not installed; files will be installed but voices cannot be registered"
command -v pw-play >/dev/null 2>&1 ||
    warn "pw-play is missing; install PipeWire tools before using the voices"

if [ "$download_assets" = ask ]; then
    if [ -t 0 ]; then
        printf '%s' \
            "Download checksum-pinned BeSTSpeech and Prose 2000 assets from their GitHub releases? [y/N] "
        read -r answer
        case "$answer" in y|Y|yes|YES) download_assets=yes ;; *) download_assets=no ;; esac
    else
        download_assets=no
    fi
fi
if [ "$download_assets" = yes ]; then
    say "Downloading assets published by the BeSTSpeech and Prose 2000 projects."
    say "Their upstream terms still apply; continuing indicates you accept them."
    python3 "$ROOT/download-assets.py" "$ROOT/assets"
fi

mkdir -p "$INSTALL_DIR" "$USER_BIN" "$MODULE_DIR" "$SYSTEMD_DIR"

root_real=$(readlink -f "$ROOT")
install_real=$(readlink -f "$INSTALL_DIR")
if [ "$root_real" != "$install_real" ]; then
    for item in app bin config lib licenses vendor README.md CHANGELOG.md VERSION download-assets.py; do
        [ ! -e "$ROOT/$item" ] || cp -a "$ROOT/$item" "$INSTALL_DIR/"
    done
    mkdir -p "$INSTALL_DIR/assets"
    if [ -d "$ROOT/assets" ]; then
        cp -an "$ROOT/assets/." "$INSTALL_DIR/assets/"
    fi
fi

cat >"$CLI" <<EOF
#!/bin/sh
INSTALL_DIR='$INSTALL_DIR'
export PYTHONPATH="\$INSTALL_DIR/app/src:\$INSTALL_DIR/vendor\${PYTHONPATH:+:\$PYTHONPATH}"
export LD_LIBRARY_PATH="\$INSTALL_DIR/vendor/unicorn/lib\${LD_LIBRARY_PATH:+:\$LD_LIBRARY_PATH}"
exec python3 -m retro_tts.cli "\$@"
EOF
chmod 755 "$CLI"

LEOPARD_HOST="$INSTALL_DIR/bin/leopard_host.exe"
LEOPARD_BACKEND=wine
if [ -x "$INSTALL_DIR/bin/leopard_host" ] &&
   "$INSTALL_DIR/bin/leopard_host" --aac-check >/dev/null 2>&1; then
    LEOPARD_HOST="$INSTALL_DIR/bin/leopard_host"
    LEOPARD_BACKEND=native
fi

cat >"$SYSTEMD_DIR/retro-tts.service" <<EOF
[Unit]
Description=Persistent renderer for retro Speech Dispatcher modules

[Service]
Type=simple
ExecStart=$INSTALL_DIR/bin/retro-tts-server
Restart=on-failure
RestartSec=1
Environment=RETRO_TTS_PROSE_CLI=$INSTALL_DIR/bin/prose_cli
Environment=RETRO_TTS_PROSE_HOST=$INSTALL_DIR/bin/ProseHost
Environment=RETRO_TTS_PROSE_ROMS=$INSTALL_DIR/assets/prose2000
Environment=RETRO_TTS_DTALK_CLI=$INSTALL_DIR/bin/dtalk_cli
Environment=RETRO_TTS_DTALK_ROM=$INSTALL_DIR/assets/doubletalkpc/doubletalkpc.bin
Environment=RETRO_TTS_BESTSPEECH_SHIM=$INSTALL_DIR/lib/libbst_shim.so
Environment=RETRO_TTS_BESTSPEECH_DLL=$INSTALL_DIR/assets/bestspeech/b32_tts.dll
Environment=RETRO_TTS_SOFTVOICE_SHIM=$INSTALL_DIR/lib/libsv_shim.so
Environment=RETRO_TTS_SOFTVOICE_BASE_DLL=$INSTALL_DIR/assets/softvoice/tibase32.dll
Environment=RETRO_TTS_SOFTVOICE_LANGUAGE_DLL=$INSTALL_DIR/assets/softvoice/tieng32.dll
Environment=RETRO_TTS_AMIGA_NARRATOR=$INSTALL_DIR/bin/narrator
Environment=RETRO_TTS_AMIGA_DEVICE=$INSTALL_DIR/assets/amiganarrator/narrator.device
Environment=RETRO_TTS_AMIGA_TRANSLATOR=$INSTALL_DIR/bin/translator
Environment=RETRO_TTS_AMIGA_TRANSLATOR_LIBRARY=$INSTALL_DIR/assets/amiganarrator/translator.library
Environment=RETRO_TTS_AMIGA_CMU_DICT=$INSTALL_DIR/assets/amiganarrator/cmudict.txt
Environment=RETRO_TTS_SMOOTHTALKER_IMAGE=$INSTALL_DIR/assets/smoothtalker/engine.bin
Environment=RETRO_TTS_MONOLOGUE_BIN=$INSTALL_DIR/assets/monologue
Environment=RETRO_TTS_WINTALKER_CLI=$INSTALL_DIR/bin/wintalker_cli.exe
Environment=RETRO_TTS_WINTALKER_DLL=$INSTALL_DIR/assets/wintalker/WinTalker.dll
Environment=RETRO_TTS_WINTALKER_LEX=$INSTALL_DIR/assets/wintalker/English.lex
Environment=RETRO_TTS_LEOPARD_HOST=$LEOPARD_HOST
Environment=RETRO_TTS_LEOPARD_BACKEND=$LEOPARD_BACKEND
Environment=RETRO_TTS_LEOPARD_TREE=$INSTALL_DIR/assets/leopardspeech/leopardspeech-data
Environment=RETRO_TTS_LEOPARD_VOICE=Alex
KillMode=mixed
TimeoutStopSec=3

[Install]
WantedBy=default.target
EOF

cat >"$INSTALL_DIR/bin/retro-tts-server" <<EOF
#!/bin/sh
INSTALL_DIR='$INSTALL_DIR'
export PYTHONPATH="\$INSTALL_DIR/app/src:\$INSTALL_DIR/vendor\${PYTHONPATH:+:\$PYTHONPATH}"
export LD_LIBRARY_PATH="\$INSTALL_DIR/vendor/unicorn/lib\${LD_LIBRARY_PATH:+:\$LD_LIBRARY_PATH}"
exec python3 -m retro_tts.server
EOF
chmod 755 "$INSTALL_DIR/bin/retro-tts-server"

available_modules="sam stspeech"
missing_modules=""

has_all() {
    for path in "$@"; do
        [ -f "$path" ] || return 1
    done
}

ASSETS="$INSTALL_DIR/assets"
if has_all "$ASSETS/smoothtalker/engine.bin"; then
    available_modules="$available_modules smoothtalker"
else missing_modules="$missing_modules smoothtalker"; fi
if has_all \
    "$ASSETS/monologue/FB_11K8.DLL" \
    "$ASSETS/monologue/FB_22K16.DLL" \
    "$ASSETS/monologue/FB_DEFLT.DIC" \
    "$ASSETS/monologue/FB_NGN.EXE" \
    "$ASSETS/monologue/FB_SPCH.DLL" \
    "$ASSETS/monologue/FB_TIMER.DLL"; then
    available_modules="$available_modules monologue"
else missing_modules="$missing_modules monologue"; fi
if has_all \
    "$ASSETS/prose2000/v3.4.1__2000__2.u22" \
    "$ASSETS/prose2000/v3.4.1__2000__3.u45" \
    "$ASSETS/prose2000/v3.4.1__2000__0.u21" \
    "$ASSETS/prose2000/v3.4.1__2000__1.u44" \
    "$ASSETS/prose2000/v3.12__8-9-88__dsp_prog.u29" \
    "$ASSETS/prose2000/v3.12__8-9-88__dsp_data.u29"; then
    available_modules="$available_modules prose2000"
else missing_modules="$missing_modules prose2000"; fi
if has_all "$ASSETS/doubletalkpc/doubletalkpc.bin"; then
    available_modules="$available_modules doubletalkpc"
else missing_modules="$missing_modules doubletalkpc"; fi
if has_all "$ASSETS/bestspeech/b32_tts.dll"; then
    available_modules="$available_modules bestspeech"
else missing_modules="$missing_modules bestspeech"; fi
if has_all "$ASSETS/softvoice/tibase32.dll" "$ASSETS/softvoice/tieng32.dll"; then
    available_modules="$available_modules softvoice"
else missing_modules="$missing_modules softvoice"; fi
if has_all "$ASSETS/amiganarrator/narrator.device" &&
   { [ -f "$ASSETS/amiganarrator/translator.library" ] ||
     [ -f "$ASSETS/amiganarrator/cmudict.txt" ]; }; then
    available_modules="$available_modules amiganarrator"
else missing_modules="$missing_modules amiganarrator"; fi
if [ "$architecture" = x86_64 ] &&
   has_all "$ASSETS/wintalker/WinTalker.dll" "$ASSETS/wintalker/English.lex" &&
   command -v wine >/dev/null 2>&1; then
    available_modules="$available_modules wintalker"
else missing_modules="$missing_modules wintalker"; fi
if [ "$architecture" = x86_64 ] &&
   has_all \
    "$ASSETS/leopardspeech/leopardspeech-data/Speech/Synthesizers/MacinTalk.SpeechSynthesizer/Contents/MacOS/MacinTalk" \
    "$ASSETS/leopardspeech/leopardspeech-data/SpeechDictionary.framework/Versions/A/SpeechDictionary" &&
   [ -d "$ASSETS/leopardspeech/leopardspeech-data/Speech/Voices" ] &&
   { { [ "$LEOPARD_BACKEND" = native ] && [ -x "$LEOPARD_HOST" ]; } ||
     { [ -f "$LEOPARD_HOST" ] && command -v wine >/dev/null 2>&1; }; }; then
    available_modules="$available_modules leopardspeech"
else missing_modules="$missing_modules leopardspeech"; fi

for module in $available_modules; do
    source="$INSTALL_DIR/config/modules/$module-generic.conf"
    target="$MODULE_DIR/$module-generic.conf"
    if [ "$module" = leopardspeech ]; then
        sed -e "s|retro-tts|$CLI|g" \
            -e "s|leopard_client|$INSTALL_DIR/bin/leopard_client|g" \
            "$source" >"$target"
    else
        sed "s|retro-tts|$CLI|g" "$source" >"$target"
    fi
done

SPEECHD_CONF="$SPEECHD_DIR/speechd.conf"
if [ ! -f "$SPEECHD_CONF" ]; then
    for candidate in \
        /etc/speech-dispatcher/speechd.conf \
        /usr/share/speech-dispatcher/conf/speechd.conf; do
        if [ -f "$candidate" ]; then
            cp "$candidate" "$SPEECHD_CONF"
            break
        fi
    done
fi

if [ -f "$SPEECHD_CONF" ]; then
    sed -i '/# BEGIN RETRO-TTS-PACK/,/# END RETRO-TTS-PACK/d' "$SPEECHD_CONF"
    {
        printf '\n# BEGIN RETRO-TTS-PACK\n'
        for module in $available_modules; do
            printf 'AddModule "%s" "sd_generic" "%s-generic.conf"\n' "$module" "$module"
        done
        printf '# END RETRO-TTS-PACK\n'
    } >>"$SPEECHD_CONF"
else
    warn "could not locate a base speechd.conf; add the AddModule lines from README.md manually"
fi

if [ "${RETRO_TTS_SKIP_SYSTEMD:-0}" != 1 ] &&
   command -v systemctl >/dev/null 2>&1 &&
   systemctl --user show-environment >/dev/null 2>&1; then
    systemctl --user daemon-reload
    if systemctl --user enable --now retro-tts.service; then
        say "Persistent renderer enabled and started."
    else
        warn "systemd could not start retro-tts.service; see README.md"
    fi
    # Prefer one socket-activated dispatcher. Leaving the socket disabled lets
    # libspeechd auto-spawn a second daemon, after which restarting the systemd
    # service fails with "Speech Dispatcher already running" and Orca can
    # remain attached to a stale socket.
    systemctl --user enable --now speech-dispatcher.socket >/dev/null 2>&1 || true
    systemctl --user try-restart speech-dispatcher.service >/dev/null 2>&1 || true
else
    warn "no usable systemd user manager; start '$INSTALL_DIR/bin/retro-tts-server' in your desktop session"
fi

say "Installed modules:$available_modules"
if [ -n "$missing_modules" ]; then
    say "Skipped modules missing proprietary assets:$missing_modules"
    say "Copy those assets into $ASSETS and run install.sh again."
fi
say "Installation directory: $INSTALL_DIR"
say "See $INSTALL_DIR/README.md for asset names and troubleshooting."
