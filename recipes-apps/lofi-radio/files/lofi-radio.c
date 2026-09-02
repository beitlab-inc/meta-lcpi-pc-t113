#define _GNU_SOURCE

#include <errno.h>
#include <glib.h>
#include <glib-unix.h>
#include <gst/gst.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define STATIONS_PATH "/etc/lofi-radio/stations"
#define CURRENT_PATH "/run/lofi-radio.station"
#define TITLE_PATH "/run/lofi-radio.title"
#define MAX_STATIONS 16

struct station {
    char name[96];
    char url[256];
};

static struct station stations[MAX_STATIONS];
static unsigned int station_count;
static unsigned int station_index;
static GstElement *pipeline;
static GMainLoop *loop;
static guint reconnect_source;
static volatile sig_atomic_t running = 1;

static void write_text(const char *path, const char *text)
{
    FILE *file = fopen(path, "w");
    if (!file)
        return;
    fputs(text ? text : "", file);
    fputc('\n', file);
    fclose(file);
}

static unsigned int load_stations(const char *path)
{
    FILE *file = fopen(path, "r");
    char line[384];

    station_count = 0;
    if (!file) {
        fprintf(stderr, "lofi-radio: cannot read %s: %s\n", path, strerror(errno));
        return 0;
    }

    while (fgets(line, sizeof(line), file) && station_count < MAX_STATIONS) {
        char *separator;
        char *newline;
        size_t name_len;

        if (line[0] == '#' || line[0] == '\n')
            continue;
        newline = strchr(line, '\n');
        if (newline)
            *newline = '\0';
        separator = strchr(line, '|');
        if (!separator || separator == line || !separator[1])
            continue;

        name_len = (size_t)(separator - line);
        if (name_len >= sizeof(stations[0].name))
            name_len = sizeof(stations[0].name) - 1;
        memcpy(stations[station_count].name, line, name_len);
        stations[station_count].name[name_len] = '\0';
        snprintf(stations[station_count].url, sizeof(stations[0].url), "%s",
                 separator + 1);
        station_count++;
    }
    fclose(file);
    return station_count;
}

static void remember_station(void)
{
    if (station_index >= station_count)
        return;
    write_text(CURRENT_PATH, stations[station_index].name);
    write_text(TITLE_PATH, stations[station_index].name);
    printf("lofi-radio: %s\n", stations[station_index].name);
    fflush(stdout);
}

static void destroy_pipeline(void)
{
    if (!pipeline)
        return;
    gst_element_set_state(pipeline, GST_STATE_NULL);
    gst_object_unref(pipeline);
    pipeline = NULL;
}

static gboolean start_pipeline(gpointer unused);

static gboolean reconnect_later(gpointer unused)
{
    (void)unused;
    reconnect_source = 0;
    if (running)
        start_pipeline(NULL);
    return G_SOURCE_REMOVE;
}

static void schedule_reconnect(unsigned int delay_seconds)
{
    if (reconnect_source)
        g_source_remove(reconnect_source);
    reconnect_source = g_timeout_add_seconds(delay_seconds, reconnect_later, NULL);
}

static gboolean on_bus(GstBus *bus, GstMessage *message, gpointer user_data)
{
    (void)bus;
    (void)user_data;

    switch (GST_MESSAGE_TYPE(message)) {
    case GST_MESSAGE_ERROR: {
        GError *error = NULL;
        gchar *debug = NULL;
        gst_message_parse_error(message, &error, &debug);
        fprintf(stderr, "lofi-radio: %s\n", error ? error->message : "stream error");
        g_clear_error(&error);
        g_free(debug);
        destroy_pipeline();
        schedule_reconnect(5);
        break;
    }
    case GST_MESSAGE_EOS:
        destroy_pipeline();
        schedule_reconnect(2);
        break;
    case GST_MESSAGE_TAG: {
        GstTagList *tags = NULL;
        gchar *title = NULL;
        gst_message_parse_tag(message, &tags);
        if (tags && gst_tag_list_get_string(tags, GST_TAG_TITLE, &title) && title) {
            write_text(TITLE_PATH, title);
            g_free(title);
        }
        if (tags)
            gst_tag_list_unref(tags);
        break;
    }
    default:
        break;
    }
    return TRUE;
}

static gboolean start_pipeline(gpointer unused)
{
    GError *error = NULL;
    GstBus *bus;
    gchar *description;

    (void)unused;
    if (!running || !station_count)
        return G_SOURCE_REMOVE;

    destroy_pipeline();
    remember_station();
    description = g_strdup_printf(
        "souphttpsrc location=\"%s\" is-live=true timeout=15 retries=5 "
        "user-agent=\"BeitlabOS-lofi-radio/1.0\" ! "
        "decodebin ! audioconvert ! audioresample ! "
        "volume name=vol volume=0.85 ! alsasink sync=false",
        stations[station_index].url);
    pipeline = gst_parse_launch(description, &error);
    g_free(description);
    if (!pipeline) {
        fprintf(stderr, "lofi-radio: cannot build pipeline: %s\n",
                error ? error->message : "unknown");
        g_clear_error(&error);
        schedule_reconnect(8);
        return G_SOURCE_REMOVE;
    }

    bus = gst_element_get_bus(pipeline);
    gst_bus_add_watch(bus, on_bus, NULL);
    gst_object_unref(bus);
    if (gst_element_set_state(pipeline, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE) {
        fprintf(stderr, "lofi-radio: playback failed\n");
        destroy_pipeline();
        schedule_reconnect(8);
    }
    return G_SOURCE_REMOVE;
}

static gboolean next_station(gpointer step_pointer)
{
    int step = GPOINTER_TO_INT(step_pointer);
    if (!station_count)
        return G_SOURCE_REMOVE;
    station_index = (station_index + (unsigned int)(step + (int)station_count)) %
                    station_count;
    if (reconnect_source) {
        g_source_remove(reconnect_source);
        reconnect_source = 0;
    }
    start_pipeline(NULL);
    return G_SOURCE_REMOVE;
}

static gboolean on_quit_signal(gpointer unused)
{
    (void)unused;
    running = 0;
    if (loop)
        g_main_loop_quit(loop);
    return G_SOURCE_REMOVE;
}

static void list_stations(void)
{
    unsigned int index;
    for (index = 0; index < station_count; ++index)
        printf("%u  %s\n    %s\n", index + 1, stations[index].name,
               stations[index].url);
}

int main(int argc, char **argv)
{
    const char *stations_path = STATIONS_PATH;
    int list_only = 0;
    int index;

    gst_init(&argc, &argv);
    for (index = 1; index < argc; ++index) {
        if (strcmp(argv[index], "--list") == 0)
            list_only = 1;
        else if (strcmp(argv[index], "--stations") == 0 && index + 1 < argc)
            stations_path = argv[++index];
        else if (strcmp(argv[index], "--station") == 0 && index + 1 < argc)
            station_index = (unsigned int)strtoul(argv[++index], NULL, 10);
        else {
            fprintf(stderr,
                    "Usage: %s [--list] [--stations FILE] [--station N]\n",
                    argv[0]);
            return 2;
        }
    }

    if (!load_stations(stations_path))
        return 1;
    if (station_index >= station_count)
        station_index = 0;
    if (list_only) {
        list_stations();
        return 0;
    }

    g_unix_signal_add(SIGINT, on_quit_signal, NULL);
    g_unix_signal_add(SIGTERM, on_quit_signal, NULL);
    g_unix_signal_add(SIGHUP, on_quit_signal, NULL);
    g_unix_signal_add(SIGUSR1, next_station, GINT_TO_POINTER(1));
    g_unix_signal_add(SIGUSR2, next_station, GINT_TO_POINTER(-1));

    loop = g_main_loop_new(NULL, FALSE);
    start_pipeline(NULL);
    g_main_loop_run(loop);

    if (reconnect_source)
        g_source_remove(reconnect_source);
    destroy_pipeline();
    g_main_loop_unref(loop);
    unlink(CURRENT_PATH);
    unlink(TITLE_PATH);
    return 0;
}
