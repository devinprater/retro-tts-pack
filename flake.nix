{
  description = "Retro TTS pack: vintage speech synthesizers (EchoTalk, BeSTSpeech, TruVoice, ...) for Speech Dispatcher and Orca";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-25.11";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs = { self, nixpkgs, flake-utils }:
    # The pack ships prebuilt x86_64 binaries (the aarch64 .so files are inert
    # data for Pi users), so only x86_64-linux is packaged.
    flake-utils.lib.eachSystem [ "x86_64-linux" ] (system:
      let
        pkgs = nixpkgs.legacyPackages.${system};
        version = builtins.replaceStrings [ "\n" ] [ "" ] (builtins.readFile ./VERSION);
        # Shared libraries the prebuilt binaries link against.
        ttsLibs = with pkgs; [
          stdenv.cc.cc.lib # libc, libm, libstdc++, libgcc_s, libpthread
          alsa-lib # libasound.so.2
          dotconf # libdotconf.so.0
          glib # libglib-2.0.so.0
          libao # libao.so.4
          libtool # libltdl.so.7
          pipewire # libpipewire-0.3.so.0
          pulseaudio # libpulse.so.0
        ];
        # 32-bit libraries for the Panthera host (Intel 80386): glibc, libgcc
        # and libstdc++, plus SQLite for Leopard's phrasing dictionary. It needs
        # no FFmpeg; its AAC decoder is compiled into it.
        ttsLibs32 = with pkgs.pkgsi686Linux; [
          stdenv.cc.cc.lib
          sqlite # libsqlite3
        ];
        # Optional Panthera and Microsoft data, pinned to upstream archive hashes.
        optionalAssets = map (asset: asset // {
          archive = pkgs.fetchurl { inherit (asset) url sha256; };
        }) [
          { url = "https://quintonwilliams.me/sapple/Sapple-1.1.1-mac.zip"; sha256 = "59402650896a33419a80a62ddc36e860812b46d1a28acef511961d2ae5e957b8"; }
          { url = "https://github.com/tgeczy/panthera-speech/releases/download/pantheraspeech/v3.3.2/panthera-linux-i686-3.3.2.tar.gz"; sha256 = "f7b61d3af84d22f98e4028db62525ca2c0c004dd6ccd5b133dfb7ee5e3a6f6c7"; }
          { url = "https://dectalk.nu/Software%20and%20Manuals/Software/Apple/leopard.zip"; sha256 = "d830eb0306fbc0044041a71f2eac16412af875aade2ac124b1ae5d30a045c76b"; }
          { url = "https://dectalk.nu/Software%20and%20Manuals/Software/Apple/tiger.zip"; sha256 = "681da8c2da2be4161659e4374db3b2cd4b424e86ff2b29f5fa0f886900cd0f3e"; }
          { url = "https://dectalk.nu/Software%20and%20Manuals/Software/Apple/lion.zip"; sha256 = "c6ee230e556bd5722407fdd58e73701ac8a696822c53c919ccf2be6c6e58936c"; }
        ];
        mkRetroTtsPack = lionOnly: pkgs.stdenv.mkDerivation {
          pname = "retro-tts-pack";
          inherit version;
          src = ./.; # The Git flake source preserves the bundled shared libraries.

          nativeBuildInputs = [ pkgs.patchelf pkgs.python3 ];
          dontBuild = true;

          # Prebuilt binaries; stripping foreign-arch objects is not wanted.
          dontStrip = true;

          installPhase = ''
            runHook preInstall
            mkdir -p $out/share/retro-tts-pack $out/bin
            cp -a . $out/share/retro-tts-pack/
            cd $out/share/retro-tts-pack
            OUT="$TMPDIR/openbst" CC=cc sh native/openbst/build.sh
            install -m755 "$TMPDIR/openbst/bst_cli" bin/bst_cli
            # Build Microsoft engines from the locked pack source.
            for engine in sam sapi4 onecore; do
              OUT="$TMPDIR/$engine" CC=cc sh "native/$engine/build.sh"
            done
            install -m755 "$TMPDIR/sam/sam_say" bin/sam_say
            install -m755 "$TMPDIR/sapi4/sapi4_speak" bin/sapi4_speak
            install -m755 "$TMPDIR/onecore/zira_say" bin/zira_say

            # Reuse upstream extraction and file verification without networking.
            ${pkgs.python3}/bin/python3 - <<'PY_ASSETS'
            import json, runpy, tarfile, zipfile
            from pathlib import Path
            helpers = runpy.run_path("download-assets.py")
            specs = {item["url"]: item for item in helpers["DOWNLOADS"] if "url" in item}
            archives = json.loads(r"""${builtins.toJSON (map (asset: { inherit (asset) url; archive = toString asset.archive; }) (builtins.filter (asset: !lionOnly || !(builtins.elem asset.url [ "https://dectalk.nu/Software%20and%20Manuals/Software/Apple/tiger.zip" "https://dectalk.nu/Software%20and%20Manuals/Software/Apple/leopard.zip" ])) optionalAssets))}""")
            for item in archives:
                spec = specs[item["url"]]
                opener = tarfile.open if spec.get("tar") else zipfile.ZipFile
                with opener(item["archive"]) as archive:
                    if "files" in spec:
                        for member, (relative, checksum) in spec["files"].items():
                            helpers["install_file"](Path("assets") / relative,
                                helpers["member_bytes"](archive, member), checksum)
                    else:
                        candidates, relative = spec["tree_candidates"]
                        helpers["install_tree_candidates"](archive, candidates, Path("assets") / relative)
            PY_ASSETS

            install -m755 assets/panthera/tiger_host bin/panthera_host
            ${pkgs.lib.optionalString lionOnly "rm -rf assets/tigerspeech assets/leopardspeech; rm -f config/modules/tigerspeech-generic.conf config/modules/leopardspeech-generic.conf"}
            # Entry point: runs install.sh from the store with a python3 that
            # exists on NixOS. install.sh bakes RETRO_TTS_PYTHON into the
            # generated ~/.local/bin/retro-tts wrapper (plain `python3`
            # when the variable is unset, so non-Nix installs are unaffected).
            cat > $out/bin/retro-tts-pack-install <<WRAPPER
            #!${pkgs.runtimeShell}
            export RETRO_TTS_PYTHON=${pkgs.python3}/bin/python3
            exec $out/share/retro-tts-pack/install.sh "\$@"
            WRAPPER
            chmod +x $out/bin/retro-tts-pack-install
            runHook postInstall
          '';

          # The prebuilt binaries expect /lib64/ld-linux-x86-64.so.2, which
          # does not exist on NixOS. Point executables at the nix loader and
          # give every x86 object the libraries above via rpath, preserving
          # any existing entries (notably the $ORIGIN RUNPATH on cgrm_spk).
          # The Panthera host is 32-bit and gets the i686 loader and libraries.
          # aarch64 objects ship for Pi users and are left untouched.
          postFixup = ''
            interp64="$(cat ${pkgs.stdenv.cc}/nix-support/dynamic-linker)"
            libpath64="${pkgs.lib.makeLibraryPath ttsLibs}"
            interp32="$(cat ${pkgs.pkgsi686Linux.stdenv.cc}/nix-support/dynamic-linker)"
            libpath32="${pkgs.lib.makeLibraryPath ttsLibs32}"
            find "$out/share/retro-tts-pack" -type f -print0 |
            while IFS= read -r -d "" f; do
              if ! head -c 4 "$f" | grep -q $'\x7fELF'; then continue; fi
              # e_machine: 62 = x86-64, 3 = 80386; anything else is left alone.
              mach="$(od -An -t u2 -j 18 -N 2 "$f" | tr -d ' ')"
              case "$mach" in
                62) interp="$interp64"; libpath="$libpath64" ;;
                3) interp="$interp32"; libpath="$libpath32" ;;
                *) continue ;;
              esac
              if patchelf --print-interpreter "$f" >/dev/null 2>&1; then
                patchelf --set-interpreter "$interp" "$f"
              fi
              if [ -n "$(patchelf --print-needed "$f" 2>/dev/null)" ]; then
                old_rpath="$(patchelf --print-rpath "$f" 2>/dev/null)"
                if [ -n "$old_rpath" ]; then
                  patchelf --set-rpath "$libpath:$old_rpath" "$f"
                else
                  patchelf --set-rpath "$libpath" "$f"
                fi
              fi
            done
          '';

          meta = with pkgs.lib; {
            description = "Vintage speech synthesizers for Speech Dispatcher and Orca";
            homepage = "https://github.com/devinprater/retro-tts-pack";
            # The pack bundles third-party engine binaries; see licenses/.
            platforms = [ "x86_64-linux" ];
            mainProgram = "retro-tts-pack-install";
          };
        };
      in
      {
        packages.default = mkRetroTtsPack false;
        packages.lion-only = mkRetroTtsPack true;
        apps.install = {
          type = "app";
          program = "${mkRetroTtsPack false}/bin/retro-tts-pack-install";
        };
      });
}
