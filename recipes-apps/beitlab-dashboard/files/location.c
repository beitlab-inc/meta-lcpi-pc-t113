#define _GNU_SOURCE

#include "location.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define ZONEINFO_PREFIX "/usr/share/zoneinfo/"
#define LOCATION_URL \
    "http://ip-api.com/json/?fields=status,message,country,regionName,city,timezone"

static pthread_mutex_t location_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_t location_thread;
static volatile int location_running;
static int location_thread_started;
static struct location_info current_location;

static int json_string(const char *json, const char *key, char *out, size_t out_size)
{
    char pattern[80];
    const char *cursor;
    size_t index = 0;

    snprintf(pattern, sizeof(pattern), "\"%s\":\"", key);
    cursor = strstr(json, pattern);
    if (!cursor)
        return -1;

    cursor += strlen(pattern);
    while (*cursor && *cursor != '"' && index + 1 < out_size) {
        if (*cursor == '\\' && cursor[1])
            cursor++;
        out[index++] = *cursor++;
    }
    out[index] = '\0';
    return index ? 0 : -1;
}

static void build_summary(struct location_info *info)
{
    if (info->city[0] && info->region[0] &&
        strcmp(info->city, info->region) != 0)
        snprintf(info->summary, sizeof(info->summary), "%s, %s, %s",
                 info->city, info->region, info->country);
    else if (info->city[0] && info->country[0])
        snprintf(info->summary, sizeof(info->summary), "%s, %s",
                 info->city, info->country);
    else if (info->country[0])
        snprintf(info->summary, sizeof(info->summary), "%s", info->country);
    else if (info->timezone[0])
        snprintf(info->summary, sizeof(info->summary), "%s", info->timezone);
    else
        snprintf(info->summary, sizeof(info->summary), "Location unknown");
}

static int apply_timezone(const char *timezone)
{
    char zone_path[256];
    FILE *timezone_file;

    if (!timezone || !timezone[0] || strchr(timezone, '.'))
        return -1;

    snprintf(zone_path, sizeof(zone_path), ZONEINFO_PREFIX "%s", timezone);
    if (access(zone_path, R_OK) != 0)
        return -1;

    unlink("/etc/localtime");
    if (symlink(zone_path, "/etc/localtime") != 0)
        return -1;

    timezone_file = fopen("/etc/timezone", "w");
    if (timezone_file) {
        fprintf(timezone_file, "%s\n", timezone);
        fclose(timezone_file);
    }

    setenv("TZ", timezone, 1);
    tzset();
    return 0;
}

static int fetch_location(struct location_info *info)
{
    FILE *pipe;
    char body[1024];
    size_t length;
    char status[32] = {0};

    memset(info, 0, sizeof(*info));
    pipe = popen("curl -sS --max-time 8 '" LOCATION_URL "' 2>/dev/null", "r");
    if (!pipe)
        return -1;

    length = fread(body, 1, sizeof(body) - 1, pipe);
    body[length] = '\0';
    if (pclose(pipe) != 0 || !length)
        return -1;

    if (json_string(body, "status", status, sizeof(status)) != 0 ||
        strcmp(status, "success") != 0)
        return -1;

    json_string(body, "city", info->city, sizeof(info->city));
    json_string(body, "regionName", info->region, sizeof(info->region));
    json_string(body, "country", info->country, sizeof(info->country));
    json_string(body, "timezone", info->timezone, sizeof(info->timezone));
    if (!info->timezone[0])
        return -1;

    info->timezone_applied = apply_timezone(info->timezone) == 0;
    build_summary(info);
    info->valid = true;
    return 0;
}

static void *location_worker(void *argument)
{
    unsigned int failures = 0;

    (void)argument;
    sleep(2);
    while (location_running) {
        struct location_info fetched;

        if (fetch_location(&fetched) == 0) {
            pthread_mutex_lock(&location_lock);
            current_location = fetched;
            pthread_mutex_unlock(&location_lock);
            failures = 0;
            for (unsigned int wait = 0; location_running && wait < 1800; ++wait)
                sleep(1);
        } else {
            if (failures < 6)
                failures++;
            for (unsigned int wait = 0;
                 location_running && wait < (30U * failures); ++wait)
                sleep(1);
        }
    }
    return NULL;
}

void location_init(void)
{
    pthread_mutex_lock(&location_lock);
    memset(&current_location, 0, sizeof(current_location));
    snprintf(current_location.summary, sizeof(current_location.summary),
             "Locating...");
    pthread_mutex_unlock(&location_lock);

    location_running = 1;
    if (pthread_create(&location_thread, NULL, location_worker, NULL) == 0)
        location_thread_started = 1;
}

void location_shutdown(void)
{
    location_running = 0;
    if (location_thread_started) {
        pthread_join(location_thread, NULL);
        location_thread_started = 0;
    }
}

void location_copy(struct location_info *info)
{
    if (!info)
        return;
    pthread_mutex_lock(&location_lock);
    *info = current_location;
    pthread_mutex_unlock(&location_lock);
}
