#define GETTEXT_DOMAIN "launcher-lib"

#include "global.hpp"
#include "game_config.hpp"
#include "version.hpp"

#include <sstream>
#include <iomanip>
#include <boost/bind.hpp>

namespace game_config {

int max_fps_to_encoder = 25;
std::map<int, std::string> suppress_thresholds;
version_info kosapi_ver;

}

namespace preferences {

std::string sn()
{
	std::string str = preferences::get_str("sn");
	if (str.empty()) {
		str = DEFAULT_SN;
	}
	return str;
}

void set_sn(const std::string& value)
{
	VALIDATE(!value.empty(), null_str);
	if (sn() != value) {
		preferences::set_str("sn", value);
		preferences::write_preferences();
	}
}

bool bleperipheral()
{
	return preferences::get_bool("bleperipheral", true);
}

void set_bleperipheral(bool value)
{
	if (bleperipheral() != value) {
		preferences::set_bool("bleperipheral", value);
		preferences::write_preferences();
	}
}

}