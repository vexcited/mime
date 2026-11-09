#include "instance.h"
#include "mime_cli.h"
#include "ui/app_identity.h"
#include "ui/ui.h"

#include <stdio.h>
#include <string.h>

static void usage(void)
{
    printf("mime %s, runs Windows programs\n\n", MIME_VERSION_STRING);
    printf("usage: mime <command> [arguments]\n\n");
    printf("  setup                        prepare the drive and download the Windows system files programs need,\n");
    printf("                               which up and run also do the first time\n");
    printf("  up                           start the machine\n");
    printf("  down [--force]               shut the machine down, letting programs close first unless --force\n");
    printf("  status                       show the machine's state\n");
    printf("  run [--admin] <target> [args]\n");
    printf("                               open a program, document, shortcut or url, or an app by its id or name,\n");
    printf("                               as the Windows Run box does; a console program runs in the terminal\n");
    printf("  apps [--installed]           list the apps run opens by name, or the installed programs\n");
    printf("  uninstall <id>               run the uninstaller of an installed program or app\n");
    printf("  features                     list the features fetched from Microsoft when a program needs them\n");
    printf("  features enable <name> [--source <file>]\n");
    printf("                               get a feature now, from a file instead of the network with --source\n");
    printf("  features disable <name>      remove a feature's files\n");
    printf("  config                       list the settings\n");
    printf("  config get <name>            show a setting\n");
    printf("  config set <name> <value>    change a setting\n\n");
    printf("settings:\n");
    printf("  locale                       the user's locale, such as fr-FR, taken when the machine starts;\n");
    printf("                               it follows the host's until set\n");
    printf("  internet                     on or off, until the machine goes down\n");
}

int main(int argc, char **argv)
{
    /* the machine shows a dialog through mime itself when the desktop has no helper for it. */
    int dialog_result = mime_ui_dialog_helper(argc, argv);
    if (dialog_result >= 0)
        return dialog_result;
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
    if (strcmp(cmd, "up") == 0)
        return mime_cmd_up(&paths, argc - 2, argv + 2);
    if (strcmp(cmd, "run") == 0)
        return mime_cmd_run(&paths, argc - 2, argv + 2);
    if (strcmp(cmd, "apps") == 0)
        return mime_cmd_apps(&paths, argc - 2, argv + 2);
    if (strcmp(cmd, "uninstall") == 0)
        return mime_cmd_uninstall(&paths, argc - 2, argv + 2);
    if (strcmp(cmd, "features") == 0)
        return mime_cmd_features(&paths, argc - 2, argv + 2);
    if (strcmp(cmd, "setup") == 0)
        return mime_cmd_setup(&paths, argc - 2, argv + 2);
    if (strcmp(cmd, "down") == 0)
        return mime_cmd_down(&paths, argc - 2, argv + 2);
    if (strcmp(cmd, "status") == 0)
        return mime_cmd_status(&paths, argc - 2, argv + 2);
    if (strcmp(cmd, "config") == 0)
        return mime_cmd_config(&paths, argc - 2, argv + 2);
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
