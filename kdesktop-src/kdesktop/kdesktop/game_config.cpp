#define GETTEXT_DOMAIN "kdesktop-lib"

#include "game_config.hpp"
#include "wml_exception.hpp"

namespace game_config {
version_info min_launcher_ver;
std::map<int, std::string> screen_modes;
}

namespace preferences {

int mainbarx()
{
	int val = preferences::get_int("mainbarx", 0);
	if (val <= 0) {
		val = 0;
	}
	return val;
}

void set_mainbarx(int value)
{
	VALIDATE(value >= 0, null_str);
	if (mainbarx() != value) {
		preferences::set_int("mainbarx", value);
		preferences::write_preferences();
	}
}

int minimapbarx()
{
	int val = preferences::get_int("minimapbarx", 0);
	if (val <= 0) {
		val = 0;
	}
	return val;
}

void set_minimapbarx(int value)
{
	VALIDATE(value >= 0, null_str);
	if (minimapbarx() != value) {
		preferences::set_int("minimapbarx", value);
		preferences::write_preferences();
	}
}

int screenmode()
{
	int val = preferences::get_int("screenmode", DEFAULT_SCREEN_MODE);
	if (val < 0 || val >= (int)game_config::screen_modes.size()) {
		val = DEFAULT_SCREEN_MODE;
	}
	return val;
}

void set_screenmode(int value)
{
	VALIDATE(value >= 0 && (int)game_config::screen_modes.size(), null_str);
	if (minimapbarx() != value) {
		preferences::set_int("screenmode", value);
		preferences::write_preferences();
	}
}

int visiblepercent()
{
	int val = preferences::get_int("visiblepercent", DEFAULT_VISIBLE_PERCENT);
	if (val < MIN_VISIBLE_PERCENT || val > MAX_VISIBLE_PERCENT) {
		val = DEFAULT_VISIBLE_PERCENT;
	}
	return val;
}

void set_visiblepercent(int value)
{
	VALIDATE(value >= MIN_VISIBLE_PERCENT && value <= MAX_VISIBLE_PERCENT, null_str);
	if (visiblepercent() != value) {
		preferences::set_int("visiblepercent", value);
		preferences::write_preferences();
	}
}

bool ratioswitchable()
{
	return preferences::get_bool("ratioswitchable", false);
}

void set_ratioswitchable(bool value)
{
	if (ratioswitchable() != value) {
		preferences::set_bool("ratioswitchable", value);
		preferences::write_preferences();
	}
}

std::string currentremote()
{
	std::string value =  preferences::get_str("currentremote");
	if (utils::to_ipv4(value) == 0) {
		value.clear();
	}
	return value;
}

void set_currentremote(const std::string& value)
{
	if (currentremote() != value) {
		preferences::set_str("currentremote", value);
		preferences::write_preferences();
	}
}

}


