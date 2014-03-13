/*
 * Simplest standalone test: renders game frames and pipes to stdout as raw RGB.
 * View with:
 *   ./standalone_test_simple | ffplay -f rawvideo -pixel_format rgb24 -video_size 320x240 -framerate 15 -i -
 *
 * Build:
 *   g++ -std=c++11 -o standalone_test_simple standalone_test_simple.cpp renderer.cpp world.cpp \
 *       $(pkg-config --cflags --libs gstreamer-1.0) -lpthread
 */

#include <pthread.h>
#include <unistd.h>
#include <cstdio>
#include <cstring>
#include <memory>

#include "renderer.h"
#include "world.h"

static World *world = nullptr;
static Renderer *renderer = nullptr;
static volatile bool is_running = false;

static void *world_thread_func(void *arg) {
    is_running = true;
    world = new World();
    while (is_running) {
        world->live();
        usleep(1000 * 40);
    }
    return nullptr;
}

int main(int argc, char *argv[]) {
    gst_init(&argc, &argv);

    renderer = new Renderer();
    pthread_t world_tid;
    pthread_create(&world_tid, nullptr, world_thread_func, nullptr);
    while (!world) usleep(10000);

    std::shared_ptr<Player> player = world->add_player();

    // Render 150 frames (10 seconds at 15fps) to stdout
    for (int frame = 0; frame < 150 && is_running; frame++) {
        Canvas canvas;
        renderer->render_world(player, canvas);
        fwrite(canvas.get_data(), 1, canvas.get_data_size(), stdout);
        usleep(1000000 / 15); // ~66ms
    }

    is_running = false;
    pthread_join(world_tid, nullptr);
    delete renderer;
    delete world;

    fprintf(stderr, "Done: wrote 150 frames\n");
    return 0;
}
