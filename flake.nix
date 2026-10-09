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
        retro-tts-pack = pkgs.stdenv.mkDerivation {
          pname = "retro-tts-pack";
          inherit version;
          src = pkgs.lib.cleanSource ./.;

          nativeBuildInputs = [ pkgs.patchelf ];

          # Prebuilt binaries; stripping foreign-arch objects is not wanted.
          dontStrip = true;

          installPhase = ''
            runHook preInstall
            mkdir -p $out/share/retro-tts-pack $out/bin
            cp -a . $out/share/retro-tts-pack/
            # Entry point: runs install.sh from the store with a python3 that
            # exists on NixOS. install.sh bakes RETRO_TTS_PYTHON into the
            # generated ~/.local/bin/retro-tts wrapper (plain `python3`
            # when the variable is unset, so non-Nix installs are unaffected).
            cat > $out/bin/retro-tts-pack-install <<WRAPPER
            #!${pkgs.runtimeShell}
            export RETRO_TTS_PYTHON=${pkgs.python3}/bin/python3
            exec $out/share/retro-tts-pack/install.sh "$@"
            WRAPPER
            chmod +x $out/bin/retro-tts-pack-install
            runHook postInstall
          '';

          # The prebuilt binaries expect /lib64/ld-linux-x86-64.so.2, which
          # does not exist on NixOS. Point executables at the nix loader and
          # give every x86_64 object the libraries above via rpath, preserving
          # any existing entries (notably the $ORIGIN RUNPATH on cgrm_spk).
          # aarch64 objects ship for Pi users and are left untouched.
          postFixup = ''
            interp="$(cat ${pkgs.stdenv.cc}/nix-support/dynamic-linker)"
            libpath="${pkgs.lib.makeLibraryPath ttsLibs}"
            find "$out/share/retro-tts-pack" -type f -print0 |
            while IFS= read -r -d "" f; do
              if ! head -c 4 "$f" | grep -q $'\x7fELF'; then continue; fi
              # e_machine == EM_X86_64 (62); skip everything else.
              if [ "$(od -An -t u2 -j 18 -N 2 "$f" | tr -d ' ')" != "62" ]; then continue; fi
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
            done || true
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
        packages.default = retro-tts-pack;
        apps.install = {
          type = "app";
          program = "${retro-tts-pack}/bin/retro-tts-pack-install";
        };
      });
}
