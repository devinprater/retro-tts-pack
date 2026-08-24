/*
 * Recreated command-line host for Centigram TruVoice 5.10.
 *
 * The supplied 2026 cgrm_spk.exe retains its original object and function
 * symbols.  Its observable call sequence and option ranges were recovered
 * from those symbols, its quick guide, and TV_ENG32.DLL's public tts_* export
 * table.  This native version keeps the useful interface while replacing
 * LoadLibrary/WinMM with the pack's Unicorn compatibility shim.
 */
#include "truevoice_shim.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

struct Output { FILE *file; };

static void write_audio(const int16_t *data, size_t count, void *opaque) {
    Output *output = static_cast<Output *>(opaque);
    std::fwrite(data, sizeof(int16_t), count, output->file);
}

static void usage(const char *program) {
    std::fprintf(stderr,
        "Usage: %s --data DIR --filename FILE [options] text\n"
        "  --voice 0..9       Peter, Sidney, Eddie, Douglas, Biff, Amos,\n"
        "                     Melvin, Alex, Wanda, or Julia\n"
        "  --rate 50..250     speaking rate (default 150)\n"
        "  --pitch 50..400    pitch (default 150)\n"
        "  --volume 0..16     volume (default 14)\n", program);
}

int main(int argc, char **argv) {
    const char *data = nullptr;
    const char *filename = nullptr;
    int voice = 0, rate = 150, pitch = 150, volume = 14;
    std::string text;
    for (int i = 1; i < argc; ++i) {
        auto value = [&](const char *option) -> const char * {
            if (++i >= argc) { std::fprintf(stderr, "%s requires a value\n", option); std::exit(2); }
            return argv[i];
        };
        if (!std::strcmp(argv[i], "--data")) data = value("--data");
        else if (!std::strcmp(argv[i], "--filename") || !std::strcmp(argv[i], "-f")) filename = value("--filename");
        else if (!std::strcmp(argv[i], "--voice")) voice = std::atoi(value("--voice"));
        else if (!std::strcmp(argv[i], "--rate")) rate = std::atoi(value("--rate"));
        else if (!std::strcmp(argv[i], "--pitch")) pitch = std::atoi(value("--pitch"));
        else if (!std::strcmp(argv[i], "--volume")) volume = std::atoi(value("--volume"));
        else if (!std::strcmp(argv[i], "--help") || !std::strcmp(argv[i], "-h")) { usage(argv[0]); return 0; }
        else { if (!text.empty()) text += ' '; text += argv[i]; }
    }
    if (!data || !filename || text.empty()) { usage(argv[0]); return 2; }
    tv_engine *engine = tv_create(data);
    if (!engine || cgrm_init(engine) != 0) {
        std::fprintf(stderr, "TruVoice initialization failed\n");
        if (engine) tv_destroy(engine);
        return 1;
    }
    FILE *file = !std::strcmp(filename, "-") ? stdout : std::fopen(filename, "wb");
    if (!file) { std::perror(filename); tv_destroy(engine); return 1; }
    Output output{file};
    int rc = cgrm_speak(engine, text.c_str(), voice, rate, pitch, volume,
                        write_audio, &output);
    if (file == stdout) std::fflush(file); else std::fclose(file);
    tv_destroy(engine);
    if (rc != 0) std::fprintf(stderr, "TruVoice synthesis failed (%d)\n", rc);
    return rc == 0 ? 0 : 1;
}
