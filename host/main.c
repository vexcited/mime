#include "instance.h"
#include "mime_cli.h"
#include "ui/app_identity.h"

#include <stdio.h>
#include <string.h>

static void usage(void)
{
    printf("mime %s, windows compatibility machine\n\n", MIME_VERSION_STRING);
    printf("usage:\n");
    printf("  mime setup [windows.iso] [--image N] [--locale NAME]   import assets from a windows iso\n");
    printf("  mime up                                start the machine\n");
    printf("  mime run [--wait] <program.exe> [args] run a program, host or C:\\ paths; --wait waits for\n");
    printf("                                         a gui program instead of returning like cmd.exe\n");
    printf("  mime down [--force] [--timeout secs]   shut the machine down, programs get to close first; --force\n");
    printf("                                         ends them all at once\n");
    printf("  mime status                            show machine state\n");
}

int main(int argc, char **argv)
{
    int proxy_result = mime_app_proxy_main(argc, argv);
    if (proxy_result >= 0)
        return proxy_result;
    int lock_fd;
    if (mime_instance_accept_launch(&lock_fd)) {
        MimePaths paths;
        if (!mime_paths_init(&paths))
            return 1;
        return mime_vmm_main(&paths, lock_fd);
    }
    if (argc < 2) {
        usage();
        return 2;
    }
    MimePaths paths;
    if (!mime_paths_init(&paths))
        return 1;

    const char *cmd = argv[1];
    if (strcmp(cmd, "setup") == 0)
        return mime_cmd_setup(&paths, argc - 2, argv + 2);
    if (strcmp(cmd, "up") == 0)
        return mime_cmd_up(&paths, argc - 2, argv + 2);
    if (strcmp(cmd, "run") == 0)
        return mime_cmd_run(&paths, argc - 2, argv + 2);
    if (strcmp(cmd, "down") == 0)
        return mime_cmd_down(&paths, argc - 2, argv + 2);
    if (strcmp(cmd, "status") == 0)
        return mime_cmd_status(&paths, argc - 2, argv + 2);
    if (strcmp(cmd, "version") == 0 || strcmp(cmd, "--version") == 0) {
        printf("mime %s\n", MIME_VERSION_STRING);
        return 0;
    }
    if (strcmp(cmd, "help") == 0 || strcmp(cmd, "--help") == 0 || strcmp(cmd, "-h") == 0) {
        usage();
        return 0;
    }
    fprintf(stderr, "mime: unknown command '%s'\n\n", cmd);
    usage();
    return 2;
}
