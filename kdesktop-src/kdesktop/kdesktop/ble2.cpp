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

#define GETTEXT_DOMAIN "kdesktop-lib"

#include "ble2.hpp"

#include <time.h>
#include "gettext.hpp"
#include "help.hpp"
#include "filesystem.hpp"
#include "sound.hpp"
#include "wml_exception.hpp"

#include <iomanip>
#include <boost/bind.hpp>

#include <algorithm>

#include "base_instance.hpp"

enum {atyle_none, atype_playback, atype_apns};
static int alert_type = atype_playback;

#define MAX_RESERVE_TEMPS		6
#define RESERVE_FILE_DATA_LEN   (sizeof(ttemperature) * MAX_RESERVE_TEMPS)

#define THRESHOLD_HDERR_REF		5
#define RESISTANCE_OUTRANDE		UINT16_MAX

const char* tble2::uuid_my_service = "fd00";
const char* tble2::uuid_write_characteristic = "fd01";
const char* tble2::uuid_notify_characteristic = "fd03";

bool tble2::tether3elem::valid() const 
{
	return ipv4 != 0 && prefixlen >= 1 && prefixlen <= 31 && gateway != 0 && utils::is_same_net(ipv4, gateway, prefixlen);
}

bool tble2::is_discovery_name(const SDL_BlePeripheral& peripheral)
{
	if (game_config::os != os_windows) {
		if (peripheral.manufacturer_data_len < 2) {
			return false;
		}
		const uint16_t launcher_manufacturer_id_ = 65520; // 0xfff0 ==> (xmit)f0 ff
		if (peripheral.manufacturer_data[0] != posix_lo8(launcher_manufacturer_id_) || peripheral.manufacturer_data[1] != posix_hi8(launcher_manufacturer_id_)) {
			return false;
		}
	}

	const std::string lower_name = utils::lowercase(peripheral.name);
	if (lower_name.empty()) {
		return false;
	}
	// rdpd, rk3399(ios)
	if (game_config::os != os_ios) {
		return !strcmp(lower_name.c_str(), "rdpd");
	} else {
		return !strcmp(lower_name.c_str(), "rdpd") || !strcmp(lower_name.c_str(), "rk3399");
	}
}

void fill_interval_fields(uint8_t* data, int normal_interval, int fast_interval, int fast_perieod)
{
    data[0] = posix_lo8(normal_interval);
    data[1] = posix_hi8(normal_interval);
    data[2] = posix_lo8(fast_interval);
    data[3] = posix_hi8(fast_interval);
    data[4] = posix_lo8(fast_perieod);
    data[5] = posix_hi8(fast_perieod);
}

tble2::tble2(base_instance* instance)
	: tble(instance, connector_)
	, file_idle_len(1024 * 16)
	, receive_buf_(NULL)
	, receive_buf_size_(0)
	, plugin_(NULL)
{
	disable_reconnect_ = true;
	extend_receive_buf(file_idle_len, 0);

	uint8_t data[6];
	{
		ttask& task = insert_task(taskid_postready);
		// step0: notify read
		task.insert(nposm, uuid_my_service, uuid_notify_characteristic, option_notify);
	}

	{
		ttask& task = insert_task(taskid_queryip);
		// step0: set time
		task.insert(nposm, uuid_my_service, uuid_write_characteristic, option_write, data, 6);
	}

	{
		ttask& task = insert_task(taskid_updateip);
		// step0: set time
		task.insert(nposm, uuid_my_service, uuid_write_characteristic, option_write, data, 6);
	}
}

tble2::~tble2()
{
	// close_file();
	if (receive_buf_) {
		free(receive_buf_);
	}
}

void tble2::connect_to(const tmac_addr& mac_addr)
{
	mac_addr_ = mac_addr;
	connector_.clear();
}

void tble2::disconnect_disable_reconnect()
{
	mac_addr_.clear();
	connector_.clear();
	disconnect_peripheral();
}

void tble2::app_calculate_mac_addr(SDL_BlePeripheral& peripheral)
{
    if (peripheral.manufacturer_data && peripheral.manufacturer_data_len >= 8) {
        int start = 2;
        if (peripheral.manufacturer_data_len > 8) {
            unsigned char flag = peripheral.manufacturer_data[2];
            if (flag & 0x1) {
                start = 8;
            } else {
                start = 3;
            }
            
        }
        peripheral.mac_addr[0] = peripheral.manufacturer_data[start];
        peripheral.mac_addr[1] = peripheral.manufacturer_data[start + 1];
        peripheral.mac_addr[2] = peripheral.manufacturer_data[start + 2];
        peripheral.mac_addr[3] = peripheral.manufacturer_data[start + 3];
        peripheral.mac_addr[4] = peripheral.manufacturer_data[start + 4];
        peripheral.mac_addr[5] = peripheral.manufacturer_data[start + 5];
        
    } else {
        peripheral.mac_addr[0] = '\0';
    }
}

void tble2::app_discover_peripheral(SDL_BlePeripheral& peripheral)
{
    if (!connector_.valid() && !connecting_peripheral_ && !peripheral_  && mac_addr_equal(mac_addr_.mac_addr, peripheral.mac_addr) && is_discovery_name(peripheral)) {
        connect_peripheral(peripheral);
    }
	if (plugin_) {
		plugin_->did_discover_peripheral(peripheral);
	}
}

void tble2::app_release_peripheral(SDL_BlePeripheral& peripheral)
{
	if (plugin_) {
		plugin_->did_release_peripheral(peripheral);
	}
}

bool tble2::is_right_services()
{
	if (peripheral_->valid_services != 3) {
		return false;
	}
	const SDL_BleService* my_service = nullptr;
	for (int n = 0; n < peripheral_->valid_services; n ++) {
		const SDL_BleService& service = peripheral_->services[n];
		if (SDL_BleUuidEqual(service.uuid, uuid_my_service)) {
			my_service = &service;
		}
	}
	if (my_service == nullptr) {
		return false;
	}
	if (my_service->valid_characteristics != 3) {
		return false;
	}

	std::vector<std::string> chars;
	chars.push_back(uuid_write_characteristic);
	chars.push_back(uuid_notify_characteristic);
	for (std::vector<std::string>::const_iterator it = chars.begin(); it != chars.end(); ++ it) {
		int n = 0;
		for (; n < my_service->valid_characteristics; n ++) {
			const SDL_BleCharacteristic& char2 = my_service->characteristics[n];
			if (SDL_BleUuidEqual(char2.uuid, it->c_str())) {
				break;
			}
		}
		if (n == my_service->valid_characteristics) {
			return false;
		}
	}

	return true;
}

void tble2::app_connect_peripheral(SDL_BlePeripheral& peripheral, const int error)
{
	// handle_event(tfile_analysis::evt_connected, time(NULL), 0);

    if (!error) {
		// mac_addr_.set(peripheral.mac_addr, utils::cstr_2_str(peripheral.uuid));
		SDL_Log("tble2::app_connect_peripheral, will execute task#%i", taskid_postready);
		tble::ttask& task = get_task(taskid_postready);
		task.execute(*this);
    }

	if (plugin_) {
		plugin_->did_connect_peripheral(peripheral, error);
	}
}

void tble2::app_disconnect_peripheral(SDL_BlePeripheral& peripheral, const int error)
{
	if (plugin_) {
		plugin_->did_disconnect_peripheral(peripheral, error);
	}
}

void tble2::app_discover_characteristics(SDL_BlePeripheral& peripheral, SDL_BleService& service, const int error)
{
	if (plugin_) {
		plugin_->did_discover_characteristics(peripheral, service, error);
	}
}

void tble2::extend_receive_buf(int size, int vsize)
{
	if (size > receive_buf_size_) {
		char* tmp = (char*)malloc(size);
		if (receive_buf_) {
			if (vsize) {
				memcpy(tmp, receive_buf_, vsize);
			}
			free(receive_buf_);
		}
		receive_buf_ = tmp;
		receive_buf_size_ = size;
	}
}

void tble2::app_read_characteristic(SDL_BlePeripheral& peripheral, SDL_BleCharacteristic& characteristic, const unsigned char* data, int len)
{
	if (SDL_BleUuidEqual(characteristic.uuid, uuid_notify_characteristic) && plugin_ != nullptr) {
		// 0x07 <cmd> <context(N byte)>
		const uint8_t prefix_byte = 0x07;
		enum {cmd_queryip_req = 1, cmd_queryip_resp, cmd_updateip_req, cmd_updateip_resp};

		if (len == 7 && data[0] == prefix_byte && data[1] == cmd_queryip_resp) {
			uint32_t ipv4 = 0;
			memcpy(&ipv4, data + 2, 4);
			int prefixlen = data[6];
			plugin_->did_query_ip(peripheral, ipv4, prefixlen);

		} else if (len == 3 && data[0] == prefix_byte && data[1] == cmd_updateip_resp) {
			plugin_->did_update_ip(peripheral, data[2]);
		}
	}
}

void tble2::app_write_characteristic(SDL_BlePeripheral& peripheral, SDL_BleCharacteristic& characteristic, const int error)
{
	if (plugin_) {
		plugin_->did_write_characteristic(peripheral, characteristic, error);
	}
}

void tble2::app_notify_characteristic(SDL_BlePeripheral& peripheral, SDL_BleCharacteristic& characteristic, const int error)
{
	if (plugin_) {
		plugin_->did_notify_characteristic(peripheral, characteristic, error);
	}
}

bool tble2::app_task_callback(ttask& task, int step_at, bool start)
{
	SDL_Log("tble2::app_task_callback--- task#%i, step_at: %i, %s, current_step_: %i", task.id(), step_at, start? "prefix": "postfix", current_step_);

	if (start) {
		// 0x07 <cmd> <context(N byte)>
		const uint8_t prefix_byte = 0x07;
		enum {cmd_queryip_req = 1, cmd_queryip_resp, cmd_updateip_req, cmd_updateip_resp};

		if (task.id() == taskid_queryip) {
			if (step_at == 0) {
				tstep& step = task.get_step(0);
				uint8_t data[2];
				data[0] = prefix_byte;
				data[1] = cmd_queryip_req;
				step.set_data(data, sizeof(data));
			}
		} else if (task.id() == taskid_updateip) {
			if (step_at == 0) {
				tstep& step = task.get_step(0);

				uint8_t data[11];
				data[0] = 0x07;
				data[1] = 0x03;
				memcpy(data + 2, &ether3elem_.ipv4, 4);
				data[6] = ether3elem_.prefixlen;
				memcpy(data + 7, &ether3elem_.gateway, 4);

				step.set_data(data, sizeof(data));
			}
		}
	} else {
		if (task.id() == taskid_postready) {
			if (step_at == (int)(task.steps().size() - 1)) {
				if (plugin_ != nullptr) {
					plugin_->did_start_queryip(*peripheral_);
				}

				int taskid = ether3elem_.valid()? taskid_updateip: taskid_queryip;
				tble::ttask& task = get_task(taskid);
				task.execute(*this);
			}
		}
	}
	return true;
}




