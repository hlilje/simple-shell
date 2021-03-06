#include "shell.h"

#if (SIGDET == 1)
static const int SIGNAL_DETECTION = 1;
#else
static const int SIGNAL_DETECTION = 0;
#endif

void sig_bg_handler(int sig)
{
    int status;
    waitpid(-1, &status, WUNTRACED);
}

int print_prompt()
{
    char wd[PATH_MAX];

    if (!getcwd(wd, PATH_MAX))
    {
        perror("Failed to get current working directory");
        return 0;
    }
    printf("%s", wd);
    printf(" > ");
    return 1;
}

int read_cmd(char* cmd, const char* input, int i)
{
    int j;
    /* Discard spaces */
    while (input[i] == ' ')
        i++;

    /* Read one command */
    for (j = 0; ; ++i, ++j)
    {
        cmd[j] = input[i];
        if (input[i] == ' ' || input[i] == '\0')
        {
            /* Let i be the next non-space character */
            while (input[i] == ' ') ++i;
            cmd[j] = '\0';
            break;
        }
    }

    return i;
}

void exit_shell()
{
    /* Kill all processes of the same group */
    if (kill(0, SIGKILL))
    {
        perror("Failed to kill all processes of the same group");
        exit(1);
    }
    exit(0);
}

int cd(const char* input, char* cmd, int i)
{
    /* Change to home directory */
    if (input[i] == '\0' || input[i] == '~')
    {
        char* home = getenv("HOME");
        if (!home)
        {
            perror("Failed to get home directory");
            return 0;
        }
        else if (chdir(home))
        {
            perror("Failed to change directory to HOME");
            return 0;
        }
    }
    /* Change to given directory */
    else
    {
        i = read_cmd(cmd, input, i);
        if (chdir(cmd))
        {
            perror("Failed to change directory");
            return 0;
        }
    }

    return 1;
}

int create_pipes(int* pipes, const int num_pipes)
{
    int i, j;
    /* Pipe and get file descriptors */
    /* 1st ix = read, 2nd ix = write */
    for (i = 0, j = 0; i < num_pipes * 2; i += 2, ++j)
    {
        if (pipe(pipes + i))
        {
            perror("Failed to create pipe");
            return 0;
        }
    }

    return 1;
}

int close_pipes(int* pipes, const int num_pipes)
{
    int i;
    for (i = 0; i < num_pipes * 2; ++i)
    {
        if (close(pipes[i]))
        {
            perror("Failed to delete file descriptor");
            return 0;
        }
    }

    return 1;
}

int fork_exec_cmd(const char* cmd, int* pipes, const int* fds, char** args,
        const int num_pipes, const int try_less_more)
{
    pid_t pid;
    int i;

    /* Fork to create new process */
    pid = fork();

    if (pid < 0)
    {
        perror("Failed to fork");
        return 0;
    }
    /* Child process goes here, parent just returns */
    else if (pid == 0)
    {
        /* Copy and overwrite file descriptor if set to do so */
        if (fds[0] != -1 && fds[1] != -1)
        {
            if (dup2(pipes[fds[0]], fds[1]) < 0)
            {
                perror("Failed to duplicate file descriptor for writing");
                _exit(1);
            }
        }
        if (fds[2] != -1 && fds[3] != -1)
        {
            if (dup2(pipes[fds[2]], fds[3]) < 0)
            {
                perror("Failed to duplicate file descriptor for writing");
                _exit(1);
            }
        }

        /* Delete all file descriptors for the child process */
        for (i = 0; i < num_pipes * 2; ++i)
        {
            if (close(pipes[i]))
            {
                perror("Failed to delete file descriptor");
                _exit(1);
            }
        }

        /* Execute command with arguments via path */
        if (args != NULL)
        {
            if (execvp(cmd, args))
            {
                perror(cmd);
                _exit(1);
            }
        }
        /* Execute command without arguments via path */
        else
        {
            /* Special case to try more if less fails */
            if (try_less_more)
            {
                if (execlp("less", cmd, NULL))
                {
                    if (execlp("more", cmd, NULL))
                    {
                        perror(cmd);
                        _exit(1);
                    }
                }
            }
            else
            {
                if (execlp(cmd, cmd, NULL))
                {
                    perror(cmd);
                    _exit(1);
                }
            }
        }
    }

    return 1;
}

int check_env(const char* input, int i)
{
    int pipes[6];                   /* File descriptors from piping */
    int fds[4];                     /* File descriptors to dupe */
    int status;                     /* Wait status */
    int j = 1;                      /* Loop index */
    int num_pipes = 2;              /* Number of pipes to create */
    char* args[80];                 /* All arguments to grep */
    char* pager = getenv("PAGER");  /* PAGER enviroment variable */
    char cmd[80];                   /* One grep parameter */

    /* Read arguments to grep */
    while (input[i] != '\0')
    {
        i = read_cmd(cmd, input, i);
        args[j] = cmd;
        ++j;
    }

    /* If arguments were given, one pipe is needed for grep */
    if (j > 1) num_pipes = 3;

    /* Create all pipes beforehand */
    create_pipes(pipes, num_pipes);

    /* Argument list to execvp is NULL terminated */
    args[j] = (char*) NULL;

    /* First argument in list must be file name */
    args[0] = cmd;

    /* pipe fds: (0, 1) [2, 3, 4, 5, 6, 7] */

    /* PIPE READ WRITE */
    /* 1    2    3     */
    /* 2    4    5     */
    /* 3    6    7     */

    /* PROC READ WRITE */
    /* 1    0    3     */
    /* 2    2    5     */
    /* 3    4    7     */
    /* 4    6    1     */

    /* Pipe and execute printenv */
    fds[0] = -1;
    fds[1] = -1;
    fds[2] = 1;
    fds[3] = WRITE;
    if (!fork_exec_cmd("printenv", pipes, fds, NULL, num_pipes, 0))
    {
        perror("Failed to execute printenv");
        return 0;
    }

    /* Only pipe and excute grep if arguments were given */
    fds[0] = 0;
    fds[1] = READ;
    fds[2] = 3;
    fds[3] = WRITE;
    if (num_pipes == 3)
    {
        if (!fork_exec_cmd("grep", pipes, fds, args, num_pipes, 0))
        {
            perror("Failed to to execute grep");
            return 0;
        }
    }

    /* Pipe and execute sort */
    if (num_pipes == 3)
    {
        fds[0] = 2;
        fds[1] = READ;
        fds[2] = 5;
        fds[3] = WRITE;
    }
    if (!fork_exec_cmd("sort", pipes, fds, NULL, num_pipes, 0))
    {
        perror("Failed to to execute sort");
        return 0;
    }

    /* Try to pipe and execute with PAGER environment variable */
    fds[0] = (num_pipes == 3) ? 4 : 2;
    fds[1] = READ;
    fds[2] = -1;
    fds[3] = -1;
    if (pager)
    {
        if (!fork_exec_cmd(pager, pipes, fds, NULL, num_pipes, 0))
        {
            perror("Failed to to execute checkEnv with environment pager");
            return 0;
        }
    }
    /* Try to pipe and execute with pager `less`, then `more` */
    else
    {
        if (!fork_exec_cmd("more", pipes, fds, NULL, num_pipes, 1))
        {
            perror("Failed to to execute checkEnv with default pagers");
            return 0;
        }
    }

    /* Let the parent processes close all pipes */
    close_pipes(pipes, num_pipes);

    /* Let the parent processes wait for all children */
    for (j = 0; j < num_pipes + 1; ++j)
    {
        /* Wait for the processes to finish */
        if (wait(&status) < 0)
        {
            perror("Failed to wait for process");
            return 0;
        }
    }

    return 1;
}

int general_cmd(char* input, const struct sigaction* act_int_old,
        const int* bg_pipes)
{
    int pipes[2];                 /* File descriptors from piping */
    int fds[4];                   /* File descriptors to dupe */
    int background_process;       /* Whether to run in the background */
    int status;                   /* Wait status */
    int i;                        /* Command index */
    int j = 1;                    /* Loop index */
    int num_pipes = 1;            /* Number of pipes to create */
    char* args[80];               /* All arguments to the command */
    char arg[80];                 /* One argument to command */
    char cmd[80];                 /* The command to be executed */
    unsigned long exec_time;      /* Execution time */
    pid_t pid;                    /* PID of child */
    struct timeval time_before;   /* Time before execution of command */
    struct timeval time_after;    /* Time after execution of command */

    create_pipes(pipes, num_pipes);
    fds[0] = -1;
    fds[1] = -1;
    fds[2] = -1;
    fds[3] = -1;

    /* Read the entire command string */
    background_process = 0;
    for (i = 0; ; ++i)
    {
        /* Check if the process should run in the background */
        if (input[i] == '&')
        {
            background_process = 1;
            input[i] = '\0';
            break;
        }
        else if (input[i] == '\0')
        {
            break;
        }
    }

    /* Read the command */
    i = read_cmd(cmd, input, 0);
    /* First argument in list must be file name */
    args[0] = cmd;

    /* Read arguments to the command */
    while (input[i] != '\0')
    {
        i = read_cmd(arg, input, i);
        args[j] = arg;
        ++j;
    }
    /* Argument list to execvp is NULL terminated */
    args[j] = (char*) NULL;

    pid = fork(); /* Create new child process that handles execution */

    /* Child process */
    if (pid == 0)
    {
        /* Save current stdout file descriptor */
        if (background_process)
        {
            /* Write termination info to process info pipe */
            if (dup2(bg_pipes[1], WRITE) < 0)
            {
                perror("Failed to duplicate file descriptor for writing");
                return 0;
            }

            /* Write err info to process info process pipe */
            if (dup2(bg_pipes[1], ERROR) < 0)
            {
                perror("Failed to duplicate file descriptor for errors");
                return 0;
            }

            /* Close file descriptor */
            if (close(bg_pipes[1]))
            {
                perror("Failed to delete file descriptor");
                return 0;
            }
        }

        /* Restore normal interrupt behaviour */
        if (sigaction(SIGINT, act_int_old, NULL))
            perror("Failed to change handler for SIGINT in child");

        /* Measure execution time */
        gettimeofday(&time_before, NULL);

        if (!fork_exec_cmd(cmd, pipes, fds, args, num_pipes, 0))
        {
            perror(cmd);
            return 0;
        }
        if ((pid = wait(&status)) < 0)
        {
            perror("Failed to wait for executing process");
            return 0;
        }

        /* Calculate execution time */
        gettimeofday(&time_after, NULL);
        exec_time = 1000 * (time_after.tv_sec - time_before.tv_sec) +
        (time_after.tv_usec - time_before.tv_usec) / 1000;
        printf("(pid: %d) %s finished executing in %lu ms.\n", pid, cmd, exec_time);

        /* Notify parent of termination */
        if (SIGNAL_DETECTION == 1 && background_process)
        {
            if (kill(getppid(), SIGUSR1))
            {
                perror("Failed to notify parent of termination");
                return 0;
            }
        }

        _exit(0); /* exit() unreliable */
    }
    /* Error */
    else if (pid < 0)
    {
        perror("Failed to fork child process");
        exit(1);
    }
    /* Parent process */
    else
    {
        /* The parent process comes here after forking */
        if (!background_process)
        {
            if (waitpid(pid, &status, 0) < 0)
            {
                perror("Failed to wait for process");
                return 0;
            }
        }
    }

    /* Let the parent processes close all pipes */
    close_pipes(pipes, num_pipes);

    return 1;
}

int print_process_info(const int* bg_pipes)
{
    char buffer[128];
    fd_set rfds;
    struct timeval tv;
    int retval, read_bytes;

    /* Check fd immediately when calling */
    FD_ZERO(&rfds);
    FD_SET(bg_pipes[0], &rfds);
    tv.tv_sec = 0;
    tv.tv_usec = 0;

    /* Loop over all text in pipe */
    read_bytes = 1;
    while (read_bytes > 0)
    {
        read_bytes = 0;
        retval = select(bg_pipes[0] + 1, &rfds, NULL, NULL, &tv);

        /* First check if file descriptor has data available, since non-blocking */
        if (retval == -1)
        {
            perror("Failed to check file descriptor");
            return 0;
        }
        else if (retval)
        {
            read_bytes = read(bg_pipes[0], buffer, 127);
            /* Read buffered output into buffer */
            if (read_bytes == -1)
            {
                perror("Failed to read file descriptor");
                return 0;
            }

            /* read does not null-terminate */
            buffer[read_bytes] = '\0';

            printf("%s", buffer);
        }
    }

    return 1;
}

int main(int argc, const char* argv[])
{
    int status, bg_pipes[2], num_pipes, flags;
    struct sigaction act_bg_term, act_int_old, act_int_new;
    /* Define handler for SIGINT (ignore) */
    act_int_new.sa_handler = SIG_IGN;
    act_int_new.sa_flags = 0;
    if (sigaction(SIGINT, &act_int_new, &act_int_old))
        perror("Failed to change handler for SIGINT");

    /* Define handler for detecting background process termination */
    if (SIGNAL_DETECTION == 1)
    {
        if (sigaction(SIGUSR1, NULL, &act_bg_term))
            perror("Failed to get handler for SIGUSR1");
        act_bg_term.sa_handler = sig_bg_handler;
        act_bg_term.sa_flags = SA_RESTART;
        if (sigaction(SIGUSR1, &act_bg_term, NULL))
            perror("Failed to set handler for SIGUSR1");
    }

    /* Create pipe for printing background process info */
    num_pipes = 1;
    create_pipes(bg_pipes, num_pipes);

    /* Configure pipe to be non-blocking on read end */
    flags = fcntl(bg_pipes[0], F_GETFL, 0);
    fcntl(bg_pipes[0], F_SETFL, flags | O_NONBLOCK);

    while (1)
    {
        char input[80], cmd[80];
        int i;

        /* Wait for all defunct children */
        /* Continue even if no child has exited */
        if (!(SIGNAL_DETECTION == 1))
            while (waitpid(-1, &status, WNOHANG | WUNTRACED) > 0);

        /* Print prompt */
        if (!print_prompt()) continue;

        /* Exit if error occurs */
        if (!fgets(input, 80, stdin))
        {
            perror("Failed to get input");
            continue;
        }

        /* Remove newline, if present */
        i = strlen(input) - 1;
        if (input[i] == '\n') input[i] = '\0';

        /* Read given commands */
        i = 0; /* Input index */
        i = read_cmd(cmd, input, i);

        if (strcmp(cmd, "exit") == 0)
            break;
        else if (strcmp(cmd, "cd") == 0)
            cd(input, cmd, i);
        else if (strcmp(cmd, "checkEnv") == 0)
            check_env(input, i);
        else if (cmd[0] == '\0') {} /* Just print process info */
        else
            general_cmd(input, &act_int_old, bg_pipes);

        /* Print accumulated process information */
        print_process_info(bg_pipes);
    }

    /* Close pipe for printing background process info */
    close_pipes(bg_pipes, num_pipes);

    exit_shell();

    return 0;
}
