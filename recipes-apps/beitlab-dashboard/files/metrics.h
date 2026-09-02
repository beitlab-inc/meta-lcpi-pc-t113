#ifndef BEITLAB_METRICS_H
#define BEITLAB_METRICS_H

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#define METRICS_IFACE_LEN 32
#define METRICS_ADDR_LEN 64
#define METRICS_TEXT_LEN 128

struct metrics_snapshot {
    double cpu_percent;
    unsigned int cpu_count;
    double load_one;
    uint64_t memory_total_kib;
    uint64_t memory_available_kib;
    uint64_t root_total_bytes;
    uint64_t root_available_bytes;
    long temperature_millic;
    uint64_t uptime_seconds;
    time_t wall_time;
    bool time_synchronized;

    char interface[METRICS_IFACE_LEN];
    char ipv4[METRICS_ADDR_LEN];
    bool link_up;
    uint64_t rx_bytes;
    uint64_t tx_bytes;
    double rx_bytes_per_second;
    double tx_bytes_per_second;
    bool has_wireless_signal;
    int wireless_dbm;

    char hostname[METRICS_TEXT_LEN];
    char kernel[METRICS_TEXT_LEN];
};

struct metrics_state {
    bool have_cpu_sample;
    uint64_t previous_cpu_total;
    uint64_t previous_cpu_idle;

    bool have_network_sample;
    char previous_interface[METRICS_IFACE_LEN];
    uint64_t previous_rx_bytes;
    uint64_t previous_tx_bytes;
    struct timespec previous_network_time;
};

void metrics_state_init(struct metrics_state *state);
int metrics_collect(struct metrics_state *state, struct metrics_snapshot *snapshot);

#endif
