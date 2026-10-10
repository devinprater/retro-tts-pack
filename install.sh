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
    warn "pw-play is missing; the streaming engines (L&H, TrueVoice and Amiga Narrator) need it, and the rest will use paplay or aplay"

if [ "$download_assets" = ask ]; then
    if [ -t 0 ]; then
        printf '%s' \
            "Download checksum-pinned optional engine assets (including Leopard MacinTalk)? [y/N] "
        read -r answer
        case "$answer" in y|Y|yes|YES) download_assets=yes ;; *) download_assets=no ;; esac
    else
        download_assets=no
    fi
fi
if [ "$download_assets" = yes ]; then
    say "Downloading checksum-pinned optional engine assets from their published sources."
    say "Their upstream terms still apply; continuing indicates you accept them."
    python3 "$ROOT/download-assets.py" "$ROOT/assets"
fi

mkdir -p "$INSTALL_DIR" "$USER_BIN" "$MODULE_DIR" "$SYSTEMD_DIR"

root_real=$(readlink -f "$ROOT")
install_real=$(readlink -f "$INSTALL_DIR")
if [ "$root_real" != "$install_real" ]; then
    for item in app bin config lib licenses vendor README.md CHANGELOG.md VERSION download-assets.py; do
        [ ! -e "$ROOT/$item" ] || {
            if [ "$item" = bin ]; then
                # Speech Dispatcher may auto-spawn while Orca is running and
                # hold a module executable open. Replace flat bin entries by
                # rename so reinstalling cannot fail with ETXTBSY.
                mkdir -p "$INSTALL_DIR/bin"
                for source_file in "$ROOT/bin/"*; do
                    [ -e "$source_file" ] || continue
                    target_file="$INSTALL_DIR/bin/$(basename "$source_file")"
                    cp -a "$source_file" "$target_file.retro-new"
                    mv -f "$target_file.retro-new" "$target_file"
                done
            else
                cp -a "$ROOT/$item" "$INSTALL_DIR/"
            fi
        }
    done
    mkdir -p "$INSTALL_DIR/assets"
    if [ -d "$ROOT/assets" ]; then
        cp -an "$ROOT/assets/." "$INSTALL_DIR/assets/"
    fi
fi

# Reuse assets from NVDA add-ons kept near the source tree.  In particular,
# search through ../../ (and one level beyond it), since the distributable is
# commonly run from speechd-tts/dist/retro-tts-pack while add-ons live at the
# repository root.  Existing pack assets always win.
find_addon() {
    addon_name=$1
    for search_dir in "$ROOT" "$ROOT/.." "$ROOT/../.." "$ROOT/../../.."; do
        [ -d "$search_dir" ] || continue
        found=$(find "$search_dir" -maxdepth 2 -type f -iname "$addon_name" -print -quit 2>/dev/null)
        if [ -n "$found" ]; then
            printf '%s\n' "$found"
            return 0
        fi
    done
    return 1
}

import_addon_asset() {
    addon_pattern=$1
    member=$2
    destination=$3
    [ -f "$destination" ] && return 0
    addon=$(find_addon "$addon_pattern") || return 0
    mkdir -p "$(dirname "$destination")"
    if python3 -c 'import sys, zipfile
archive, wanted = sys.argv[1:]
wanted = wanted.replace("\\", "/")
with zipfile.ZipFile(archive) as source:
    name = next(n for n in source.namelist() if n.replace("\\", "/") == wanted)
    sys.stdout.buffer.write(source.read(name))' "$addon" "$member" \
       >"$destination.tmp" 2>/dev/null &&
       [ -s "$destination.tmp" ]; then
        mv "$destination.tmp" "$destination"
        say "Imported $(basename "$destination") from $(basename "$addon")."
    else
        rm -f "$destination.tmp"
    fi
}

import_zip_asset() {
    archive_pattern=$1
    member=$2
    destination=$3
    [ -f "$destination" ] && return 0
    archive=$(find_addon "$archive_pattern") || return 0
    mkdir -p "$(dirname "$destination")"
    if python3 -c 'import sys, zipfile
archive, wanted = sys.argv[1:]
with zipfile.ZipFile(archive) as source:
    name = next(n for n in source.namelist()
                if n.replace("\\", "/").casefold() == wanted.casefold())
    sys.stdout.buffer.write(source.read(name))' "$archive" "$member" \
       >"$destination.tmp" 2>/dev/null && [ -s "$destination.tmp" ]; then
        mv "$destination.tmp" "$destination"
        say "Imported $(basename "$destination") from $(basename "$archive")."
    else
        rm -f "$destination.tmp"
    fi
}

extract_cab_dlls() {
    archive_pattern=$1
    destination=$2
    archive=$(find_addon "$archive_pattern") || return 0
    work=$(mktemp -d "${TMPDIR:-/tmp}/retro-tts-cab.XXXXXX")
    if command -v 7z >/dev/null 2>&1; then
        # L&H used inconsistent .DLL/.dll casing between language packs.
        # Extract the small CAB payload and filter DLLs below.
        7z e -y -o"$work" "$archive" >/dev/null 2>&1 || true
    elif command -v cabextract >/dev/null 2>&1; then
        cabextract -q -d "$work" "$archive" >/dev/null 2>&1 || true
    else
        warn "cannot extract $(basename "$archive"); install 7z/cabextract or copy its DLLs into $destination"
        rm -rf "$work"
        return 0
    fi
    mkdir -p "$destination"
    imported=0
    for dll in "$work/"*.DLL "$work/"*.dll; do
        [ -f "$dll" ] || continue
        case $(basename "$dll" | tr '[:lower:]' '[:upper:]') in
            ADVPACK.DLL|W95INF16.DLL|W95INF32.DLL|LHSAPI40.DLL) continue ;;
        esac
        normalized=$(basename "$dll" | tr '[:lower:]' '[:upper:]')
        target="$destination/$normalized"
        if [ ! -f "$target" ]; then
            cp "$dll" "$target"
            imported=$((imported + 1))
        fi
    done
    rm -rf "$work"
    [ "$imported" -eq 0 ] || say "Imported $imported DLLs from $(basename "$archive")."
}

ASSETS="$INSTALL_DIR/assets"
import_addon_asset 'smoothtalker*.nvda-addon' 'synthDrivers\\_smoothtalker_engine\\engine.bin' "$ASSETS/smoothtalker/engine.bin"
    import_addon_asset 'monologue*.nvda-addon' 'synthDrivers/_monologue_engine/bin/FB_11K8.DLL' "$ASSETS/monologue/FB_11K8.DLL"
    import_addon_asset 'monologue*.nvda-addon' 'synthDrivers/_monologue_engine/bin/FB_22K16.DLL' "$ASSETS/monologue/FB_22K16.DLL"
    import_addon_asset 'monologue*.nvda-addon' 'synthDrivers/_monologue_engine/bin/FB_DEFLT.DIC' "$ASSETS/monologue/FB_DEFLT.DIC"
    import_addon_asset 'monologue*.nvda-addon' 'synthDrivers/_monologue_engine/bin/FB_NGN.EXE' "$ASSETS/monologue/FB_NGN.EXE"
    import_addon_asset 'monologue*.nvda-addon' 'synthDrivers/_monologue_engine/bin/FB_SPCH.DLL' "$ASSETS/monologue/FB_SPCH.DLL"
    import_addon_asset 'monologue*.nvda-addon' 'synthDrivers/_monologue_engine/bin/FB_TIMER.DLL' "$ASSETS/monologue/FB_TIMER.DLL"
    import_addon_asset 'doubletalkpc*.nvda-addon' 'synthDrivers/doubletalkpc/doubletalkpc.bin' "$ASSETS/doubletalkpc/doubletalkpc.bin"
    # BeSTspeech is the vendored openbst engine now: its tables are compiled in,
    # so there is no b32_tts.dll and no dll_*.dll to import.
    import_addon_asset 'softvoice*.nvda-addon' 'synthDrivers/tibase32.dll' "$ASSETS/softvoice/tibase32.dll"
    import_addon_asset 'softvoice*.nvda-addon' 'synthDrivers/tieng32.dll' "$ASSETS/softvoice/tieng32.dll"
    import_addon_asset 'softvoice*.nvda-addon' 'synthDrivers/TISPAN32.DLL' "$ASSETS/softvoice/TISPAN32.DLL"
    import_addon_asset 'amigaNarrator*.nvda-addon' 'synthDrivers/_amigaNarrator/narrator.device' "$ASSETS/amiganarrator/narrator.device"
    import_addon_asset 'amigaNarrator*.nvda-addon' 'synthDrivers/_amigaNarrator/translator.library' "$ASSETS/amiganarrator/translator.library"
    import_addon_asset 'amigaNarrator*.nvda-addon' 'synthDrivers/_amigaNarrator/cmudict.txt' "$ASSETS/amiganarrator/cmudict.txt"
import_addon_asset 'WinTalker*.nvda-addon' 'synthDrivers/wintalker_data/x64/WinTalker.dll' "$ASSETS/wintalker/WinTalker.dll"
import_addon_asset 'WinTalker*.nvda-addon' 'synthDrivers/wintalker_data/English.lex' "$ASSETS/wintalker/English.lex"
import_addon_asset 'echotalk*.nvda-addon' 'synthDrivers/echotalk/textalker.obj.bin' "$ASSETS/echotalk/textalker.obj.bin"
import_addon_asset 'echotalk*.nvda-addon' 'synthDrivers/echotalk/textalker.ram.bin' "$ASSETS/echotalk/textalker.ram.bin"
import_addon_asset 'echotalk*.nvda-addon' 'synthDrivers/echotalk/textalker_v13.obj.bin' "$ASSETS/echotalk/textalker_v13.obj.bin"
import_addon_asset 'echotalk*.nvda-addon' 'synthDrivers/echotalk/textalker_v13.ram.bin' "$ASSETS/echotalk/textalker_v13.ram.bin"

# TruVoice is native OpenTV now and takes no DLL: its constant tables are
# compiled into the front end that install.sh builds above.
#
# Sam, Mike, Mary and the OneCore voices read their own data files at run time.
# Keep them anywhere up to three levels above this source tree and they are
# imported by name; README.md lists them.
import_named_asset() {
    named_asset=$1
    named_destination=$2
    [ -f "$named_destination" ] && return 0
    named_source=$(find_addon "$named_asset") || return 0
    mkdir -p "$(dirname "$named_destination")"
    cp "$named_source" "$named_destination"
    say "Imported $named_asset."
}
import_named_asset 'Sam.spd'  "$ASSETS/mssam/Sam.spd"
import_named_asset 'Sam.sdf'  "$ASSETS/mssam/Sam.sdf"
import_named_asset 'Mike.spd' "$ASSETS/mssam/Mike.spd"
import_named_asset 'Mike.sdf' "$ASSETS/mssam/Mike.sdf"
import_named_asset 'Mary.spd' "$ASSETS/mssam/Mary.spd"
import_named_asset 'Mary.sdf' "$ASSETS/mssam/Mary.sdf"
import_named_asset 'LTTS1033.LXA' "$ASSETS/mssam/LTTS1033.LXA"
import_named_asset 'r1033tts.LXA' "$ASSETS/mssam/r1033tts.LXA"
import_named_asset 'MSTTSLocEnUS.dat' "$ASSETS/onecore/MSTTSLocEnUS.dat"
for onecore_part in APM BEP INI; do
    for onecore_voice in David Zira Mark; do
        import_named_asset "M1033$onecore_voice.$onecore_part" \
            "$ASSETS/onecore/M1033$onecore_voice.$onecore_part"
    done
done

# The L&H TTS3000 packages are Microsoft CAB self-extractors.  Flattening the
# DLLs is intentional: the native shim resolves every plugin by basename.
for language in dun eng enu frf ged iti jpj kok ptb rur spe; do
    extract_cab_dlls "lhtts$language.exe" "$ASSETS/lhtts"
done

if [ ! -d "$ASSETS/outspoken/outspoken-roms" ]; then
    outspoken_archive=$(find_addon 'outspoken-roms.zip') || outspoken_archive=
    if [ -n "$outspoken_archive" ]; then
        mkdir -p "$ASSETS/outspoken"
        python3 -c 'import pathlib, sys, zipfile
archive, target = sys.argv[1:]
root = pathlib.Path(target).resolve()
with zipfile.ZipFile(archive) as source:
    for info in source.infolist():
        name = pathlib.PurePosixPath(info.filename.replace("\\", "/"))
        if info.is_dir() or not name.parts or name.parts[0] != "outspoken-roms":
            continue
        relative = pathlib.Path(*name.parts)
        output = root / relative
        output.parent.mkdir(parents=True, exist_ok=True)
        if not output.exists():
            output.write_bytes(source.read(info))' "$outspoken_archive" "$ASSETS/outspoken"
        say "Imported OutSpoken ROM collection from $(basename "$outspoken_archive")."
    fi
fi

cat >"$CLI" <<EOF
#!/bin/sh
INSTALL_DIR='$INSTALL_DIR'
export PYTHONPATH="\$INSTALL_DIR/app/src:\$INSTALL_DIR/vendor\${PYTHONPATH:+:\$PYTHONPATH}"
export LD_LIBRARY_PATH="\$INSTALL_DIR/vendor/unicorn/lib\${LD_LIBRARY_PATH:+:\$LD_LIBRARY_PATH}"
export RETRO_TTS_SOFTVOICE_SHIM="\$INSTALL_DIR/lib/libsv_shim.so"
export RETRO_TTS_SOFTVOICE_BASE_DLL="\$INSTALL_DIR/assets/softvoice/tibase32.dll"
export RETRO_TTS_SOFTVOICE_LANGUAGE_DLL="\$INSTALL_DIR/assets/softvoice/tieng32.dll"
export RETRO_TTS_SOFTVOICE_SPANISH_DLL="\$INSTALL_DIR/assets/softvoice/TISPAN32.DLL"
export RETRO_TTS_LHTTS_SHIM="\$INSTALL_DIR/lib/liblhtts_shim.$architecture.so"
export RETRO_TTS_LHTTS_DATA="\$INSTALL_DIR/assets/lhtts"
export RETRO_TTS_TRUEVOICE_CLI="\$INSTALL_DIR/bin/tv_cli"
export RETRO_TTS_MSSAM_CLI="\$INSTALL_DIR/bin/sam_say"
export RETRO_TTS_MSSAM_DATA="\$INSTALL_DIR/assets/mssam"
export RETRO_TTS_ONECORE_CLI="\$INSTALL_DIR/bin/zira_say"
export RETRO_TTS_ONECORE_DATA="\$INSTALL_DIR/assets/onecore"
export RETRO_TTS_BESTSPEECH_CLI="\$INSTALL_DIR/bin/bst_cli"
export RETRO_TTS_AUDIO_LIB="\$INSTALL_DIR/lib/libretro_audio.so"
export RETRO_TTS_ECHOTALK_LIB="\$INSTALL_DIR/lib/libechotalk.$architecture.so"
export RETRO_TTS_ECHOTALK_DATA="\$INSTALL_DIR/assets/echotalk"
export RETRO_TTS_OUTSPOKEN_HOST="\$INSTALL_DIR/lib/libosp_host.$architecture.so"
export RETRO_TTS_OUTSPOKEN_ROMS="\$INSTALL_DIR/assets/outspoken/outspoken-roms"
exec ${RETRO_TTS_PYTHON:-python3} -m retro_tts.cli "\$@"
EOF
chmod 755 "$CLI"

# Speech Dispatcher starts GenericExecuteSynth once per utterance, and what it
# starts is a process: a fresh Python interpreter costs roughly 40-50 ms there,
# which measured is about half the wait before a word is heard. Build the native
# client whenever a compiler is present. It plays through pw-play, paplay or
# aplay, so PipeWire is not needed for the engines that hand their audio back to
# the client. The regular Python CLI remains for diagnostics and manual use.
SPEECHD_CLIENT="$CLI"
CLIENT_SOURCE="$INSTALL_DIR/app/retro_tts_client.c"
CLIENT_BINARY="$INSTALL_DIR/bin/retro-tts-client"
if command -v cc >/dev/null 2>&1 &&
   cc -O2 -Wall -Wextra -o "$CLIENT_BINARY.tmp" "$CLIENT_SOURCE"; then
    chmod 755 "$CLIENT_BINARY.tmp"
    mv "$CLIENT_BINARY.tmp" "$CLIENT_BINARY"
    SPEECHD_CLIENT="$CLIENT_BINARY"
    say "Enabled the low-latency native Speech Dispatcher client."
else
    rm -f "$CLIENT_BINARY.tmp"
    warn "using the Python Speech Dispatcher client (install a C compiler for lower onset latency)"
fi

# The three engines added with the vendored native sources are compiled here,
# at install time, from native/.  That is deliberate and it is the whole point
# of doing it this way: there is no per-architecture binary to ship for them,
# only C, so unlike the shimmed engines they run on ARM64 as well as x86_64.
build_native_engine() {
    engine_name=$1
    engine_script=$2
    engine_binary=$3
    if [ ! -f "$ROOT/native/$engine_script/build.sh" ]; then
        [ -x "$INSTALL_DIR/bin/$engine_binary" ] ||
            warn "$engine_name: native/$engine_script/build.sh is missing; bin/$engine_binary was not built"
        return 0
    fi
    if ! command -v cc >/dev/null 2>&1; then
        [ -x "$INSTALL_DIR/bin/$engine_binary" ] ||
            warn "$engine_name needs a C compiler (cc); bin/$engine_binary was not built"
        return 0
    fi
    native_work=$(mktemp -d "${TMPDIR:-/tmp}/retro-tts-build.XXXXXX")
    if OUT="$native_work" CC=cc sh "$ROOT/native/$engine_script/build.sh" \
         >/dev/null 2>"$native_work/build.log" &&
       [ -x "$native_work/$engine_binary" ]; then
        mkdir -p "$INSTALL_DIR/bin"
        cp "$native_work/$engine_binary" "$INSTALL_DIR/bin/$engine_binary.tmp"
        mv -f "$INSTALL_DIR/bin/$engine_binary.tmp" "$INSTALL_DIR/bin/$engine_binary"
        chmod 755 "$INSTALL_DIR/bin/$engine_binary"
        say "Built $engine_name from source."
    else
        warn "could not build $engine_name; see $native_work/build.log"
    fi
    rm -rf "$native_work"
}
build_native_engine "Centigram TruVoice" opentv tv_cli
build_native_engine "Microsoft Sam, Mike and Mary" sam sam_say
build_native_engine "Microsoft David, Zira and Mark" onecore zira_say

# The pause shortener runs on the output of every engine, which makes it the
# one piece of per-sample work the whole pack pays for on every utterance.
# native/audio builds it as a small shared library; engines/audio.py keeps
# its Python version and uses it when a machine has no compiler.
if [ -f "$ROOT/native/audio/build.sh" ] && command -v cc >/dev/null 2>&1; then
    audio_work=$(mktemp -d "${TMPDIR:-/tmp}/retro-tts-build.XXXXXX")
    if OUT="$audio_work" CC=cc sh "$ROOT/native/audio/build.sh" \
         >/dev/null 2>"$audio_work/build.log" &&
       [ -f "$audio_work/libretro_audio.so" ]; then
        mkdir -p "$INSTALL_DIR/lib"
        cp "$audio_work/libretro_audio.so" "$INSTALL_DIR/lib/libretro_audio.so.tmp"
        mv -f "$INSTALL_DIR/lib/libretro_audio.so.tmp" \
            "$INSTALL_DIR/lib/libretro_audio.so"
        chmod 755 "$INSTALL_DIR/lib/libretro_audio.so"
        say "Built the pause shortener from source."
    else
        warn "could not build the pause shortener; the Python one will be used"
    fi
    rm -rf "$audio_work"
fi
build_native_engine "BeSTspeech / Keynote Gold" openbst bst_cli

# The Apple generations run through Panthera's own i686 host, which maps the
# Mach-O engine directly: no Wine, no emulation, no CPU translation. It carries
# its own Glint AAC decoder, so what it wants from the system is only the 32-bit
# runtime libraries: libc6:i386 and libstdc++6:i386, and libsqlite3-0:i386 for
# Leopard's phrasing dictionary. Wine stays as the fallback for machines without
# those, where the PE host is the only route left.
PANTHERA_HOST="$INSTALL_DIR/bin/panthera_host"
if [ -f "$ASSETS/panthera/tiger_host" ]; then
    cp "$ASSETS/panthera/tiger_host" "$PANTHERA_HOST"
    chmod 755 "$PANTHERA_HOST"
fi
LEOPARD_HOST="$INSTALL_DIR/bin/leopard_host.exe"
LEOPARD_BACKEND=wine
TIGER_HOST="$INSTALL_DIR/bin/leopard_host.exe"
TIGER_BACKEND=wine
LION_HOST="$INSTALL_DIR/bin/panthera_host.exe"
LION_BACKEND=wine
if [ -x "$PANTHERA_HOST" ] && "$PANTHERA_HOST" --aac-check >/dev/null 2>&1; then
    LEOPARD_HOST="$PANTHERA_HOST"
    LEOPARD_BACKEND=native
    TIGER_HOST="$PANTHERA_HOST"
    TIGER_BACKEND=native
    LION_HOST="$PANTHERA_HOST"
    LION_BACKEND=native
    say "The Apple generations will run natively, without Wine."
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
Environment=RETRO_TTS_BESTSPEECH_CLI=$INSTALL_DIR/bin/bst_cli
Environment=RETRO_TTS_AUDIO_LIB=$INSTALL_DIR/lib/libretro_audio.so
Environment=RETRO_TTS_SOFTVOICE_SHIM=$INSTALL_DIR/lib/libsv_shim.so
Environment=RETRO_TTS_SOFTVOICE_BASE_DLL=$INSTALL_DIR/assets/softvoice/tibase32.dll
Environment=RETRO_TTS_SOFTVOICE_LANGUAGE_DLL=$INSTALL_DIR/assets/softvoice/tieng32.dll
Environment=RETRO_TTS_SOFTVOICE_SPANISH_DLL=$INSTALL_DIR/assets/softvoice/TISPAN32.DLL
Environment=RETRO_TTS_LHTTS_SHIM=$INSTALL_DIR/lib/liblhtts_shim.$architecture.so
Environment=RETRO_TTS_LHTTS_DATA=$INSTALL_DIR/assets/lhtts
Environment=RETRO_TTS_TRUEVOICE_CLI=$INSTALL_DIR/bin/tv_cli
Environment=RETRO_TTS_MSSAM_CLI=$INSTALL_DIR/bin/sam_say
Environment=RETRO_TTS_MSSAM_DATA=$INSTALL_DIR/assets/mssam
Environment=RETRO_TTS_ONECORE_CLI=$INSTALL_DIR/bin/zira_say
Environment=RETRO_TTS_ONECORE_DATA=$INSTALL_DIR/assets/onecore
Environment=RETRO_TTS_ECHOTALK_LIB=$INSTALL_DIR/lib/libechotalk.$architecture.so
Environment=RETRO_TTS_ECHOTALK_DATA=$INSTALL_DIR/assets/echotalk
Environment=RETRO_TTS_OUTSPOKEN_HOST=$INSTALL_DIR/lib/libosp_host.$architecture.so
Environment=RETRO_TTS_OUTSPOKEN_ROMS=$INSTALL_DIR/assets/outspoken/outspoken-roms
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
Environment=RETRO_TTS_TIGER_HOST=$TIGER_HOST
Environment=RETRO_TTS_TIGER_BACKEND=$TIGER_BACKEND
Environment=RETRO_TTS_TIGER_TREE=$INSTALL_DIR/assets/tigerspeech/tigerspeech-data
Environment=RETRO_TTS_TIGER_VOICE=Vicki
Environment=RETRO_TTS_LION_HOST=$LION_HOST
Environment=RETRO_TTS_LION_BACKEND=$LION_BACKEND
Environment=RETRO_TTS_LION_TREE=$INSTALL_DIR/assets/lionspeech/lionspeech-data
Environment=RETRO_TTS_LION_VOICE=Alex
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
exec ${RETRO_TTS_PYTHON:-python3} -m retro_tts.server
EOF
chmod 755 "$INSTALL_DIR/bin/retro-tts-server"

available_modules="sam stspeech"
# ⛔ A THIRD CATEGORY. "missing proprietary assets" used to absorb modules that
# CANNOT run on this CPU at all -- they need an arch-specific native shim this
# release builds only for x86_64. Telling a Pi 4 user to go find DLLs for a module
# that can never load is the misdirection behind issue #4.
unsupported_modules=""
missing_modules=""

has_all() {
    for path in "$@"; do
        [ -f "$path" ] || return 1
    done
}

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
# BeSTspeech is native openbst: no DLL and no shim, just the front end built
# above, so like TruVoice it is available on any CPU the pack supports.
if [ -x "$INSTALL_DIR/bin/bst_cli" ]; then
    available_modules="$available_modules bestspeech"
else
    missing_modules="$missing_modules bestspeech"
    warn "bestspeech needs bin/bst_cli; install.sh builds it when a C compiler (cc) is available."
fi
if has_all "$ASSETS/softvoice/tibase32.dll" "$ASSETS/softvoice/tieng32.dll"; then
    available_modules="$available_modules softvoice"
else missing_modules="$missing_modules softvoice"; fi
# ⛔ LHTTS SHIMS AN ARCH-SPECIFIC NATIVE LIBRARY, AND ONLY x86_64 IS BUILT.
# install.sh used to fold that into one "missing proprietary assets" bucket,
# which sent users of a Pi 4 hunting for DLLs they already had. The two causes
# are genuinely different and are now reported differently:
#   * the DLL assets are absent  -> an assets problem, fixable by the user
#   * the shim for this CPU is absent -> a PACKAGING gap, not fixable by the user
# See issue #4: on aarch64 the module can never be enabled however the assets are
# placed, because lib/liblhtts_shim.aarch64.so is not shipped at all.
# TruVoice was in this bucket too.  It is not any more: it is compiled from
# source at install time, so it runs on any CPU this pack supports (issue #6).
LHTTS_SHIM="$INSTALL_DIR/lib/liblhtts_shim.$architecture.so"
if has_all \
    "$ASSETS/lhtts/TTSMGR32.DLL" \
    "$ASSETS/lhtts/TTSDCT32.DLL" \
    "$ASSETS/lhtts/ENUG2P60.DLL" \
    "$ASSETS/lhtts/ENUCT260.DLL" \
    "$ASSETS/lhtts/ENUVM160.DLL" \
    "$LHTTS_SHIM"; then
    available_modules="$available_modules lhtts"
elif [ ! -f "$LHTTS_SHIM" ]; then
    unsupported_modules="$unsupported_modules lhtts"
    warn "lhtts cannot run on $architecture: $LHTTS_SHIM is not shipped (only x86_64 is built). The DLLs are irrelevant on this CPU."
else missing_modules="$missing_modules lhtts"; fi
# TruVoice is native OpenTV: no DLL, no shim, just the front end built above,
# so it is available on any CPU the pack supports (this is issue #6).
if [ -x "$INSTALL_DIR/bin/tv_cli" ]; then
    available_modules="$available_modules truevoice"
else
    missing_modules="$missing_modules truevoice"
    warn "truevoice needs bin/tv_cli; install.sh builds it when a C compiler (cc) is available."
fi
# Sam, Mike, Mary and the OneCore voices need both their data and the binary
# built from the vendored sources.
if [ ! -x "$INSTALL_DIR/bin/sam_say" ]; then
    warn "mssam needs bin/sam_say; install.sh builds it when a C compiler (cc) is available."
fi
if has_all \
    "$ASSETS/mssam/Sam.spd" "$ASSETS/mssam/Sam.sdf" \
    "$ASSETS/mssam/Mike.spd" "$ASSETS/mssam/Mike.sdf" \
    "$ASSETS/mssam/Mary.spd" "$ASSETS/mssam/Mary.sdf" \
    "$ASSETS/mssam/LTTS1033.LXA" "$ASSETS/mssam/r1033tts.LXA" \
    "$INSTALL_DIR/bin/sam_say"; then
    available_modules="$available_modules mssam"
else missing_modules="$missing_modules mssam"; fi
if [ ! -x "$INSTALL_DIR/bin/zira_say" ]; then
    warn "onecore needs bin/zira_say; install.sh builds it when a C compiler (cc) is available."
fi
if has_all \
    "$ASSETS/onecore/MSTTSLocEnUS.dat" \
    "$ASSETS/onecore/M1033David.APM" "$ASSETS/onecore/M1033David.BEP" "$ASSETS/onecore/M1033David.INI" \
    "$ASSETS/onecore/M1033Zira.APM" "$ASSETS/onecore/M1033Zira.BEP" "$ASSETS/onecore/M1033Zira.INI" \
    "$ASSETS/onecore/M1033Mark.APM" "$ASSETS/onecore/M1033Mark.BEP" "$ASSETS/onecore/M1033Mark.INI" \
    "$INSTALL_DIR/bin/zira_say"; then
    available_modules="$available_modules onecore"
else missing_modules="$missing_modules onecore"; fi
if has_all "$ASSETS/amiganarrator/narrator.device" &&
   { [ -f "$ASSETS/amiganarrator/translator.library" ] ||
     [ -f "$ASSETS/amiganarrator/cmudict.txt" ]; }; then
    available_modules="$available_modules amiganarrator"
else missing_modules="$missing_modules amiganarrator"; fi
if has_all \
    "$ASSETS/echotalk/textalker.obj.bin" \
    "$ASSETS/echotalk/textalker.ram.bin" \
    "$ASSETS/echotalk/textalker_v13.obj.bin" \
    "$ASSETS/echotalk/textalker_v13.ram.bin" \
    "$INSTALL_DIR/lib/libechotalk.$architecture.so"; then
    available_modules="$available_modules echotalk"
else missing_modules="$missing_modules echotalk"; fi
if [ -d "$ASSETS/outspoken/outspoken-roms" ] &&
   [ -f "$INSTALL_DIR/lib/libosp_host.$architecture.so" ]; then
    available_modules="$available_modules outspoken"
else missing_modules="$missing_modules outspoken"; fi
# Why a module cannot run, in the right words. A missing Wine installation or a
# missing 32-bit runtime is not missing engine data, and reporting it as such
# sends people looking in the wrong place.
APPLE_RUNTIME_NOTE="its engine data, and either Wine or the 32-bit runtime for the native host: libc6:i386 and libstdc++6:i386, plus libsqlite3-0:i386 for Leopard's dictionary"
missing_notes=""
skip_module() {
    missing_modules="$missing_modules $1"
    if [ -n "${2:-}" ]; then
        missing_notes="$missing_notes
  $1 needs $2"
    fi
    return 0
}

if [ "$architecture" = x86_64 ] &&
   has_all "$ASSETS/wintalker/WinTalker.dll" "$ASSETS/wintalker/English.lex" &&
   command -v wine >/dev/null 2>&1; then
    available_modules="$available_modules wintalker"
else skip_module wintalker "Wine"; fi
if [ "$architecture" = x86_64 ] &&
   has_all \
    "$ASSETS/leopardspeech/leopardspeech-data/Speech/Synthesizers/MacinTalk.SpeechSynthesizer/Contents/MacOS/MacinTalk" \
    "$ASSETS/leopardspeech/leopardspeech-data/SpeechDictionary.framework/Versions/A/SpeechDictionary" &&
   [ -d "$ASSETS/leopardspeech/leopardspeech-data/Speech/Voices" ] &&
   { { [ "$LEOPARD_BACKEND" = native ] && [ -x "$LEOPARD_HOST" ]; } ||
     { [ -f "$LEOPARD_HOST" ] && command -v wine >/dev/null 2>&1; }; }; then
    available_modules="$available_modules leopardspeech"
else skip_module leopardspeech "$APPLE_RUNTIME_NOTE"; fi
if [ "$architecture" = x86_64 ] &&
   has_all \
    "$ASSETS/tigerspeech/tigerspeech-data/Speech/Synthesizers/MacinTalk.SpeechSynthesizer/Contents/MacOS/MacinTalk" \
    "$ASSETS/tigerspeech/tigerspeech-data/SpeechDictionary.framework/Versions/A/SpeechDictionary" \
    "$INSTALL_DIR/bin/leopard_host.exe" &&
   [ -d "$ASSETS/tigerspeech/tigerspeech-data/Speech/Voices" ] &&
   { { [ "$TIGER_BACKEND" = native ] && [ -x "$TIGER_HOST" ]; } ||
     { [ -f "$TIGER_HOST" ] && command -v wine >/dev/null 2>&1; }; }; then
    available_modules="$available_modules tigerspeech"
else skip_module tigerspeech "$APPLE_RUNTIME_NOTE"; fi
if [ "$architecture" = x86_64 ] &&
   has_all \
    "$ASSETS/lionspeech/lionspeech-data/Speech/Synthesizers/MacinTalk.SpeechSynthesizer/Contents/MacOS/MacinTalk" \
    "$ASSETS/lionspeech/lionspeech-data/SpeechDictionary.framework/Versions/A/SpeechDictionary" \
    "$ASSETS/lionspeech/lionspeech-data/libstdc++.6.0.9.dylib" \
    "$ASSETS/lionspeech/lionspeech-data/libc++abi.dylib" \
    "$INSTALL_DIR/bin/panthera_host.exe" &&
   [ -d "$ASSETS/lionspeech/lionspeech-data/Speech/Voices" ] &&
   { { [ "$LION_BACKEND" = native ] && [ -x "$LION_HOST" ]; } ||
     { [ -f "$LION_HOST" ] && command -v wine >/dev/null 2>&1; }; }; then
    available_modules="$available_modules lionspeech"
else skip_module lionspeech "$APPLE_RUNTIME_NOTE"; fi

for module in $available_modules; do
    source="$INSTALL_DIR/config/modules/$module-generic.conf"
    target="$MODULE_DIR/$module-generic.conf"
    sed "s|retro-tts|$SPEECHD_CLIENT|g" "$source" >"$target"
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

# ⛔ DO NOT REGISTER MODULES WITH AddModule HERE. This used to append an
# AddModule list, and that list did nothing but BREAK things:
#
#   * One explicit AddModule makes speech-dispatcher stop discovering its
#     standard modules. Measured on 0.12: with the block present it loaded ONLY
#     the entries registered here, and the stock ones it named failed anyway (a
#     bare binary name like "sd_espeak-ng" plus a non-generic config name like
#     "espeak-ng.conf" does not load). The retro entries were what survived, and
#     "sam" sorts first -- one survivor of a broken list. That is the reported
#     "sam-generic jump scare".
#   * It was unnecessary. speech-dispatcher ALREADY finds this pack's modules by
#     directory: configs go to $MODULE_DIR, binaries to $INSTALL_DIR/bin, and its
#     log reports "Module name=sam-generic being inserted into detected_modules
#     list" with NO AddModule line anywhere.
#
# So the correct state is a speechd.conf with no RETRO-TTS-PACK block at all.
# Strip one if an older installer left it, or upgrading stays broken.
if [ -f "$SPEECHD_CONF" ] && grep -q 'BEGIN RETRO-TTS-PACK' "$SPEECHD_CONF"; then
    sed -i '/# BEGIN RETRO-TTS-PACK/,/# END RETRO-TTS-PACK/d' "$SPEECHD_CONF"
    say "Removed this pack's AddModule block from $(basename "$SPEECHD_CONF"); speech-dispatcher discovers these modules by directory."
fi

# Install the pack's patched Speech Dispatcher generic module, built from
# native/speech-dispatcher/generic-real-voice-names.patch: it reports the real
# AddVoice names to Orca instead of the repeated "MALE1"/"FEMALE1" variant
# (issue #7). Speech Dispatcher looks for the sd_generic binary in the user
# module dir before the system one, so placing it there shadows the stock
# module for every *-generic.conf without touching system files.
USER_MODULE_DIR="$DATA_HOME/../libexec/speech-dispatcher-modules"
mkdir -p "$USER_MODULE_DIR"
case "$architecture" in
    x86_64)
        if [ -f "$ROOT/bin/sd_retro_generic.x86_64" ]; then
            cp "$ROOT/bin/sd_retro_generic.x86_64" "$USER_MODULE_DIR/sd_generic"
            chmod 755 "$USER_MODULE_DIR/sd_generic"
            say "Installed the pack's patched Speech Dispatcher generic module (Orca lists real voice names)."
        else
            warn "sd_retro_generic.x86_64 not found in the pack; Orca may list voices by variant (e.g. Male1)"
        fi
        ;;
    *)
        warn "no patched Speech Dispatcher generic module ships for $architecture; Orca may list voices by variant (e.g. Male1)"
        ;;
esac

if [ "${RETRO_TTS_SKIP_SYSTEMD:-0}" != 1 ] &&
   command -v systemctl >/dev/null 2>&1 &&
   systemctl --user show-environment >/dev/null 2>&1; then
    systemctl --user daemon-reload
    if systemctl --user enable retro-tts.service >/dev/null 2>&1 &&
       systemctl --user restart retro-tts.service; then
        say "Persistent renderer enabled and restarted."
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
if [ -n "$unsupported_modules" ]; then
    say "Not available on this CPU ($architecture):$unsupported_modules"
    say "  These need an arch-specific native shim this release ships only for x86_64."
    say "  No asset can fix it -- see issue #4."
fi
if [ -n "$missing_modules" ]; then
    say "Skipped modules that cannot run here:$missing_modules"
    if [ -n "$missing_notes" ]; then
        printf '%s\n' "$missing_notes"
    fi
    say "Copy those assets into $ASSETS and run install.sh again."
fi
say "Installation directory: $INSTALL_DIR"
say "See $INSTALL_DIR/README.md for asset names and troubleshooting."
