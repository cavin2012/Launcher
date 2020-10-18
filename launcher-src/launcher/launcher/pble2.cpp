/* $Id: title_screen.cpp 48740 2011-03-05 10:01:34Z mordante $ */
/*
   Copyright (C) 2008 - 2011 by Mark de Wever <koraq@xs4all.nl>
   Part of the Battle for Wesnoth Project http://www.wesnoth.org/

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.
   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY.

   See the COPYING file for more details.
*/

#define GETTEXT_DOMAIN "launcher-lib"

#include "pble2.hpp"

#include <time.h>
#include "game_config.hpp"
// #include "game_preferences.hpp"
#include "gettext.hpp"
#include "help.hpp"
#include "filesystem.hpp"
#include <hero.hpp>
#include "sound.hpp"
#include "wml_exception.hpp"

#include <iomanip>
#include <boost/bind.hpp>

#include <algorithm>

#include "base_instance.hpp"
#include <kosapi/net.h>

const char* tpble2::uuid_my_service = "fd00";
const char* tpble2::uuid_write_characteristic = "fd01";
const char* tpble2::uuid_notify_characteristic = "fd03";

uint32_t getipv4_prefixlen(const std::string& iface, int* prefixlen)
{
	char result[128];
    const int maxBytes = sizeof(result);
	char msg[128];

	sprintf(msg, "interface getcfg %s", iface.c_str());
    kosNetSendMsg(msg, result, maxBytes);
	if (game_config::os == os_windows) {
		// 36:56:fb:9f:f8:a2 192.168.1.108 24 up broadcast running multicast
		// 00:00:00:00:00:00 0.0.0.0 0 down
		strcpy(result, "36:56:fb:9f:f8:a2 192.168.1.105 24 up broadcast running multicast");
	}
    std::vector<std::string> vstr = utils::split(result, ' ');
    uint32_t ipv4 = 0;
	// net::IPAddress
    if (vstr.size() >= 4) {
        ipv4 = utils::to_ipv4(vstr[1]);
		if (prefixlen != nullptr) {
			*prefixlen = utils::to_int(vstr[2]);
		}
    }
	return ipv4;
}

tpble2::tpble2(base_instance* instance)
	: tpble(instance)
{
}

tpble2::~tpble2()
{
}

void tpble2::app_read_characteristic(const std::string& characteristic, const unsigned char* data, int len)
{
	SDL_Log("tpble2::app_read_characteristic(%s)--- len: %i", characteristic.c_str(), len);
	if (SDL_BleUuidEqual(characteristic.c_str(), uuid_write_characteristic)) {
		// 0x07 <cmd> <context(N byte)>
		const uint8_t prefix_byte = 0x07;
		enum {cmd_queryip_req = 1, cmd_queryip_resp, cmd_updateip_req, cmd_updateip_resp};

		if (len == 2 && data[0] == prefix_byte && data[1] == cmd_queryip_req) {
			// 0x07 cmd_queryip_req
			uint32_t ip = instance->current_ip();
			int prefixlen = 0;
			uint32_t ip2 = getipv4_prefixlen("eth0", &prefixlen);
			if (ip != ip2) {
				prefixlen = 0;
			}
			uint8_t resp[7];
			resp[0] = prefix_byte;
			resp[1] = cmd_queryip_resp;
			memcpy(resp + 2, &ip, sizeof(uint32_t));
			resp[6] = prefixlen;

			SDL_Log("receive request: query ip");
			write_characteristic(uuid_notify_characteristic, resp, sizeof(resp));

		} else if (len == 11 && data[0] == prefix_byte && data[1] == cmd_updateip_req) {
			// 0    1                2,3,4,5  6         7,8,9,10
			// 0x07 cmd_updateip_req ip       prefixlen gateway
			uint32_t ipv4 = 0;
			memcpy(&ipv4, data + 2, 4);
			int prefixlen = data[6];
			uint32_t gateway = 0;
			memcpy(&gateway, data + 7, 4);
			SDL_Log("receive request: update ip, ipv4: 0x%08x/%i, gateway:0x%08x", ipv4, prefixlen, gateway);
			bool ret = instance->app_update_ip(ipv4, prefixlen, gateway);

			uint8_t resp[3];
			resp[0] = prefix_byte;
			resp[1] = cmd_updateip_resp;
			resp[2] = ret? 200: 50;
			write_characteristic(uuid_notify_characteristic, resp, sizeof(resp));
		}
	}
}

void tpble2::simulate_read_characteristic()
{
	VALIDATE(game_config::os == os_windows, null_str);

	uint8_t resp[2];
	resp[0] = 0x07;
	resp[1] = 0x01;

/*
	uint8_t resp[11];
	resp[0] = 0x07;
	resp[1] = 0x03;
	uint32_t ipv4 = 0x7401a8c0; // 192.168.1.116
	memcpy(resp + 2, &ipv4, 4);
	resp[6] = 24;
	uint32_t gateway = 0x0101a8c0; // 192.168.1.1
	memcpy(resp + 7, &gateway, 4);
*/
	app_read_characteristic(uuid_write_characteristic, resp, sizeof(resp));
}