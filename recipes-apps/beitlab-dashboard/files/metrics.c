#define _GNU_SOURCE

#include "metrics.h"

#include <arpa/inet.h>
#include <dirent.h>
#include <errno.h>
#include <ifaddrs.h>
#include <inttypes.h>
#include <limits.h>
#include <net/if.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/statvfs.h>
#include <sys/sysinfo.h>
#include <sys/timex.h>
#include <sys/utsname.h>
#include <unistd.h>

static int read_u64_file(const char *path, uint64_t *value)
{
    FILE *file = fopen(path, "r");
    if (!file)
        return -1;

    unsigned long long parsed = 0;
    int ok = fscanf(file, "%llu", &parsed) == 1;
    fclose(file);
    if (!ok)
        return -1;

    *value = (uint64_t)parsed;
    return 0;
}

static void collect_cpu(struct metrics_state *state,
                        struct metrics_snapshot *snapshot)
{
    FILE *file = fopen("/proc/stat", "r");
    if (!file)
        return;

    uint64_t user = 0, nice = 0, system = 0, idle = 0, iowait = 0;
    uint64_t irq = 0, softirq = 0, steal = 0;
    int fields = fscanf(file,
                        "cpu %" SCNu64 " %" SCNu64 " %" SCNu64 " %" SCNu64
                        " %" SCNu64 " %" SCNu64 " %" SCNu64 " %" SCNu64,
                        &user, &nice, &system, &idle, &iowait, &irq, &softirq,
                        &steal);
    fclose(file);
    if (fields < 4)
        return;

    uint64_t idle_total = idle + iowait;
    uint64_t total = user + nice + system + idle + iowait + irq + softirq + steal;

    if (state->have_cpu_sample && total > state->previous_cpu_total) {
        uint64_t total_delta = total - state->previous_cpu_total;
        uint64_t idle_delta = idle_total - state->previous_cpu_idle;
        if (idle_delta > total_delta)
            idle_delta = total_delta;
        snapshot->cpu_percent =
            100.0 * (double)(total_delta - idle_delta) / (double)total_delta;
    }

    state->previous_cpu_total = total;
    state->previous_cpu_idle = idle_total;
    state->have_cpu_sample = true;
}

static void collect_memory(struct metrics_snapshot *snapshot)
{
    FILE *file = fopen("/proc/meminfo", "r");
    if (!file)
        return;

    char key[64];
    uint64_t value;
    char unit[16];
    uint64_t free_kib = 0, buffers_kib = 0, cached_kib = 0;

    while (fscanf(file, "%63s %" SCNu64 " %15s", key, &value, unit) == 3) {
        if (strcmp(key, "MemTotal:") == 0)
            snapshot->memory_total_kib = value;
        else if (strcmp(key, "MemAvailable:") == 0)
            snapshot->memory_available_kib = value;
        else if (strcmp(key, "MemFree:") == 0)
            free_kib = value;
        else if (strcmp(key, "Buffers:") == 0)
            buffers_kib = value;
        else if (strcmp(key, "Cached:") == 0)
            cached_kib = value;
    }
    fclose(file);

    if (!snapshot->memory_available_kib)
        snapshot->memory_available_kib = free_kib + buffers_kib + cached_kib;
}

static void collect_storage(struct metrics_snapshot *snapshot)
{
    struct statvfs info;
    if (statvfs("/", &info) != 0)
        return;

    snapshot->root_total_bytes = (uint64_t)info.f_blocks * info.f_frsize;
    snapshot->root_available_bytes = (uint64_t)info.f_bavail * info.f_frsize;
}

static void collect_temperature(struct metrics_snapshot *snapshot)
{
    DIR *directory = opendir("/sys/class/thermal");
    if (!directory)
        return;

    struct dirent *entry;
    while ((entry = readdir(directory)) != NULL) {
        if (strncmp(entry->d_name, "thermal_zone", 12) != 0)
            continue;

        char path[PATH_MAX];
        uint64_t temperature = 0;
        snprintf(path, sizeof(path), "/sys/class/thermal/%s/temp", entry->d_name);
        if (read_u64_file(path, &temperature) == 0) {
            snapshot->temperature_millic = (long)temperature;
            break;
        }
    }
    closedir(directory);
}

static int default_route_interface(char *name, size_t name_size)
{
    FILE *file = fopen("/proc/net/route", "r");
    if (!file)
        return -1;

    char line[256];
    if (!fgets(line, sizeof(line), file)) {
        fclose(file);
        return -1;
    }

    while (fgets(line, sizeof(line), file)) {
        char iface[METRICS_IFACE_LEN];
        unsigned long destination = 1;
        unsigned long flags = 0;
        if (sscanf(line, "%31s %lx %*s %lx", iface, &destination, &flags) == 3 &&
            destination == 0 && (flags & 0x1UL) != 0) {
            snprintf(name, name_size, "%s", iface);
            fclose(file);
            return 0;
        }
    }
    fclose(file);
    return -1;
}

static int fallback_interface(char *name, size_t name_size)
{
    struct ifaddrs *addresses = NULL;
    if (getifaddrs(&addresses) != 0)
        return -1;

    int result = -1;
    for (struct ifaddrs *item = addresses; item; item = item->ifa_next) {
        if (!item->ifa_addr || item->ifa_addr->sa_family != AF_INET)
            continue;
        if (!(item->ifa_flags & IFF_UP) || (item->ifa_flags & IFF_LOOPBACK))
            continue;
        snprintf(name, name_size, "%s", item->ifa_name);
        result = 0;
        break;
    }
    freeifaddrs(addresses);
    return result;
}

static void interface_ipv4(const char *name, char *address, size_t address_size)
{
    struct ifaddrs *addresses = NULL;
    if (getifaddrs(&addresses) != 0)
        return;

    for (struct ifaddrs *item = addresses; item; item = item->ifa_next) {
        if (!item->ifa_addr || item->ifa_addr->sa_family != AF_INET)
            continue;
        if (strcmp(item->ifa_name, name) != 0)
            continue;
        struct sockaddr_in *ipv4 = (struct sockaddr_in *)item->ifa_addr;
        if (inet_ntop(AF_INET, &ipv4->sin_addr, address, address_size))
            break;
    }
    freeifaddrs(addresses);
}

static void collect_wireless_signal(struct metrics_snapshot *snapshot)
{
    FILE *file = fopen("/proc/net/wireless", "r");
    if (!file)
        return;

    char line[256];
    while (fgets(line, sizeof(line), file)) {
        char iface[METRICS_IFACE_LEN] = {0};
        double level = 0.0;
        if (sscanf(line, " %31[^:]: %*x %*lf %lf", iface, &level) == 2 &&
            strcmp(iface, snapshot->interface) == 0) {
            snapshot->has_wireless_signal = true;
            snapshot->wireless_dbm = (int)level;
            break;
        }
    }
    fclose(file);
}

static void collect_network(struct metrics_state *state,
                            struct metrics_snapshot *snapshot)
{
    if (default_route_interface(snapshot->interface,
                                sizeof(snapshot->interface)) != 0)
        fallback_interface(snapshot->interface, sizeof(snapshot->interface));

    if (!snapshot->interface[0]) {
        snprintf(snapshot->interface, sizeof(snapshot->interface), "offline");
        return;
    }

    interface_ipv4(snapshot->interface, snapshot->ipv4, sizeof(snapshot->ipv4));
    if (!snapshot->ipv4[0])
        snprintf(snapshot->ipv4, sizeof(snapshot->ipv4), "no IPv4");

    char path[PATH_MAX];
    char state_text[32] = {0};
    snprintf(path, sizeof(path), "/sys/class/net/%s/operstate",
             snapshot->interface);
    FILE *file = fopen(path, "r");
    if (file) {
        if (fgets(state_text, sizeof(state_text), file))
            snapshot->link_up = strncmp(state_text, "up", 2) == 0 ||
                                strncmp(state_text, "unknown", 7) == 0;
        fclose(file);
    }

    snprintf(path, sizeof(path), "/sys/class/net/%s/statistics/rx_bytes",
             snapshot->interface);
    read_u64_file(path, &snapshot->rx_bytes);
    snprintf(path, sizeof(path), "/sys/class/net/%s/statistics/tx_bytes",
             snapshot->interface);
    read_u64_file(path, &snapshot->tx_bytes);

    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    double elapsed = (double)(now.tv_sec - state->previous_network_time.tv_sec) +
                     (double)(now.tv_nsec - state->previous_network_time.tv_nsec) /
                         1000000000.0;

    if (state->have_network_sample &&
        strcmp(state->previous_interface, snapshot->interface) == 0 &&
        elapsed > 0.05) {
        if (snapshot->rx_bytes >= state->previous_rx_bytes)
            snapshot->rx_bytes_per_second =
                (double)(snapshot->rx_bytes - state->previous_rx_bytes) / elapsed;
        if (snapshot->tx_bytes >= state->previous_tx_bytes)
            snapshot->tx_bytes_per_second =
                (double)(snapshot->tx_bytes - state->previous_tx_bytes) / elapsed;
    }

    snprintf(state->previous_interface, sizeof(state->previous_interface), "%s",
             snapshot->interface);
    state->previous_rx_bytes = snapshot->rx_bytes;
    state->previous_tx_bytes = snapshot->tx_bytes;
    state->previous_network_time = now;
    state->have_network_sample = true;

    collect_wireless_signal(snapshot);
}

void metrics_state_init(struct metrics_state *state)
{
    memset(state, 0, sizeof(*state));
}

int metrics_collect(struct metrics_state *state, struct metrics_snapshot *snapshot)
{
    if (!state || !snapshot) {
        errno = EINVAL;
        return -1;
    }

    memset(snapshot, 0, sizeof(*snapshot));
    snapshot->temperature_millic = LONG_MIN;
    snapshot->wall_time = time(NULL);
    long online_cpus = sysconf(_SC_NPROCESSORS_ONLN);
    snapshot->cpu_count = online_cpus > 0 ? (unsigned int)online_cpus : 0;
    getloadavg(&snapshot->load_one, 1);
    gethostname(snapshot->hostname, sizeof(snapshot->hostname) - 1);

    struct sysinfo system_info;
    if (sysinfo(&system_info) == 0)
        snapshot->uptime_seconds = (uint64_t)system_info.uptime;

    struct utsname uts;
    if (uname(&uts) == 0)
        snprintf(snapshot->kernel, sizeof(snapshot->kernel), "%s", uts.release);

    struct timex time_status;
    memset(&time_status, 0, sizeof(time_status));
    int time_result = adjtimex(&time_status);
    snapshot->time_synchronized =
        time_result != TIME_ERROR && !(time_status.status & STA_UNSYNC);

    collect_cpu(state, snapshot);
    collect_memory(snapshot);
    collect_storage(snapshot);
    collect_temperature(snapshot);
    collect_network(state, snapshot);
    return 0;
}
