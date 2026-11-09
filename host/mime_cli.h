#ifndef MIME_CLI_H
#define MIME_CLI_H

#include "mime_host.h"

int mime_cmd_up(const MimePaths *paths, int argc, char **argv);
int mime_cmd_run(const MimePaths *paths, int argc, char **argv);
int mime_cmd_down(const MimePaths *paths, int argc, char **argv);
int mime_cmd_status(const MimePaths *paths, int argc, char **argv);
int mime_cmd_config(const MimePaths *paths, int argc, char **argv);
int mime_cmd_apps(const MimePaths *paths, int argc, char **argv);
int mime_cmd_uninstall(const MimePaths *paths, int argc, char **argv);
int mime_cmd_features(const MimePaths *paths, int argc, char **argv);
int mime_cmd_setup(const MimePaths *paths, int argc, char **argv);

/* makes sure the host holds every Windows system file of the manifest, downloading the missing ones. 0 when it does. */
int mime_genuine_ensure(const MimePaths *paths);
/* how many of them the host holds, for mime status. */
void mime_genuine_print_status(const MimePaths *paths);

/* entry point of the background machine process, see instance.h. */
int mime_vmm_main(const MimePaths *paths, int lock_fd);


/* copies the mime system binaries into the C: drive. */
bool mime_prepare_drive(const MimePaths *paths);

#endif
