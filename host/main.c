#include "instance.h"
#include "mime_cli.h"
#include "ui/app_identity.h"

#include <stdio.h>
#include <string.h>

static void usage(void)
{
    printf("mime %s, windows compatibility machine\n\n", MIME_VERSION_STRING);
    printf("usage:\n");
    printf("  mime up                                start the machine\n");
    printf("  mime run [--wait] [--admin] <target> [args]\n");
    printf("                                         open a program, host or C:\\ paths, an app by its id or name, or\n");
    printf("                                         anything the windows run box opens: a name like notepad, a\n");
    printf("                                         document, a shortcut, a url; --wait waits for a gui program\n");
    printf("                                         instead of returning like cmd.exe; --admin runs it as\n");
    printf("                                         administrator\n");
    printf("  mime apps                              list the apps mime run opens by id, the start menu's entries\n");
    printf("  mime apps --installed                  list the installed programs\n");
    printf("  mime uninstall <id>                    run the uninstaller of an installed program, or of an app\n");
    printf("  mime features                          list the features mime fetches from Microsoft when a program\n");
    printf("                                         needs them: the .NET Framework, WebView2 and the fonts\n");
    printf("  mime features enable <name> [--source file]\n");
    printf("                                         get a feature now; with --source the file is taken from the\n");
    printf("                                         disk and checked the same way\n");
    printf("  mime features disable <name>           remove the files of a feature, a program that needs it\n");
    printf("                                         asks again\n");
    printf("  mime down [--force] [--timeout secs]   shut the machine down, programs get to close first; --force\n");
    printf("                                         ends them all at once\n");
    printf("  mime status                            show machine state\n");
    printf("  mime locale [name]                     show the user's locale, or set it, for example fr-FR; it\n");
    printf("                                         follows the host's until set\n");
    printf("  mime config                            show the settings of the running machine, they last until\n");
    printf("                                         it goes down\n");
    printf("  mime config get <name>                 show one setting\n");
    printf("  mime config set <name> <value>         change one setting; internet on or off puts the machine\n");
    printf("                                         online or offline\n");
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
    if (strcmp(cmd, "down") == 0)
        return mime_cmd_down(&paths, argc - 2, argv + 2);
    if (strcmp(cmd, "status") == 0)
        return mime_cmd_status(&paths, argc - 2, argv + 2);
    if (strcmp(cmd, "locale") == 0)
        return mime_cmd_locale(&paths, argc - 2, argv + 2);
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
