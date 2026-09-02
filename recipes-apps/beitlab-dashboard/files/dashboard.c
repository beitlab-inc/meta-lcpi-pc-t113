#define _GNU_SOURCE

#include "beitlab_identity.h"
#include "fbdev_port.h"
#include "location.h"
#include "metrics.h"

#include <limits.h>
#include <lvgl/lvgl.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define HISTORY_SECONDS 10

static volatile sig_atomic_t running = 1;
static struct metrics_state metric_state;

struct dashboard_widgets {
    lv_obj_t *clock;
    lv_obj_t *date;
    lv_obj_t *location;
    lv_obj_t *cpu_label;
    lv_obj_t *cpu_value;
    lv_obj_t *cpu_bar;
    lv_obj_t *memory_value;
    lv_obj_t *memory_bar;
    lv_obj_t *system_details;
    lv_obj_t *network_name;
    lv_obj_t *network_address;
    lv_obj_t *rx_rate;
    lv_obj_t *tx_rate;
    lv_obj_t *network_totals;
    lv_obj_t *chart;
    lv_chart_series_t *rx_series;
    lv_chart_series_t *tx_series;
    lv_obj_t *chart_scale;
    lv_obj_t *footer;
    int32_t rx_history[HISTORY_SECONDS];
    int32_t tx_history[HISTORY_SECONDS];
};

static struct dashboard_widgets widgets;

static void handle_signal(int signal_number)
{
    (void)signal_number;
    running = 0;
}

static void format_bytes(double bytes, char *buffer, size_t size)
{
    static const char *units[] = {"B", "KiB", "MiB", "GiB"};
    unsigned int unit = 0;
    while (bytes >= 1024.0 && unit < 3) {
        bytes /= 1024.0;
        ++unit;
    }
    if (unit == 0)
        snprintf(buffer, size, "%.0f %s", bytes, units[unit]);
    else
        snprintf(buffer, size, "%.1f %s", bytes, units[unit]);
}

static void format_rate(double bytes_per_second, char *buffer, size_t size)
{
    char amount[32];
    format_bytes(bytes_per_second, amount, sizeof(amount));
    snprintf(buffer, size, "%s/s", amount);
}

static void format_uptime(uint64_t seconds, char *buffer, size_t size)
{
    uint64_t days = seconds / 86400;
    unsigned int hours = (unsigned int)((seconds % 86400) / 3600);
    unsigned int minutes = (unsigned int)((seconds % 3600) / 60);
    if (days)
        snprintf(buffer, size, "%llud %02uh %02um",
                 (unsigned long long)days, hours, minutes);
    else
        snprintf(buffer, size, "%02uh %02um", hours, minutes);
}

static lv_obj_t *make_label(lv_obj_t *parent, const char *text,
                            const lv_font_t *font, lv_color_t color)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, color, 0);
    return label;
}

static lv_obj_t *make_panel(lv_obj_t *parent, int x, int y, int width,
                            int height, const char *title)
{
    lv_obj_t *panel = lv_obj_create(parent);
    lv_obj_set_pos(panel, x, y);
    lv_obj_set_size(panel, width, height);
    lv_obj_set_style_bg_color(panel, lv_color_hex(0x0d1b2a), 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(panel, lv_color_hex(0x223a50), 0);
    lv_obj_set_style_border_width(panel, 1, 0);
    lv_obj_set_style_radius(panel, 8, 0);
    lv_obj_set_style_pad_all(panel, 14, 0);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *heading =
        make_label(panel, title, &lv_font_montserrat_14, lv_color_hex(0x7f9bb3));
    lv_obj_align(heading, LV_ALIGN_TOP_LEFT, 0, 0);
    return panel;
}

static lv_obj_t *make_usage_bar(lv_obj_t *parent, int y, lv_color_t color)
{
    lv_obj_t *bar = lv_bar_create(parent);
    lv_obj_set_pos(bar, 0, y);
    lv_obj_set_size(bar, 220, 8);
    lv_bar_set_range(bar, 0, 100);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0x172d40), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar, color, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(bar, 4, LV_PART_MAIN);
    lv_obj_set_style_radius(bar, 4, LV_PART_INDICATOR);
    return bar;
}

static void build_interface(void)
{
    lv_obj_t *screen = lv_scr_act();
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x07111d), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *brand =
        make_label(screen, "BEITLAB", &lv_font_montserrat_32,
                   lv_color_hex(0x3fa9f5));
    lv_obj_set_pos(brand, 16, 10);

    lv_obj_t *product =
        make_label(screen, "BeitlabOS  |  " BEITLAB_PRODUCT_TEXT,
                   &lv_font_montserrat_14, lv_color_hex(0x9eb4c7));
    lv_obj_set_pos(product, 18, 49);

    lv_obj_t *identity =
        make_label(screen, BEITLAB_BOARD_TEXT "  |  Release " BEITLAB_RELEASE_TEXT,
                   &lv_font_montserrat_12, lv_color_hex(0x607f98));
    lv_obj_set_pos(identity, 18, 67);

    widgets.clock =
        make_label(screen, "--:--:--", &lv_font_montserrat_36,
                   lv_color_hex(0xf2f7fb));
    lv_obj_set_width(widgets.clock, 250);
    lv_obj_set_style_text_align(widgets.clock, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_pos(widgets.clock, 534, 4);

    widgets.date =
        make_label(screen, "---", &lv_font_montserrat_14,
                   lv_color_hex(0x9eb4c7));
    lv_obj_set_width(widgets.date, 250);
    lv_obj_set_style_text_align(widgets.date, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_pos(widgets.date, 534, 48);

    widgets.location =
        make_label(screen, "Locating...", &lv_font_montserrat_12,
                   lv_color_hex(0x3fa9f5));
    lv_obj_set_width(widgets.location, 250);
    lv_obj_set_style_text_align(widgets.location, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_pos(widgets.location, 534, 68);

    lv_obj_t *performance = make_panel(screen, 16, 96, 250, 176, "SYSTEM LOAD");
    widgets.cpu_label =
        make_label(performance, "CPU", &lv_font_montserrat_14,
                   lv_color_hex(0x9eb4c7));
    lv_obj_set_pos(widgets.cpu_label, 0, 22);
    widgets.cpu_value =
        make_label(performance, "-- %", &lv_font_montserrat_28,
                   lv_color_hex(0x5ec8ff));
    lv_obj_set_width(widgets.cpu_value, 220);
    lv_obj_set_style_text_align(widgets.cpu_value, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_pos(widgets.cpu_value, 0, 16);
    widgets.cpu_bar = make_usage_bar(performance, 56, lv_color_hex(0x3fa9f5));

    lv_obj_t *memory_label =
        make_label(performance, "MEMORY", &lv_font_montserrat_14,
                   lv_color_hex(0x9eb4c7));
    lv_obj_set_pos(memory_label, 0, 80);
    widgets.memory_value =
        make_label(performance, "--", &lv_font_montserrat_16,
                   lv_color_hex(0xf2f7fb));
    lv_obj_set_width(widgets.memory_value, 220);
    lv_obj_set_style_text_align(widgets.memory_value, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_pos(widgets.memory_value, 0, 102);
    widgets.memory_bar =
        make_usage_bar(performance, 128, lv_color_hex(0x66d9a8));

    lv_obj_t *system = make_panel(screen, 16, 288, 250, 155, "DEVICE");
    widgets.system_details =
        make_label(system, "Collecting system data...", &lv_font_montserrat_14,
                   lv_color_hex(0xc3d1dc));
    lv_obj_set_pos(widgets.system_details, 0, 29);
    lv_label_set_long_mode(widgets.system_details, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(widgets.system_details, 220);
    lv_obj_set_style_text_line_space(widgets.system_details, 8, 0);

    lv_obj_t *network = make_panel(screen, 282, 96, 502, 347, "NETWORK");
    widgets.network_name =
        make_label(network, "OFFLINE", &lv_font_montserrat_20,
                   lv_color_hex(0xf2f7fb));
    lv_obj_set_pos(widgets.network_name, 0, 25);
    widgets.network_address =
        make_label(network, "No active interface", &lv_font_montserrat_14,
                   lv_color_hex(0x7f9bb3));
    lv_obj_set_pos(widgets.network_address, 0, 51);

    lv_obj_t *rx_heading =
        make_label(network, "RX", &lv_font_montserrat_14, lv_color_hex(0x5ec8ff));
    lv_obj_set_pos(rx_heading, 0, 76);
    widgets.rx_rate =
        make_label(network, "0 B/s", &lv_font_montserrat_20,
                   lv_color_hex(0x5ec8ff));
    lv_obj_set_pos(widgets.rx_rate, 36, 72);
    lv_obj_t *tx_heading =
        make_label(network, "TX", &lv_font_montserrat_14,
                   lv_color_hex(0x66d9a8));
    lv_obj_set_pos(tx_heading, 236, 76);
    widgets.tx_rate =
        make_label(network, "0 B/s", &lv_font_montserrat_20,
                   lv_color_hex(0x66d9a8));
    lv_obj_set_pos(widgets.tx_rate, 272, 72);

    widgets.chart = lv_chart_create(network);
    lv_obj_set_pos(widgets.chart, 0, 110);
    lv_obj_set_size(widgets.chart, 472, 154);
    lv_chart_set_type(widgets.chart, LV_CHART_TYPE_LINE);
    lv_chart_set_update_mode(widgets.chart, LV_CHART_UPDATE_MODE_SHIFT);
    lv_chart_set_point_count(widgets.chart, HISTORY_SECONDS);
    lv_chart_set_range(widgets.chart, LV_CHART_AXIS_PRIMARY_Y, 0, 32);
    lv_obj_set_style_bg_color(widgets.chart, lv_color_hex(0x091623), 0);
    lv_obj_set_style_bg_opa(widgets.chart, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(widgets.chart, lv_color_hex(0x223a50), 0);
    lv_obj_set_style_border_width(widgets.chart, 1, 0);
    lv_obj_set_style_line_color(widgets.chart, lv_color_hex(0x1a3042),
                                LV_PART_MAIN);
    lv_obj_set_style_size(widgets.chart, 0, LV_PART_INDICATOR);
    lv_obj_set_style_line_width(widgets.chart, 3, LV_PART_ITEMS);
    lv_chart_set_div_line_count(widgets.chart, 4, 10);
    widgets.rx_series =
        lv_chart_add_series(widgets.chart, lv_color_hex(0x5ec8ff),
                            LV_CHART_AXIS_PRIMARY_Y);
    widgets.tx_series =
        lv_chart_add_series(widgets.chart, lv_color_hex(0x66d9a8),
                            LV_CHART_AXIS_PRIMARY_Y);
    lv_chart_set_all_value(widgets.chart, widgets.rx_series, 0);
    lv_chart_set_all_value(widgets.chart, widgets.tx_series, 0);

    widgets.network_totals =
        make_label(network, "Totals RX 0 B  |  TX 0 B", &lv_font_montserrat_12,
                   lv_color_hex(0x9eb4c7));
    lv_obj_set_pos(widgets.network_totals, 0, 270);

    widgets.chart_scale =
        make_label(network, "Last 10 seconds  |  Scale 32 KiB/s",
                   &lv_font_montserrat_12, lv_color_hex(0x607f98));
    lv_obj_set_pos(widgets.chart_scale, 0, 290);

    widgets.footer =
        make_label(screen, "Initializing BeitlabOS dashboard...",
                   &lv_font_montserrat_12, lv_color_hex(0x607f98));
    lv_obj_set_pos(widgets.footer, 18, 457);
    lv_obj_set_width(widgets.footer, 764);
    lv_obj_set_style_text_align(widgets.footer, LV_TEXT_ALIGN_CENTER, 0);
}

static int32_t next_chart_limit(void)
{
    int32_t maximum = 0;
    for (unsigned int index = 0; index < HISTORY_SECONDS; ++index) {
        if (widgets.rx_history[index] > maximum)
            maximum = widgets.rx_history[index];
        if (widgets.tx_history[index] > maximum)
            maximum = widgets.tx_history[index];
    }

    int32_t limit = 32;
    while (limit < maximum && limit < 1024 * 1024)
        limit *= 2;
    return limit;
}

static void update_dashboard(lv_timer_t *timer)
{
    (void)timer;
    struct metrics_snapshot snapshot;
    if (metrics_collect(&metric_state, &snapshot) != 0)
        return;

    struct location_info location;
    struct tm local;
    char text[256];

    location_copy(&location);
    if (localtime_r(&snapshot.wall_time, &local)) {
        strftime(text, sizeof(text), "%H:%M:%S", &local);
        lv_label_set_text(widgets.clock, text);
        strftime(text, sizeof(text), "%A, %d %B %Y  %Z", &local);
        lv_label_set_text(widgets.date, text);
    }
    if (location.valid)
        lv_label_set_text(widgets.location, location.summary);
    else if (location.summary[0])
        lv_label_set_text(widgets.location, location.summary);

    int cpu_percent = (int)(snapshot.cpu_percent + 0.5);
    if (cpu_percent < 0)
        cpu_percent = 0;
    if (cpu_percent > 100)
        cpu_percent = 100;
    lv_label_set_text_fmt(widgets.cpu_label, "CPU  %u cores", snapshot.cpu_count);
    lv_label_set_text_fmt(widgets.cpu_value, "%d %%", cpu_percent);
    lv_bar_set_value(widgets.cpu_bar, cpu_percent, LV_ANIM_ON);

    uint64_t used_memory =
        snapshot.memory_total_kib > snapshot.memory_available_kib ?
        snapshot.memory_total_kib - snapshot.memory_available_kib : 0;
    int memory_percent = snapshot.memory_total_kib ?
        (int)(used_memory * 100 / snapshot.memory_total_kib) : 0;
    lv_label_set_text_fmt(widgets.memory_value, "%llu / %llu MiB",
                          (unsigned long long)(used_memory / 1024),
                          (unsigned long long)(snapshot.memory_total_kib / 1024));
    lv_bar_set_value(widgets.memory_bar, memory_percent, LV_ANIM_ON);

    char uptime[48];
    format_uptime(snapshot.uptime_seconds, uptime, sizeof(uptime));
    int storage_percent = snapshot.root_total_bytes ?
        (int)((snapshot.root_total_bytes - snapshot.root_available_bytes) * 100 /
              snapshot.root_total_bytes) : 0;
    char temperature[32];
    if (snapshot.temperature_millic == LONG_MIN)
        snprintf(temperature, sizeof(temperature), "unavailable");
    else
        snprintf(temperature, sizeof(temperature), "%.1f C",
                 snapshot.temperature_millic / 1000.0);

    lv_label_set_text_fmt(widgets.system_details,
                          "Temperature   %s\n"
                          "Root storage  %d%% used\n"
                          "Uptime        %s\n"
                          "Load average  %.2f",
                          temperature, storage_percent, uptime,
                          snapshot.load_one);

    lv_label_set_text_fmt(widgets.network_name, "%s  %s",
                          snapshot.interface,
                          snapshot.link_up ? "CONNECTED" : "OFFLINE");

    if (snapshot.has_wireless_signal)
        lv_label_set_text_fmt(widgets.network_address, "%s  |  Signal %d dBm",
                              snapshot.ipv4, snapshot.wireless_dbm);
    else
        lv_label_set_text(widgets.network_address, snapshot.ipv4);

    char rx_rate[32], tx_rate[32], rx_total[32], tx_total[32];
    format_rate(snapshot.rx_bytes_per_second, rx_rate, sizeof(rx_rate));
    format_rate(snapshot.tx_bytes_per_second, tx_rate, sizeof(tx_rate));
    format_bytes((double)snapshot.rx_bytes, rx_total, sizeof(rx_total));
    format_bytes((double)snapshot.tx_bytes, tx_total, sizeof(tx_total));
    lv_label_set_text(widgets.rx_rate, rx_rate);
    lv_label_set_text(widgets.tx_rate, tx_rate);
    lv_label_set_text_fmt(widgets.network_totals, "Totals RX %s  |  TX %s",
                          rx_total, tx_total);

    memmove(&widgets.rx_history[0], &widgets.rx_history[1],
            sizeof(widgets.rx_history) - sizeof(widgets.rx_history[0]));
    memmove(&widgets.tx_history[0], &widgets.tx_history[1],
            sizeof(widgets.tx_history) - sizeof(widgets.tx_history[0]));
    widgets.rx_history[HISTORY_SECONDS - 1] =
        (int32_t)(snapshot.rx_bytes_per_second / 1024.0 + 0.5);
    widgets.tx_history[HISTORY_SECONDS - 1] =
        (int32_t)(snapshot.tx_bytes_per_second / 1024.0 + 0.5);

    int32_t chart_limit = next_chart_limit();
    lv_chart_set_range(widgets.chart, LV_CHART_AXIS_PRIMARY_Y, 0, chart_limit);
    lv_chart_set_next_value(widgets.chart, widgets.rx_series,
                            widgets.rx_history[HISTORY_SECONDS - 1]);
    lv_chart_set_next_value(widgets.chart, widgets.tx_series,
                            widgets.tx_history[HISTORY_SECONDS - 1]);
    lv_label_set_text_fmt(widgets.chart_scale,
                          "Last 10 seconds  |  Scale %d KiB/s", chart_limit);

    lv_label_set_text_fmt(
        widgets.footer, "%s  |  Linux %s  |  Build %s  |  Clock %s%s%s",
        snapshot.hostname, snapshot.kernel, BEITLAB_BUILD_TEXT,
        snapshot.time_synchronized ? "NTP synced" : "waiting for NTP",
        location.valid && location.timezone[0] ? "  |  " : "",
        location.valid && location.timezone[0] ? location.timezone : "");
}

static int print_metrics_once(void)
{
    struct metrics_state state;
    struct metrics_snapshot first, snapshot;
    metrics_state_init(&state);
    if (metrics_collect(&state, &first) != 0)
        return 1;
    sleep(1);
    if (metrics_collect(&state, &snapshot) != 0)
        return 1;

    char rx[32], tx[32];
    format_rate(snapshot.rx_bytes_per_second, rx, sizeof(rx));
    format_rate(snapshot.tx_bytes_per_second, tx, sizeof(tx));
    printf("brand=%s product=%s board=%s release=%s\n",
           BEITLAB_VENDOR_TEXT, BEITLAB_PRODUCT_TEXT, BEITLAB_BOARD_TEXT,
           BEITLAB_RELEASE_TEXT);
    printf("cpu=%.1f%% memory=%llu/%lluKiB temperature=%ldmC uptime=%llus\n",
           snapshot.cpu_percent,
           (unsigned long long)(snapshot.memory_total_kib -
                                snapshot.memory_available_kib),
           (unsigned long long)snapshot.memory_total_kib,
           snapshot.temperature_millic,
           (unsigned long long)snapshot.uptime_seconds);
    printf("interface=%s address=%s link=%s rx=%s tx=%s\n",
           snapshot.interface, snapshot.ipv4,
           snapshot.link_up ? "up" : "down", rx, tx);
    location_init();
    sleep(4);
    struct location_info location;
    location_copy(&location);
    printf("location=%s timezone=%s applied=%d\n",
           location.summary, location.timezone, location.timezone_applied);
    location_shutdown();
    return 0;
}

int main(int argc, char **argv)
{
    const char *framebuffer = "/dev/fb0";
    const char *tty = "/dev/tty1";

    for (int index = 1; index < argc; ++index) {
        if (strcmp(argv[index], "--once") == 0)
            return print_metrics_once();
        if (strcmp(argv[index], "-f") == 0 && index + 1 < argc)
            framebuffer = argv[++index];
        else if (strcmp(argv[index], "-t") == 0 && index + 1 < argc)
            tty = argv[++index];
        else {
            fprintf(stderr,
                    "Usage: %s [--once] [-f /dev/fb0] [-t /dev/tty1]\n",
                    argv[0]);
            return 2;
        }
    }

    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);
    signal(SIGHUP, handle_signal);

    lv_init();
    if (fbdev_port_init(framebuffer, tty) != 0)
        return 1;
    if (fbdev_port_width() < 640 || fbdev_port_height() < 400) {
        fprintf(stderr, "dashboard: display is too small (%dx%d)\n",
                fbdev_port_width(), fbdev_port_height());
        fbdev_port_shutdown();
        return 1;
    }

    metrics_state_init(&metric_state);
    location_init();
    memset(&widgets, 0, sizeof(widgets));
    build_interface();
    update_dashboard(NULL);
    lv_timer_create(update_dashboard, 1000, NULL);

    struct timespec previous;
    clock_gettime(CLOCK_MONOTONIC, &previous);
    while (running) {
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        int64_t elapsed_ns =
            (int64_t)(now.tv_sec - previous.tv_sec) * 1000000000LL +
            (int64_t)(now.tv_nsec - previous.tv_nsec);
        uint64_t elapsed_ms =
            elapsed_ns > 0 ? (uint64_t)elapsed_ns / 1000000U : 0;
        if (elapsed_ms) {
            if (elapsed_ms > UINT32_MAX)
                elapsed_ms = UINT32_MAX;
            lv_tick_inc((uint32_t)elapsed_ms);
            previous = now;
        }
        lv_timer_handler();
        usleep(5000);
    }

    location_shutdown();
    fbdev_port_shutdown();
    return 0;
}
