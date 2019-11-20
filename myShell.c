#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <fcntl.h>

#define MAX_CMD_ARG 10
#define FOREGROUND 1
#define BACKGROUND 0
#define STDPERM 0644

const char *prompt = "myshell> ";
char *cmdvector[MAX_CMD_ARG];
char *cmdgrp[MAX_CMD_ARG];
char cmdline[BUFSIZ];
int proc_type;

/* ----------------------------------- Error-Handling ----------------------------------- */
void fatal(char *str) {
    perror(str);
    exit(1);
}
/* -------------------------------------------------------------------------------------- */


/* ------------------------------ Zombie Process-Handler -------------------------------- */
void zombie_handling(int sig) {
    int status;
    pid_t pid;
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0);
}
/* -------------------------------------------------------------------------------------- */


/* --------------------------------- Built-in Function ---------------------------------- */
int myShell_cd(char **args);

int myShell_exit();

char *builtin_cmd[] = {"cd", "exit"};

int (*builtin_func[])(char **) = {&myShell_cd, &myShell_exit};

int numOfBuiltin() { return sizeof(builtin_cmd) / sizeof(char *); }

int myShell_cd(char **args) {
    if (args[1] == NULL)
        fputs("Usage cd error\n", stdout);
    else {
        if (chdir(args[1]) != 0)
            fatal("myshell: ");
    }
    return 1;
}

int myShell_exit() {
    exit(0);
}
/* -------------------------------------------------------------------------------------- */


/* -------------------------------- Execution Of Command -------------------------------- */
/*
 * Makes command vector from command line
 * Caution : parameter 's' changes after makelist()
 * */
int makelist(char *s, const char *delimiters, char **list, int MAX_LIST) {
    int i = 0;
    int numtokens = 0;
    char *snew = NULL;

    if ((s == NULL) || (delimiters == NULL)) return -1;

    snew = s + strspn(s, delimiters);
    if ((list[numtokens] = strtok(snew, delimiters)) == NULL)
        return numtokens;

    numtokens = 1;

    while (1) {
        if ((list[numtokens] = strtok(NULL, delimiters)) == NULL)
            break;
        if (numtokens == (MAX_LIST - 1)) return -1;
        numtokens++;
    }
    return numtokens;
}

/*
 * Check type of process (FOREGROUND or BACKGROUND)
 * */
void check_type() {
    int i = 0;
    proc_type = FOREGROUND; // default -> Foreground process
    for (i = 0; i < strlen(cmdline); ++i) {
        // 1. Check type, and store at proc_type
        if (cmdline[i] == '&') {
            proc_type = BACKGROUND;
            // 2. Deletes '&' to make proper command
            cmdline[i] = ' ';
            return;
        }
    }
}

/*
 * Check if it needs redirection
 * If redirection exists, change STDIN or STDOUT
 * Also, change 'cur_cmd' to be exact command by ignore redirection arguments
 * */
void check_redirect(char *cur_cmd) {
    int i, fd;
    int cmd_len = strlen(cur_cmd);
    char *tmp_arg;

    for (i = cmd_len - 1; i >= 0; i--) {

        switch (cur_cmd[i]) {
            case '<':
                tmp_arg = strtok(&cur_cmd[i + 1], " \t");
                if ((fd = open(tmp_arg, O_RDONLY | O_CREAT, STDPERM)) < 0)
                    fatal("file open error");
                dup2(fd, STDIN_FILENO);
                close(fd);
                cur_cmd[i] = '\0';
                break;
            case '>':
                tmp_arg = strtok(&cur_cmd[i + 1], " \t");
                if ((fd = open(tmp_arg, O_WRONLY | O_CREAT | O_TRUNC, STDPERM)) < 0)
                    fatal("file open error");
                dup2(fd, STDOUT_FILENO);
                close(fd);
                cur_cmd[i] = '\0';
                break;
            default:
                break;
        }
    }
}

/*
 * Run cmd using cmdvector
 * */
void exec_cmd(char *cur_cmd) {
    // 1. Check if there exists redirection argument and change command properly
    check_redirect(cur_cmd);

    // 2. Make cmdvector from proper cur_cmd
    makelist(cur_cmd, " \t", cmdvector, MAX_CMD_ARG);
    execvp(cmdvector[0], cmdvector);
    fatal("exec_cmd() error");
}

/*
 * Run process in separated group, handling pipe
 * */
void exec_cmd_grp(char *cur_cmd) {
    int num_pipe, i;
    int pfd[2];

    pid_t pid = getpid();

    // 1. Set signal option to default
    signal(SIGQUIT, SIG_DFL);
    signal(SIGINT, SIG_DFL);

    // 2. Set current process as group leader
    setpgid(pid, pid);

    // 3. Get terminal control to current group if its foreground process
    if (proc_type == FOREGROUND) {
        if (tcsetpgrp(STDIN_FILENO, getpgid(pid)) == -1)
            fatal("tcsetpgrp()");
    }

    // 4. Check if there exists pipe, than fork separately
    if ((num_pipe = makelist(cur_cmd, "|", cmdgrp, MAX_CMD_ARG)) <= 0)
        fatal("makelist error at exec_cmd_grp()");

    for (i = 0; i < num_pipe - 1; ++i) {
        pipe(pfd);
        switch (fork()) {
            case -1:
                fatal("fork error at at exec_cmd_grp()");
            case 0:
                close(pfd[0]);
                dup2(pfd[1], STDOUT_FILENO);
                exec_cmd(cmdgrp[i]);
            default:
                close(pfd[1]);
                dup2(pfd[0], STDIN_FILENO);
        }
    }
    exec_cmd(cmdgrp[i]);
}

/*
 * Prepare for running
 * Separate built-in process
 * */
int exec_cmd_line(char *cur_cmd) {
    int status;
    int i;
    pid_t pid;
    char tmp_cmd[BUFSIZ];

    // 1. Check if background process
    check_type();

    // 2. Make exact argv list
    memcpy(tmp_cmd, cur_cmd, strlen(cur_cmd) + 1);
    makelist(tmp_cmd, " \t", cmdvector, MAX_CMD_ARG);

    // 3. Check if its valid
    if (cmdvector[0] == NULL) {
        return -1;
    }

    // 4. Check if built-in command
    for (i = 0; i < numOfBuiltin(); ++i) {
        if (strcmp(cmdvector[0], builtin_cmd[i]) == 0)
            return (*builtin_func[i])(cmdvector);
    }

    // 5. Fork and exec process (Treat differently on Background or Foreground)
    switch (pid = fork()) {
        case 0:
            exec_cmd_grp(cur_cmd);
            break;
        case -1:
            fatal("execProc case -1: ");
            break;
        default:
            if (proc_type == BACKGROUND) {
                break;
            }
            waitpid(pid, &status, 0);
            // get terminal control from child process
            tcsetpgrp(STDIN_FILENO, getpgid(0));
            fflush(stdout);
    }
    return status;
}
/* -------------------------------------------------------------------------------------- */

/* ---------------------------------------- Main ---------------------------------------- */
int main(int argc, char **argv) {
    struct sigaction act;
    sigemptyset(&act.sa_mask);
    act.sa_handler = zombie_handling;
    act.sa_flags = SA_RESTART;
    sigaction(SIGCHLD, &act, NULL);

    signal(SIGINT, SIG_IGN);
    signal(SIGQUIT, SIG_IGN);
    signal(SIGTTOU, SIG_IGN);
    signal(SIGTTIN, SIG_IGN);

    while (1) {
        fputs(prompt, stdout);
        fgets(cmdline, BUFSIZ, stdin);
        cmdline[strlen(cmdline) - 1] = '\0';

        exec_cmd_line(cmdline);
    }
}
/* -------------------------------------------------------------------------------------- */
