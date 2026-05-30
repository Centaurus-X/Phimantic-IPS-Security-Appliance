#include "phimantec/phim_config.h"
#include "phimantec/phim_log.h"
#include "phimantec/phim_proxy_worker.h"

#include <stdio.h>
#include <string.h>

static void usage(const char *argv0)
{
    (void)fprintf(stderr, "Usage: %s [-c config/phimantec-proxyd.conf] [-v]\n", argv0);
}

int main(int argc, char **argv)
{
    const char *config_path = "config/phimantec-proxyd.conf";
    phim_config_t cfg;
    int i;
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-c") == 0 && i + 1 < argc) {
            config_path = argv[++i];
        } else if (strcmp(argv[i], "-v") == 0) {
            phim_log_set_level(PHIM_LOG_DEBUG);
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            usage(argv[0]);
            return 0;
        } else {
            usage(argv[0]);
            return 2;
        }
    }
    if (phim_config_load(&cfg, config_path) != PHIM_OK) {
        return 1;
    }
    return phim_proxy_run(&cfg) == PHIM_OK ? 0 : 1;
}
