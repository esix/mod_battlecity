/*
 * Standalone GStreamer test for mod_battlecity
 * Renders the game world and displays it in a window with keyboard control.
 *
 * Build:
 *   g++ -std=c++11 -o standalone_test standalone_test.cpp renderer.cpp world.cpp \
 *       $(pkg-config --cflags --libs gstreamer-1.0) -lpthread -framework Cocoa
 *
 * Run:
 *   ./standalone_test          # display in window
 *   ./standalone_test file     # save to /tmp/battlecity.h264
 */

#include <gst/gst.h>
#include <glib.h>
#include <pthread.h>
#include <unistd.h>
#include <termios.h>
#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>

#include "renderer.h"
#include "world.h"

static World *world = nullptr;
static Renderer *renderer = nullptr;
static volatile bool is_running = false;
static GMainLoop *main_loop = nullptr;

static std::shared_ptr<Player> g_player;

// World simulation thread
static void *world_thread_func(void *) {
    is_running = true;
    world = new World();
    while (is_running) {
        world->live();
        usleep(1000 * 40);
    }
    return nullptr;
}

// GStreamer appsrc callback
static gboolean cb_need_data(GstElement *appsrc, guint, gpointer) {
    static GstClockTime timestamp = 0;

    Canvas canvas;
    renderer->render_world(g_player, canvas);

    guint size = canvas.get_data_size();
    GstBuffer *buffer = gst_buffer_new_and_alloc(size);
    GstMapInfo info;
    gst_buffer_map(buffer, &info, GST_MAP_WRITE);
    memcpy(info.data, canvas.get_data(), size);
    gst_buffer_unmap(buffer, &info);

    GST_BUFFER_PTS(buffer) = timestamp;
    GST_BUFFER_DURATION(buffer) = gst_util_uint64_scale_int(1, GST_SECOND, 15);
    timestamp += GST_BUFFER_DURATION(buffer);

    GstFlowReturn ret;
    g_signal_emit_by_name(appsrc, "push-buffer", buffer, &ret);
    gst_buffer_unref(buffer);

    return (ret == GST_FLOW_OK) ? TRUE : FALSE;
}

// Terminal raw mode helpers
static struct termios orig_termios;

static void terminal_raw_mode() {
    tcgetattr(STDIN_FILENO, &orig_termios);
    struct termios raw = orig_termios;
    raw.c_lflag &= ~(ECHO | ICANON);
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

static void terminal_restore() {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
}

// Keyboard input thread
static void *input_thread_func(void *) {
    terminal_raw_mode();

    while (is_running) {
        int ch = getchar();
        if (ch == EOF) break;

        switch (ch) {
            case 'w': case 'W': g_player->command_move(E_NORTH); break;
            case 's': case 'S': g_player->command_move(E_SOUTH); break;
            case 'a': case 'A': g_player->command_move(E_WEST);  break;
            case 'd': case 'D': g_player->command_move(E_EAST);  break;
            case ' ':           g_player->command_fire();         break;
            case 'p': case 'P': g_player->get_world()->print();  break;
            case 'q': case 'Q': case 3:
                is_running = false;
                if (main_loop) g_main_loop_quit(main_loop);
                break;
        }
    }

    terminal_restore();
    return nullptr;
}

// Bus message handler
static gboolean bus_call(GstBus *bus, GstMessage *msg, gpointer) {
    switch (GST_MESSAGE_TYPE(msg)) {
        case GST_MESSAGE_ERROR: {
            GError *err;
            gchar *debug;
            gst_message_parse_error(msg, &err, &debug);
            g_printerr("Error: %s\n", err->message);
            g_error_free(err);
            g_free(debug);
            g_main_loop_quit(main_loop);
            break;
        }
        case GST_MESSAGE_EOS:
            g_main_loop_quit(main_loop);
            break;
        default:
            break;
    }
    return TRUE;
}

static void run_app(int argc, char *argv[]) {
    bool use_file = (argc > 1 && strcmp(argv[1], "file") == 0);

    renderer = new Renderer();
    pthread_t world_tid;
    pthread_create(&world_tid, nullptr, world_thread_func, nullptr);
    while (!world) usleep(10000);

    g_player = world->add_player();

    const char *pipeline_desc;
    if (use_file) {
        pipeline_desc =
            "appsrc name=src ! "
            "video/x-raw,format=RGB,width=320,height=240,framerate=15/1 ! "
            "videoconvert ! "
            "x264enc byte-stream=true bframes=0 speed-preset=3 bitrate=256 ! "
            "video/x-h264,stream-format=byte-stream,profile=baseline ! "
            "filesink location=/tmp/battlecity.h264";
    } else {
        pipeline_desc =
            "appsrc name=src ! "
            "video/x-raw,format=RGB,width=320,height=240,framerate=15/1 ! "
            "videoconvert ! "
            "videoscale ! video/x-raw,width=640,height=480 ! "
            "autovideosink";
    }

    GError *error = nullptr;
    GstElement *pipeline = gst_parse_launch(pipeline_desc, &error);
    if (error) {
        g_printerr("Pipeline error: %s\n", error->message);
        g_error_free(error);
        return;
    }

    GstElement *src = gst_bin_get_by_name(GST_BIN(pipeline), "src");
    g_object_set(G_OBJECT(src), "stream-type", 0, "format", GST_FORMAT_TIME, NULL);
    g_signal_connect(src, "need-data", G_CALLBACK(cb_need_data), nullptr);
    gst_object_unref(src);

    // Bus watcher
    GstBus *bus = gst_element_get_bus(pipeline);
    gst_bus_add_watch(bus, bus_call, nullptr);
    gst_object_unref(bus);

    // Input thread
    pthread_t input_tid;
    pthread_create(&input_tid, nullptr, input_thread_func, nullptr);

    printf("Controls: w=up s=down a=left d=right space=fire q=quit\n");

    gst_element_set_state(pipeline, GST_STATE_PLAYING);

    main_loop = g_main_loop_new(nullptr, FALSE);
    g_main_loop_run(main_loop);

    // Cleanup
    is_running = false;
    gst_element_set_state(pipeline, GST_STATE_NULL);
    gst_object_unref(pipeline);
    g_main_loop_unref(main_loop);

    terminal_restore();
    pthread_join(world_tid, nullptr);

    delete renderer;
    delete world;
    printf("\nDone.\n");
}

#ifdef __APPLE__
#include <TargetConditionals.h>
#if TARGET_OS_MAC
// On macOS, GStreamer video sinks need NSApplication on the main thread.
// gst_macos_main() handles this by running our code in a block on the main thread.

static int macos_main_func(int argc, char **argv, gpointer user_data) {
    run_app(argc, argv);
    return 0;
}

int main(int argc, char *argv[]) {
    gst_init(&argc, &argv);
    return gst_macos_main(macos_main_func, argc, argv, nullptr);
}
#endif
#else
int main(int argc, char *argv[]) {
    gst_init(&argc, &argv);
    run_app(argc, argv);
    return 0;
}
#endif
