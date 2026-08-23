#define _GNU_SOURCE
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

static volatile sig_atomic_t server_fd = -1;
static volatile sig_atomic_t player_pid = -1;

static void interrupted(int signal_number) {
    if (server_fd >= 0) close(server_fd);
    if (player_pid > 0) kill(player_pid, SIGTERM);
    _exit(128 + signal_number);
}

static int write_all(int fd, const void *buffer, size_t length) {
    const unsigned char *data = buffer;
    while (length) {
        ssize_t count = write(fd, data, length);
        if (count < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        data += count;
        length -= (size_t)count;
    }
    return 0;
}

static int read_all(int fd, void *buffer, size_t length) {
    unsigned char *data = buffer;
    while (length) {
        ssize_t count = read(fd, data, length);
        if (count == 0) return -1;
        if (count < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        data += count;
        length -= (size_t)count;
    }
    return 0;
}

static char *json_escape(const char *input) {
    size_t size = 1;
    for (const unsigned char *p = (const unsigned char *)input; *p; ++p)
        size += (*p == '"' || *p == '\\') ? 2 : (*p < 0x20 ? 6 : 1);
    char *output = malloc(size), *q = output;
    if (!output) return NULL;
    for (const unsigned char *p = (const unsigned char *)input; *p; ++p) {
        if (*p == '"' || *p == '\\') { *q++ = '\\'; *q++ = (char)*p; }
        else if (*p < 0x20) { snprintf(q, 7, "\\u%04x", *p); q += 6; }
        else *q++ = (char)*p;
    }
    *q = 0;
    return output;
}

static const char *option_value(int argc, char **argv, const char *name,
                                const char *fallback) {
    for (int i = 1; i + 1 < argc; ++i)
        if (!strcmp(argv[i], name)) return argv[i + 1];
    return fallback;
}

int main(int argc, char **argv) {
    const char *engine = option_value(argc, argv, "--engine", NULL);
    const char *text = option_value(argc, argv, "--text", NULL);
    const char *rate = option_value(argc, argv, "--rate", "50");
    const char *pitch = option_value(argc, argv, "--pitch", "50");
    const char *volume = option_value(argc, argv, "--volume", "90");
    const char *voice = option_value(argc, argv, "--voice", "");
    const char *output = option_value(argc, argv, "--output", NULL);
    if (!engine || !text) {
        fprintf(stderr, "usage: retro-tts-client --engine NAME --text TEXT [--rate N] [--pitch N] [--volume N] [--voice NAME] [--output WAV]\n");
        return 2;
    }
    /* sd_generic treats a nonzero GenericExecuteSynth result as a fatal
       module failure. A single transient engine or PipeWire error must drop
       only that utterance, not remove the synthesizer until Dispatcher is
       restarted. --output retains normal nonzero diagnostic behavior. */
    const int failure = output ? 1 : 0;
    signal(SIGPIPE, SIG_IGN);
    char *e_engine = json_escape(engine), *e_text = json_escape(text);
    char *e_voice = json_escape(voice);
    if (!e_engine || !e_text || !e_voice) return 1;
    size_t json_size = strlen(e_engine) + strlen(e_text) + strlen(e_voice) + 192;
    char *json = malloc(json_size);
    if (!json) return 1;
    int json_length = snprintf(json, json_size,
        "{\"engine\":\"%s\",\"text\":\"%s\",\"rate\":%d,\"pitch\":%d,"
        "\"volume\":%d,\"voice\":\"%s\",\"play\":false}",
        e_engine, e_text, atoi(rate), atoi(pitch), atoi(volume), e_voice);
    free(e_engine); free(e_text); free(e_voice);
    if (json_length < 0 || (size_t)json_length >= json_size) return 1;

    const char *socket_path = getenv("RETRO_TTS_SOCKET");
    char default_socket[sizeof(((struct sockaddr_un *)0)->sun_path)];
    if (!socket_path) {
        const char *runtime = getenv("XDG_RUNTIME_DIR");
        if (!runtime) runtime = "/tmp";
        snprintf(default_socket, sizeof(default_socket), "%s/retro-tts.sock", runtime);
        socket_path = default_socket;
    }
    struct sockaddr_un address = { .sun_family = AF_UNIX };
    if (strlen(socket_path) >= sizeof(address.sun_path)) return 1;
    strcpy(address.sun_path, socket_path);
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0 || connect(fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("retro-tts socket"); return failure;
    }
    server_fd = fd;
    signal(SIGTERM, interrupted); signal(SIGINT, interrupted);
    uint32_t request_length = htonl((uint32_t)json_length);
    if (write_all(fd, &request_length, 4) || write_all(fd, json, json_length)) return failure;
    free(json);
    unsigned char header[5];
    if (read_all(fd, header, sizeof(header))) return failure;
    uint32_t response_length;
    memcpy(&response_length, header + 1, 4);
    response_length = ntohl(response_length);
    unsigned char *wav = malloc(response_length ? response_length : 1);
    if (!wav || read_all(fd, wav, response_length)) return failure;
    close(fd); server_fd = -1;
    if (header[0]) {
        fwrite(wav, 1, response_length, stderr); fputc('\n', stderr); free(wav); return failure;
    }
    if (output) {
        int out = open(output, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        int result = out < 0 || write_all(out, wav, response_length);
        if (out >= 0) close(out);
        free(wav); return result;
    }
    int audio = memfd_create("retro-tts-audio", 0);
    if (audio < 0 || write_all(audio, wav, response_length) || lseek(audio, 0, SEEK_SET) < 0)
        return failure;
    free(wav);
    char path[64]; snprintf(path, sizeof(path), "/proc/self/fd/%d", audio);
    pid_t child = fork();
    if (child == 0) {
        execlp("pw-play", "pw-play", "--latency", "10ms", path, (char *)NULL);
        _exit(127);
    }
    if (child < 0) return failure;
    player_pid = child;
    int status;
    while (waitpid(child, &status, 0) < 0 && errno == EINTR) {}
    player_pid = -1; close(audio);
    return WIFEXITED(status) && WEXITSTATUS(status) == 0 ? 0 : failure;
}
