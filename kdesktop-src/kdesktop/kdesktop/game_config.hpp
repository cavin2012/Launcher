#ifndef GAME_CONFIG_HPP_INCLUDED
#define GAME_CONFIG_HPP_INCLUDED

#include "preferences.hpp"
#include "sdl_utils.hpp"
#include "version.hpp"

enum {screenmode_scale, screenmode_min = screenmode_scale, screenmode_ratio, screenmode_partial, screenmode_count};
#define DEFAULT_SCREEN_MODE		screenmode_scale
#define MIN_VISIBLE_PERCENT		20
#define MAX_VISIBLE_PERCENT		99
#define DEFAULT_VISIBLE_PERCENT	95

namespace game_config {
extern version_info min_launcher_ver;
extern std::map<int, std::string> screen_modes;
}

struct trdpcookie
{
	trdpcookie()
	{
		clear();
	}

	bool valid() const { return ipv4 != 0; }

	void clear()
	{
		ipv4 = 0;
	}

	uint32_t ipv4;
};

struct tpbdevice
{
	tpbdevice(const std::string& uuid, const std::string& name, uint32_t ip, int64_t last_access)
		: uuid(utils::lowercase(uuid))
		, name(name)
		, ip(ip)
		, last_access(last_access)
	{}

	bool operator<(const tpbdevice& that) const 
	{
		int cmp = strcmp(name.c_str(), that.name.c_str());
		return cmp < 0;
	}

	std::string uuid;
	std::string name;
	uint32_t ip;
	int64_t last_access;
};

struct tpbgroup
{
	tpbgroup(const std::string& uuid, const std::string& name, const std::set<tpbdevice>& devices)
		: uuid(utils::lowercase(uuid))
		, name(name)
		, devices(devices)
	{}

	bool operator<(const tpbgroup& that) const 
	{
		int cmp = strcmp(name.c_str(), that.name.c_str());
		return cmp < 0;
	}

	std::string uuid;
	std::string name;
	std::set<tpbdevice> devices;
};

struct tpbremotes
{
	int version;
	int64_t timestamp;
	std::set<tpbgroup> groups;
};

namespace preferences {

int mainbarx();
void set_mainbarx(int value);

int minimapbarx();
void set_minimapbarx(int value);

int screenmode();
void set_screenmode(int value);

int visiblepercent();
void set_visiblepercent(int value);

bool ratioswitchable();
void set_ratioswitchable(bool value);

std::string currentremote();
void set_currentremote(const std::string& value);

}

#endif

