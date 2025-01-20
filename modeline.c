#define _GNU_SOURCE
#include "util.h"
#include <assert.h>
#include <glib.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

static pid_t current_pid = 0;

static void logmsg(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);

    char path[4096];
    snprintf(path, sizeof(path), "/run/user/%u/kitty-modeline.log", getuid());

    FILE *fp = fopen(path, "a");
    if (!fp) {
        fprintf(stderr, "failed to open log file: %s: %m\n", path);
        return;
    }
    vfprintf(fp, fmt, ap);
    fputc('\n', fp);
    fclose(fp);

    va_end(ap);
}

static char **decode_payload(char *encoded_payload)
{
    size_t payload_len;
    g_base64_decode_inplace(encoded_payload, &payload_len);
    encoded_payload[payload_len] = '\0';

    g_auto(GStrv) parts = g_strsplit(encoded_payload, "\n", 0);
    g_autoptr(GStrvBuilder) builder = g_strv_builder_new();
    for (char **part = parts; *part != NULL; part++) {
        if (strchr(*part, '='))
            g_strv_builder_add(builder, *part);
    }
    return g_strv_builder_end(builder);
}

static void str_remove(char *s, size_t slen, char *r, size_t rlen)
{
    char *ss = s;
    while (true) {
        ss = strstr(ss, r);
        if (!ss) {
            s[slen] = '\0';
            break;
        }
        slen -= rlen;
        memmove(ss, ss + rlen, slen);
    }
}

static void remove_bash_quote(char *s)
{
    size_t len = strlen(s);
    if (s[0] == '\'' || s[0] == '"') {
        memmove(s, s + 1, len - 1);
        --len;
        s[len] = '\0';
    }
    if (s[len - 1] == '\'' || s[len - 1] == '"')
        s[len - 1] = '\0';
}

static bool do_update(char **envp)
{
    g_autofree char *cwd = NULL;
    g_autofree char *n_columns = NULL;
    g_autofree char *cmd_status = NULL;
    g_autofree char *pipe_status = NULL;
    g_autofree char *duration = NULL;
    g_autofree char *n_jobs = NULL;

    for (char **part = envp; *part != NULL; part++) {
        char *key = *part;
        char *val = strchr(key, '=');
        assert(val != NULL);
        ++val;

        if (g_str_has_prefix(key, "PWD=")) {
            remove_bash_quote(val);
            cwd = strdup(val);
        } else if (g_str_has_prefix(key, "COLUMNS="))
            n_columns = strdup(val);
        else if (g_str_has_prefix(key, "STARSHIP_CMD_STATUS="))
            cmd_status = strdup(val);
        else if (g_str_has_prefix(key, "STARSHIP_PIPE_STATUS="))
            pipe_status = strdup(val);
        else if (g_str_has_prefix(key, "STARSHIP_DURATION="))
            duration = strdup(val);
        else if (g_str_has_prefix(key, "NUM_JOBS="))
            n_jobs = strdup(val);
    }

    if (!n_columns)
        n_columns = strdup("80");

    if (!cwd) {
        logmsg("ERROR: PWD not found in passed environment");
        return false;
    }
    if (!cmd_status) {
        logmsg("ERROR: STARSHIP_CMD_STATUS not found in passed environment");
        return false;
    }
    if (!pipe_status) {
        logmsg("ERROR: STARSHIP_PIPE_STATUS not found in passed environment");
        return false;
    }
    if (!n_jobs) {
        logmsg("ERROR: NUM_JOBS not found in passed environment");
        return false;
    }

    int pipefd[2];
    if (pipe(pipefd) < 0) {
        logmsg("ERROR: pipefd: %m");
        return false;
    }

    pid_t pid = fork();
    if (pid < 0) {
        logmsg("ERROR: fork: %m");
        close(pipefd[0]);
        close(pipefd[1]);
        return false;
    }

    if (pid == 0) {
        dup2(pipefd[1], STDOUT_FILENO);
        close(pipefd[1]);

        if (chdir(cwd) < 0)
            logmsg("ERROR: chdir: %s: %m", cwd);

        g_autoptr(GStrvBuilder) builder = g_strv_builder_new();
        g_strv_builder_add_many(builder, "starship", "prompt", "--terminal-width", n_columns, "--status", cmd_status,
                                "--pipestatus", pipe_status, "--jobs", n_jobs, NULL);
        if (duration)
            g_strv_builder_add_many(builder, "--cmd-duration", duration, NULL);
        g_auto(GStrv) argv = g_strv_builder_end(builder);

        if (execvpe("starship", argv, envp) < 0)
            logmsg("ERROR: execvpe: %m");
        exit(1);
    }

    close(pipefd[1]);

    char output[4096] = {0};

    char *x = output;
    int buflen = sizeof(output) - 1;
    while (buflen > 0) {
        int n_read = read(pipefd[0], x, buflen);
        if (n_read < 0) {
            logmsg("ERROR: read: %m");
            close(pipefd[0]);
            return false;
        }
        if (n_read == 0)
            break;
        x += n_read;
        buflen -= n_read;
    }
    assert(buflen > 0);

    close(pipefd[0]);

    int status;
    if (waitpid(pid, &status, 0) < 0) {
        logmsg("ERROR: waitpid: %m");
        return false;
    }

    size_t outlen = strlen(output);
    str_remove(output, outlen, "\\[", 2);
    str_remove(output, outlen, "\\]", 2);

    printf("\x1b[2K\r%s", output);
    fflush(stdout);

    return true;
}

static void update(char *raw_payload)
{
    if (setpgid(0, 0) < 0)
        logmsg("ERROR: setpgid: %m");
    g_auto(GStrv) envp = decode_payload(raw_payload);
    do_update(envp);
}

static void on_update_request(char *raw_payload)
{
    if (current_pid) {
        kill(-current_pid, SIGKILL);
        int status;
        waitpid(current_pid, &status, 0);
        current_pid = 0;
    }

    pid_t pid = fork();
    if (pid < 0) {
        logmsg("ERROR: fork: %m");
        return;
    }
    if (pid == 0) {
        signal(SIGCHLD, SIG_DFL);
        update(raw_payload);
        exit(0);
    }
    current_pid = pid;
}

static bool run_server(const char *ttykey)
{
    struct sockaddr_un addr;
    size_t addr_len;
    get_socket_address(&addr, &addr_len, ttykey);

    int sk = socket(AF_UNIX, SOCK_DGRAM, 0);
    if (sk < 0) {
        fprintf(stderr, "socket: %m");
        fflush(stderr);
        return false;
    }
    if (bind(sk, &addr, addr_len) < 0) {
        fprintf(stderr, "bind: %m");
        fflush(stderr);
        return false;
    }

    while (true) {
        char raw_payload[65536];
        if (recv(sk, raw_payload, sizeof(raw_payload), 0) < 0) {
            logmsg("ERROR: recv: %m");
            continue;
        }
        on_update_request(raw_payload);
    }

    return false;
}

static void sigchld_handler(int signum)
{
    if (current_pid) {
        int status;
        waitpid(current_pid, &status, WNOHANG);
    }
}

static char **read_dump(const char *ttykey)
{
    char path[4096];
    snprintf(path, sizeof(path), "/run/user/%u/kitty-modeline/shell-%s", getuid(), ttykey);

    FILE *fp = fopen(path, "r");
    if (!fp)
        return NULL;

    char raw_payload[65536];
    if (fread(raw_payload, sizeof(raw_payload), 1, fp) && ferror(fp)) {
        logmsg("ERROR: fread: %m");
        return NULL;
    }

    fclose(fp);
    unlink(path);

    return decode_payload(raw_payload);
}

int main(int argc, char *argv[])
{
    if (argc != 2) {
        fprintf(stderr, "usage: kitty-modeline <TTY>\n");
        return 1;
    }
    g_autofree char *ttykey = strdup(argv[1]);
    assert(ttykey != NULL);
    escape_tty_key_inplace(ttykey);

    char **dump = read_dump(ttykey);
    if (dump) {
        do_update(dump);
        g_strfreev(dump);
    }

    signal(SIGCHLD, sigchld_handler);

    run_server(ttykey);

    return 0;
}
