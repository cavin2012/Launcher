#define GETTEXT_DOMAIN "kdesktop-lib"

#include "gui/dialogs/home.hpp"

#include "gui/widgets/label.hpp"
#include "gui/widgets/button.hpp"
#include "gui/widgets/toggle_button.hpp"
#include "gui/widgets/text_box.hpp"
#include "gui/widgets/listbox.hpp"
#include "gui/widgets/window.hpp"
#include "gui/dialogs/menu.hpp"
#include "gettext.hpp"
#include "version.hpp"
#include "formula_string_utils.hpp"

#include <boost/bind.hpp>
#include <net/base/ip_endpoint.h>

namespace gui2 {

REGISTER_DIALOG(kdesktop, home)

thome::thome(tpbremotes& pbremotes)
	: pbremotes_(pbremotes)
{
}

void thome::pre_show()
{
	window_->set_escape_disabled(true);
	window_->set_label("misc/white-background.png");

	std::stringstream ss;
	utils::string_map symbols;

	ss.str("");
	ss << game_config::get_app_msgstr(null_str);
	find_widget<tlabel>(window_, "title", false).set_label(ss.str());

	ss.str("");
	ss << " V" << game_config::version.str(true);
	find_widget<tlabel>(window_, "version", false).set_label(ss.str());

	tbutton* button = find_widget<tbutton>(window_, "scan", false, true);
	connect_signal_mouse_left_click(
				*button
			, boost::bind(
			&thome::click_scan
			, this, boost::ref(*button)));

	const int max_ipv4_str_chars = 3 + 1 + 3 + 1 + 3 + 1 + 3;
	// ip addr
	ipaddr_ = find_widget<ttext_box>(window_, "ipaddr", false, true);
	ipaddr_->set_did_text_changed(boost::bind(&thome::did_text_box_changed, this, _1));
	ipaddr_->set_maximum_chars(max_ipv4_str_chars);
	ipaddr_->set_placeholder("192.168.1.109");
	ipaddr_->set_label(preferences::currentremote());

	button = find_widget<tbutton>(window_, "desktop", false, true);
	connect_signal_mouse_left_click(
				*button
			, boost::bind(
			&thome::click_rdp
			, this, boost::ref(*button)));
	button->set_active(can_rdp());

	ttoggle_button* toggle = find_widget<ttoggle_button>(window_, "ratio_switchable", false, true);
	toggle->set_value(preferences::ratioswitchable());
	toggle->set_did_state_changed(boost::bind(&thome::did_ratio_switchable_changed, this, _1));
	symbols.clear();
	symbols["item"] = game_config::screen_modes.find(screenmode_ratio)->second;
	toggle->set_label(vgettext2("When switching mode, can select '$item'", symbols));

	// reload_devices(pbremotes_);
}

void thome::post_show()
{
}

void thome::reload_devices(tpbremotes& pbremotes)
{
	tlistbox* list = find_widget<tlistbox>(window_, "devices", false, true);

	std::map<std::string, std::string> data;
	for (std::set<tpbgroup>::const_iterator it = pbremotes.groups.begin(); it != pbremotes.groups.end(); ++ it) {
		const tpbgroup& group = *it;

		data.clear();
		data.insert(std::make_pair("name", group.name));
		data.insert(std::make_pair("description", group.uuid));
		list->insert_row(data);
		for (std::set<tpbdevice>::const_iterator it2 = group.devices.begin(); it2 != group.devices.end(); ++ it2) {
			const tpbdevice& device = *it2;
			data.clear();
			data.insert(std::make_pair("name", device.name));
			data.insert(std::make_pair("description", device.uuid));
			list->insert_row(data);
		}
	}
}

void thome::click_rdp(tbutton& widget)
{
	VALIDATE(can_rdp(), null_str);
	net::IPAddress address((const uint8_t*)&rdpcookie_.ipv4, 4);
	preferences::set_currentremote(address.ToString());
	window_->set_retval(DESKTOP);
}

void thome::click_scan(tbutton& widget)
{
	window_->set_retval(SCAN);
}

void thome::did_ratio_switchable_changed(ttoggle_button& widget)
{
	preferences::set_ratioswitchable(widget.get_value());
}

bool thome::can_rdp() const
{
	return rdpcookie_.valid();
}

void thome::did_text_box_changed(ttext_box& widget)
{
	rdpcookie_.ipv4 = utils::to_ipv4(widget.label());

	tbutton* button = find_widget<tbutton>(window_, "desktop", false, true);
	button->set_active(can_rdp());
}

} // namespace gui2

