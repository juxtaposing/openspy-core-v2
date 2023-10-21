#include <stdio.h>
#include <map>
#include <string>
#include <sstream>
#include <OS/Net/NetServer.h>
#include "server/CDKeyServer.h"
#include "server/CDKeyDriver.h"

INetServer *g_gameserver = NULL;

void tick_handler(uv_timer_t* handle) {
	g_gameserver->tick();
}

int main() {
	uv_loop_t *loop = uv_default_loop();
	uv_timer_t tick_timer;

	uv_timer_init(uv_default_loop(), &tick_timer);
    uv_timer_start(&tick_timer, tick_handler, 0, 250);

	OS::Init("cdkey");
	g_gameserver = new CDKey::Server();

	char address_buff[256];
	char port_buff[16];
	size_t temp_env_sz = sizeof(address_buff);

	if(uv_os_getenv("OPENSPY_CDKEY_BIND_ADDR", (char *)&address_buff, &temp_env_sz) != UV_ENOENT) {
		temp_env_sz = sizeof(port_buff);

		uint16_t port = 27901;
		if(uv_os_getenv("OPENSPY_CDKEY_BIND_PORT", (char *)&port_buff, &temp_env_sz) != UV_ENOENT) {
			port = atoi(port_buff);
		}

		CDKey::Driver *driver = new CDKey::Driver(g_gameserver, address_buff, port);

		OS::LogText(OS::ELogLevel_Info, "Adding CDKey Driver: %s:%d\n", address_buff, port);
		g_gameserver->addNetworkDriver(driver);
	} else {
		OS::LogText(OS::ELogLevel_Warning, "Missing CDKey bind address environment variable");
	}

    uv_run(loop, UV_RUN_DEFAULT);

    uv_loop_close(loop);

    delete g_gameserver;

    OS::Shutdown();
    return 0;
}