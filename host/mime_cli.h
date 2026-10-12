#ifndef MIME_CLI_H
#define MIME_CLI_H

#include "mime_host.h"

int mime_cmd_setup(const MimePaths *paths, int argc, char **argv);
int mime_cmd_up(const MimePaths *paths, int argc, char **argv);
int mime_cmd_run(const MimePaths *paths, int argc, char **argv);
int mime_cmd_down(const MimePaths *paths, int argc, char **argv);
int mime_cmd_status(const MimePaths *paths, int argc, char **argv);

/* entry point of the background machine process, see instance.h. */
int mime_vmm_main(const MimePaths *paths, int lock_fd);

bool mime_is_setup(const MimePaths *paths);

/* copies the mime system binaries into the C: drive. */
bool mime_prepare_drive(const MimePaths *paths);

#endif
