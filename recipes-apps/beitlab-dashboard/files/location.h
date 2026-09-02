#ifndef BEITLAB_LOCATION_H
#define BEITLAB_LOCATION_H

#include <stdbool.h>

#define LOCATION_TEXT_LEN 96

struct location_info {
    bool valid;
    bool timezone_applied;
    char city[LOCATION_TEXT_LEN];
    char region[LOCATION_TEXT_LEN];
    char country[LOCATION_TEXT_LEN];
    char timezone[LOCATION_TEXT_LEN];
    char summary[LOCATION_TEXT_LEN];
};

void location_init(void);
void location_shutdown(void);
void location_copy(struct location_info *info);

#endif
