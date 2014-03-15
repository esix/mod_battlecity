/*
 * FreeSWITCH battle city module
 */


#include "stream-controller.h"
#include "world.h"


#include <pthread.h>
#include <unistd.h>
#include <memory>

#include <gst/gst.h>
#include <glib.h>
#include <switch.h>

SWITCH_MODULE_LOAD_FUNCTION(mod_battlecity_load);
SWITCH_MODULE_SHUTDOWN_FUNCTION(mod_battlecity_shutdown);
SWITCH_MODULE_DEFINITION(mod_battlecity, mod_battlecity_load, mod_battlecity_shutdown, NULL);



#define VID_BIT (1 << 31)


static World *world = NULL;
static Renderer *renderer = new Renderer();
volatile bool is_running = false;
pthread_t world_thread;

static void * world_thread_func(void *arg) {
	is_running = true;
	world = new World();
	while(is_running) {
		world->live();
		usleep(1000 * 40);
	}
	switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "Finished world thread\n");
	return NULL;
}





SWITCH_STANDARD_APP(play_battlecity_function)
{
	std::shared_ptr<Player> player = world->add_player();
	StreamController streamController(session, renderer, player);
	streamController.start();
	world->remove_player(player);
}

/* Registration */

SWITCH_MODULE_LOAD_FUNCTION(mod_battlecity_load)
{
	switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "mod_battlecity: starting load\n");

	gst_init (NULL, NULL);
	switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "mod_battlecity: gst_init done\n");

	int result = pthread_create(&world_thread, NULL, world_thread_func, NULL);
	switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "mod_battlecity: world thread created (result=%d)\n", result);

	switch_application_interface_t *app_interface;
	switch_file_interface_t *file_interface;


	/* connect my internal structure to the blank pointer passed to me */
	*module_interface = switch_loadable_module_create_module_interface(pool, modname);

	SWITCH_ADD_APP(app_interface, "play_battlecity", "play battlecity", "play battlecity", play_battlecity_function, "<file>", SAF_NONE);

	switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "mod_battlecity: load complete\n");

	/* indicate that the module should continue to be loaded */
	return SWITCH_STATUS_SUCCESS;
}

SWITCH_MODULE_SHUTDOWN_FUNCTION(mod_battlecity_shutdown)
{
	switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "shutdown\n");
	is_running = false;
	int result = pthread_join(world_thread, NULL);
	switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "shutdown - OK\n");

	return SWITCH_STATUS_SUCCESS;
}
