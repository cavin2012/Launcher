#define GETTEXT_DOMAIN "kdesktop-lib"

#include "gui/dialogs/scan.hpp"

#include "gui/widgets/label.hpp"
#include "gui/widgets/button.hpp"
#include "gui/widgets/listbox.hpp"
#include "gui/widgets/stack.hpp"
#include "gui/widgets/window.hpp"
#include "gui/dialogs/ethernetip.hpp"
#include "gettext.hpp"
#include "rose_config.hpp"
#include "version.hpp"

#include "formula_string_utils.hpp"

#include <boost/bind.hpp>

namespace gui2 {

REGISTER_DIALOG(kdesktop, scan)

tscan::tdiscover::tdiscover(const tble2& ble, SDL_BlePeripheral* peripheral, const tmac_addr& _mac_addr)
	: peripheral(peripheral)
	, queryip_status(queryip_no)
	, prefixlen(nposm)
	, at(nposm)
{
	mac_addr = _mac_addr;
}

tscan::tscan(tble2& ble)
	: ble_(ble)
	, list_widget_(nullptr)
	, scan_stack_(nullptr)
	, scan_desc_widget_(nullptr)
	, ip_id_("ip")
	, rssi_id_("rssi")
	, scaning_(false)
	, queryip_discover_(nullptr)
	, stop_scan_ticks_(0)
	, queryip_halt_ticks_(0)
	, updateip_halt_ticks_(0)
	, closing_(false)

{
	set_timer_interval(800);
}

void tscan::pre_show()
{
	window_->set_escape_disabled(true);
	window_->set_label("misc/white-background.png");

	utils::string_map symbols;

	std::stringstream ss;
	ss.str("");
	ss << game_config::get_app_msgstr(null_str) << " V" << game_config::version.str(true);
	find_widget<tlabel>(window_, "title", false).set_label(ss.str());

	ss.str("");
	symbols["updateip"] = _("Update IP");
	ss << vgettext2("scan help $updateip", symbols);
	find_widget<tlabel>(window_, "help", false).set_label(ss.str());

	tlistbox* list = find_widget<tlistbox>(window_, "devices", false, true);
	list->enable_select(false);
	list_widget_ = list;

	scan_stack_ = find_widget<tstack>(window_, "scan_stack", false, true);
	pre_scan_label(*scan_stack_->layer(LABEL_SCAN_LAYER));
	pre_scan_button(*scan_stack_->layer(BUTTON_SCAN_LAYER));

	ble_.set_plugin(this);
	start_scan_internal();
}

void tscan::post_show()
{
	SDL_Log("tscan::post_show---");
	closing_ = true;
	if (queryip_discover_ != nullptr) {
		std::string text;
		const tdiscover& discover = *queryip_discover_;
		bool querying = queryip_halt_ticks_ != 0;
		stop_action_internal(querying);

		// disconnect_and_rescan();
		ble_.disconnect_disable_reconnect();
		return;
	}

	if (stop_scan_ticks_ != 0) {
		stop_scan_internal();
	}
	SDL_Log("---tscan::post_show X");
}

void tscan::pre_scan_label(tgrid& grid)
{
	scan_desc_widget_ = find_widget<tlabel>(&grid, "scan_description", false, true);
}

void tscan::pre_scan_button(tgrid& grid)
{
	tbutton* button = find_widget<tbutton>(&grid, "scan", false, true);
	connect_signal_mouse_left_click(
			  *button
			, boost::bind(
			&tscan::click_scan
			, this, boost::ref(*button)));
	button->set_label(_("Scan again"));
}

std::string tscan::queryip_str(const tdiscover& discover, bool query) const
{
	std::stringstream ss;
	if (query) {
		ss << _("Query IP");
	} else {
		ss << discover.ip.ToString() << "=>" << discover.update_to_ip.ToString();
	}
	ss << " ";
	if (discover.queryip_status == queryip_no) {
		ss << "[1/3]" << _("Connecting");

	} else if (discover.queryip_status == queryip_request) {
		ss << "[2/3]" << _("Send request");

	} else if (discover.queryip_status == queryip_response) {
		ss << "[3/3]" << _("Waiting response");

	} else if (discover.queryip_status == queryip_ok) {
		ss.str("");
		if (query) {
			ss << discover.ip.ToString() << "/" << utils::from_prefixlen(discover.prefixlen);
		} else {
			ss << discover.ip.ToString() << "=>" << discover.update_to_ip.ToString() << (" OK");
		}

	} else if (discover.queryip_status == queryip_connectfail) {
		ss << _("Connect fail");

	} else if (discover.queryip_status == queryip_getservicesfail) {
		ss << _("Get serviceDB fail");

	} else if (discover.queryip_status == queryip_errorservices) {
		ss << _("ServiceDB error");

	} else if (discover.queryip_status == queryip_unknownfail) {
		ss << _("Unknown error");

	} else {
		VALIDATE(discover.queryip_status == queryip_invalidsn, null_str);
		ss << _("Unknown device, don't query");
	}

	return ss.str();
}

void tscan::set_discover_row_ip_label(const tdiscover& discover, bool query)
{
	set_discover_row_label(discover, ip_id_, queryip_str(discover, query));
}

void tscan::set_discover_row_label(const tdiscover& discover, const std::string& id, const std::string& label)
{
	VALIDATE(discover.at != nposm && discover.at < list_widget_->rows(), null_str);

	ttoggle_panel& row = list_widget_->row_panel(discover.at);
	row.set_child_label(id, label);
}

static bool is_valid_sn_char(uint8_t ch)
{
	if ((ch & 0x80) || ch <= 0x20 || ch == 0x7f) {
		return false;
	}
	return true;
}

std::string extract_sn(const uint8_t* data, int len)
{
	if (len != 0) {
		VALIDATE(data != nullptr, null_str);
	}
	if (len < 3) {
		return null_str;
	}
	for (int at = 2; at < len; at ++) {
		uint8_t ch = data[at];
		if (!is_valid_sn_char(ch)) {
			return null_str;
		}
	}
	return std::string((const char*)(data + 2), len - 2);
}

void tscan::did_discover_peripheral(SDL_BlePeripheral& peripheral)
{
	if (closing_) {
		return;
	}

	if (!peripheral.name || peripheral.name[0] == '\0') {
		return;
	}
	if (!tble2::is_discovery_name(peripheral)) {
		return;
	}
	if (queryip_discover_ != nullptr) {
		// now one discover is querying ip, don't cosider other peripheral.
		return;
	}

	tdiscover discover(ble_, &peripheral, tmac_addr(peripheral.mac_addr, utils::cstr_2_str(peripheral.uuid)));
    if (find_discover(peripheral) != nullptr) {
		return;
	}
	discover.sn = extract_sn(discover.peripheral->manufacturer_data, discover.peripheral->manufacturer_data_len);
	if (!discover.sn.empty()) {
		// discover.queryip_status = queryip_no;
	} else {
		bool use_fakesn = game_config::os == os_windows;
		if (use_fakesn) {
			discover.sn = "fack-sn";
		} else {
			discover.queryip_status = queryip_invalidsn;
		}
	}

	// current not moniter this peripheral, put it to non-moniter.
	discovers_.push_back(discover);
	reload_discovers(*list_widget_);

	// if ((game_config::os != os_windows || scaning_) && !discover.sn.empty()) {
	if (scaning_ && !discover.sn.empty()) {
		// if scanning, query ip. else do nothing.
		query_ip(discovers_.back());
	}
}

void tscan::did_connect_peripheral(SDL_BlePeripheral& peripheral, const int error)
{
	tdiscover* discover = find_discover(peripheral);
	if (discover == nullptr) {
		return;
	}
	SDL_Log("tscan::did_connect_peripheral--- error: %i", error);
	if (error == tble::bleerr_ok) {
		discover->queryip_status = queryip_request;
	} else if (error == tble::bleerr_connect) {
		discover->queryip_status = queryip_connectfail;

	} else if (error == tble::bleerr_getservices) {
		discover->queryip_status = queryip_getservicesfail;

	} else if (error == tble::bleerr_errorservices) {
		discover->queryip_status = queryip_errorservices;

	} else {
		discover->queryip_status = queryip_unknownfail;
	}
	set_discover_row_ip_label(*discover, queryip_halt_ticks_ != 0);
}

void tscan::did_disconnect_peripheral(SDL_BlePeripheral& peripheral, const int error)
{
	if (queryip_discover_ != nullptr) {
		advance_end_queryip(SDL_GetTicks());
	}
	tdiscover* discover = find_discover(peripheral);
	if (discover == nullptr) {
		return;
	}



	SDL_Log("tscan::did_disconnect_peripheral--- error: %i, queryip_status: %i", error, discover->queryip_status);
	if (discover->queryip_status == queryip_ok) {
		return;
	}

	// Connection may be unexpectedly disconnected while sending request and waiting for response.
	if (scaning_) {
		SDL_Log("tscan::did_disconnect_peripheral, exceptly disconnect, queryip_status: %i, rescan", 
			discover->queryip_status);
		ble_.start_scan();
	}
}

void tscan::did_start_queryip(SDL_BlePeripheral& peripheral)
{
	tdiscover* discover = find_discover(peripheral);
	if (discover == nullptr) {
		return;
	}

	discover->queryip_status = queryip_response;
}

void tscan::advance_end_queryip(uint32_t now)
{
	if (queryip_halt_ticks_ != 0) {
		VALIDATE(updateip_halt_ticks_ == 0, null_str);
		if (now < queryip_halt_ticks_) {
			stop_scan_ticks_ -= queryip_halt_ticks_ - now;
		}
		queryip_halt_ticks_ = 0;
	} else {
		VALIDATE(updateip_halt_ticks_ != 0, null_str);
		updateip_halt_ticks_ = 0;
		// has end update ip, recover "updateip" button.
		update_ip_stopped_ui();
	}
	queryip_discover_ = nullptr;
}

void tscan::did_query_ip(SDL_BlePeripheral& peripheral, uint32_t ipv4, int prefixlen)
{
	SDL_Log("tscan::did_query_ip--- ipv4: 0x%08x", ipv4);

	if (queryip_discover_ != nullptr) {
		advance_end_queryip(SDL_GetTicks());
	}
	tdiscover* discover = find_discover(peripheral);
	if (discover == nullptr) {
		return;
	}
	discover->queryip_status = queryip_ok;
	discover->ip = net::IPAddress((const uint8_t*)&ipv4, 4);
	discover->prefixlen = prefixlen;

	set_discover_row_ip_label(*discover, true);

	disconnect_and_rescan();
	SDL_Log("---tscan::did_query_ip X");
}

void tscan::did_update_ip(SDL_BlePeripheral& peripheral, int resp_code)
{
	SDL_Log("tscan::did_update_ip--- resp: %i", resp_code);
	if (queryip_discover_ != nullptr) {
		advance_end_queryip(SDL_GetTicks());
	}
	tdiscover* discover = find_discover(peripheral);
	if (discover == nullptr) {
		return;
	}
	discover->queryip_status = queryip_ok;
	
	set_discover_row_ip_label(*discover, false);
	// update new ip/prefixl to this discover.
	discover->ip = net::IPAddress((const uint8_t*)&(ble_.ether3elem().ipv4), 4);
	discover->prefixlen = ble_.ether3elem().prefixlen;

	disconnect_and_rescan();
	SDL_Log("---tscan::did_update_ip X");

}

void tscan::disconnect_and_rescan()
{
	SDL_Log("disconnect_and_rescan");
	ble_.disconnect_disable_reconnect();
	if (scaning_) {
		ble_.start_scan();
	}
	SDL_Log("---tscan::disconnect_and_rescan X");
}

tscan::tdiscover* tscan::find_discover(const SDL_BlePeripheral& peripheral)
{
	int at = 0;
	for (std::vector<tdiscover>::iterator it = discovers_.begin(); it != discovers_.end(); it ++, at ++) {
		tdiscover& discover = *it;
		if (discover.peripheral == &peripheral) {
			return &discover;
		}
	}
	return nullptr;
}

void tscan::query_ip(tdiscover& discover)
{
	VALIDATE(discover.queryip_status == queryip_no, null_str);
	VALIDATE(scaning_, null_str);
	VALIDATE(queryip_discover_ == nullptr, null_str);
	VALIDATE(queryip_halt_ticks_ == 0 && updateip_halt_ticks_ == 0, null_str);

	const int queryip_threshold = game_config::os != os_windows? 25000: 2000; // 25 second
	queryip_halt_ticks_ = SDL_GetTicks() + queryip_threshold;
	queryip_discover_ = &discover;

	VALIDATE(stop_scan_ticks_ != 0, null_str);
	stop_scan_ticks_ += queryip_threshold;
	// make ensure stop scan must be after query ip end.
	stop_scan_ticks_ = SDL_max(stop_scan_ticks_, queryip_halt_ticks_ + 1000);

	ble_.set_ether3elem(tble2::tether3elem());
	ble_.connect_peripheral(*discover.peripheral);
/*
	// Although onece connected, ble.cpp will stop_scan. But here's call ble_.stop_scan() still.
	// Stop scanning immediately and enter connecting peripheral.
	ble_.stop_scan();

	ble_.connect_to(discover.mac_addr);
	ble_.start_scan();
*/
}

void tscan::update_ip(tdiscover& discover, const tble2::tether3elem& ether3elem)
{
	// VALIDATE(discover.queryip_status == queryip_ok, null_str);
	VALIDATE(!scaning_, null_str);
	VALIDATE(queryip_discover_ == nullptr, null_str);
	VALIDATE(queryip_halt_ticks_ == 0 && updateip_halt_ticks_ == 0, null_str);
	VALIDATE(stop_scan_ticks_ == 0, null_str);

	discover.update_to_ip = net::IPAddress((const uint8_t*)&ether3elem.ipv4, 4);
	discover.queryip_status = queryip_no;
	set_discover_row_ip_label(discover, false);

	const int updateip_threshold = game_config::os != os_windows? 25000: 5000; // 25 second
	updateip_halt_ticks_ = SDL_GetTicks() + updateip_threshold;
	queryip_discover_ = &discover;

	ble_.set_ether3elem(ether3elem);
	ble_.connect_peripheral(*discover.peripheral);
}

void tscan::reload_discovers(tlistbox& list)
{
	list.clear();

	std::stringstream ss;
	std::map<std::string, std::string> data;
	int at = 0;
	for (std::vector<tdiscover>::iterator it = discovers_.begin(); it != discovers_.end(); it ++, at ++) {
		data.clear();

		tdiscover& discover = *it;
        VALIDATE(discover.peripheral != nullptr, null_str);
		data.insert(std::make_pair("name", discover.peripheral->name));
		ss.str("");
		ss << (discover.sn.empty()? _("Invalid SN"): discover.sn);
		ss << "(" << discover.mac_addr.str() << ")";
		data.insert(std::make_pair("macaddr", ss.str()));
		
		if (discover.peripheral->manufacturer_data_len > 2) {
		}
		data.insert(std::make_pair("rssi", str_cast(discover.peripheral->rssi)));
		data.insert(std::make_pair("ip", queryip_str(discover, true)));

		ttoggle_panel& row = list.insert_row(data);

		tbutton* button = find_widget<tbutton>(&row, "updateip", false, true);
		connect_signal_mouse_left_click(
				  *button
				, boost::bind(
				&tscan::click_updateip
				, this, boost::ref(row), boost::ref(*button)));
		button->set_label(_("Update IP"));
		button->set_visible(twidget::INVISIBLE);

		button = find_widget<tbutton>(&row, "desktop", false, true);
		connect_signal_mouse_left_click(
				  *button
				, boost::bind(
				&tscan::click_rdp
				, this, boost::ref(row), boost::ref(*button)));
		button->set_label(_("Remote desktop"));
		button->set_visible(twidget::INVISIBLE);

		discover.at = row.at();
	}
}

void tscan::visible_updateip_buttons(tlistbox& list, bool visible)
{
	VALIDATE(!scaning_, null_str);
	VALIDATE(stop_scan_ticks_ == 0, null_str);
	// VALIDATE(queryip_discover_ == nullptr, null_str);
	VALIDATE(queryip_halt_ticks_ == 0 && updateip_halt_ticks_ == 0, null_str);

	int rows = list.rows();
	VALIDATE(rows == discovers_.size(), null_str);
	for (int at = 0; at < rows; at ++) {
		const tdiscover& discover = discovers_[at];
		ttoggle_panel& row = list.row_panel(at);
		tbutton* upateip_widget = find_widget<tbutton>(&row, "updateip", false, true);
		tbutton* desktop_widget = find_widget<tbutton>(&row, "desktop", false, true);
		if (visible) {
			VALIDATE(upateip_widget->get_visible() == twidget::INVISIBLE, null_str);
			VALIDATE(desktop_widget->get_visible() == twidget::INVISIBLE, null_str);
			if (discover.ip.IsIPv4() || game_config::os == os_windows) {
				upateip_widget->set_visible(twidget::VISIBLE);
				// desktop_widget->set_visible(twidget::VISIBLE);
			}
		} else {
			upateip_widget->set_visible(twidget::INVISIBLE);
			// desktop_widget->set_visible(twidget::INVISIBLE);
		}
	}
}

void tscan::start_scan_internal()
{
	VALIDATE(!scaning_, null_str);
	VALIDATE(stop_scan_ticks_ == 0, null_str);
	scaning_ = !scaning_;

	uint32_t now = SDL_GetTicks();
	// const int scan_threshold = game_config::os != os_windows? 15000: 5000; // 20 second
	int scan_threshold = game_config::os != os_windows? 15000: 2000/*5000*/; // 20 second
/*
	if (game_config::version.revision_level() == 1) {
		scan_threshold = 24 * 3600 * 1000;
	}
*/
	stop_scan_ticks_ = now + scan_threshold;
	set_scan_description_label(now);

	discovers_.clear();
	list_widget_->clear();
	ble_.start_scan();
}

void tscan::stop_scan_internal()
{
	VALIDATE(stop_scan_ticks_ != 0, null_str);
	VALIDATE(scaning_, null_str);

	scaning_ = !scaning_;
	stop_scan_ticks_ = 0;
	// tble::tdisable_reconnect_lock lock(ble_, true);
	if (queryip_discover_ != nullptr) {
		queryip_discover_ = nullptr;
	}
	ble_.stop_scan();
	ble_.disconnect_disable_reconnect();
	scan_stack_->set_radio_layer(BUTTON_SCAN_LAYER);
	visible_updateip_buttons(*list_widget_, true);
}

void tscan::stop_action_internal(bool querying)
{
	VALIDATE(queryip_discover_ != nullptr, null_str);
	queryip_discover_ = nullptr;
	if (querying) {
		VALIDATE(updateip_halt_ticks_ == 0, null_str);
		queryip_halt_ticks_ = 0;
	} else {
		VALIDATE(queryip_halt_ticks_ == 0, null_str);
		updateip_halt_ticks_ = 0;
		// has end update ip, recover "updateip" button.
		update_ip_stopped_ui();
	}
}

void tscan::click_scan(tbutton& widget)
{
	start_scan_internal();

	scan_stack_->set_radio_layer(LABEL_SCAN_LAYER);
}

void tscan::click_updateip(ttoggle_panel& row, tbutton& widget)
{
	tble2::tether3elem ether3elem; 
	{
		gui2::tethernetip dlg(ble_, ether3elem);
		dlg.show();
		if (dlg.get_retval() != twindow::OK) {
			return;
		}
	}

	VALIDATE(ether3elem.valid(), null_str);
	visible_updateip_buttons(*list_widget_, false);

	update_ip(discovers_[row.at()], ether3elem);

	set_scan_description_label(SDL_GetTicks());
	scan_stack_->set_radio_layer(LABEL_SCAN_LAYER);
}

void tscan::click_rdp(ttoggle_panel& row, tbutton& widget)
{
	window_->set_retval(DESKTOP);
}

void tscan::update_ip_stopped_ui()
{
	visible_updateip_buttons(*list_widget_, true);
	scan_stack_->set_radio_layer(BUTTON_SCAN_LAYER);
}

void tscan::set_scan_description_label(uint32_t now)
{
	utils::string_map symbols;
	if (stop_scan_ticks_ != 0) {
		VALIDATE(stop_scan_ticks_ > now, null_str);
		int elapse = stop_scan_ticks_ - now;
		symbols["elapse"] = format_elapse_hms(elapse / 1000);
		std::string msg = vgettext2("Scan will stop after $elapse", symbols);
		scan_desc_widget_->set_label(msg);

	} else {
		VALIDATE(queryip_discover_ != nullptr, null_str);
		const tdiscover& discover = *queryip_discover_;
		symbols["src"] = discover.ip.ToString();
		symbols["dst"] = discover.update_to_ip.ToString();
		std::string msg = vgettext2("Updating IP, from $src to $dst", symbols);
		scan_desc_widget_->set_label(msg);
	}
}

void tscan::app_timer_handler(uint32_t now)
{
	if (queryip_discover_ != nullptr) {
		std::string text;
		const tdiscover& discover = *queryip_discover_;
		uint32_t halt_ticks = queryip_halt_ticks_;
		bool querying = true;
		if (halt_ticks == 0) {
			halt_ticks = updateip_halt_ticks_;
			querying = false;
		} else {
			VALIDATE(updateip_halt_ticks_ == 0, null_str);
		}
		VALIDATE(halt_ticks > 0, null_str);
		if (now >= halt_ticks) {
			stop_action_internal(querying);
/*
			queryip_discover_ = nullptr;
			if (querying) {
				queryip_halt_ticks_ = 0;
			} else {
				updateip_halt_ticks_ = 0;
				// has end update ip, recover "updateip" button.
				update_ip_stopped_ui();
			}
*/
			disconnect_and_rescan();
			if (querying) {
				text = _("Can not get IP within limited time");
			} else {
				text = _("Can not update IP within limited time");
			}
			
		} else {
			uint32_t resi = (halt_ticks - now) / 1000;
			text = str_cast(resi);
		}
		set_discover_row_label(discover, rssi_id_, text);
	}

	if (stop_scan_ticks_ != 0) {
		// When querying IP, don't elapse scan ticks.
		VALIDATE(scaning_, null_str);
		if (now < stop_scan_ticks_) {
			set_scan_description_label(now);

		} else {
			stop_scan_internal();
/*
			scaning_ = !scaning_;
			stop_scan_ticks_ = 0;
			// tble::tdisable_reconnect_lock lock(ble_, true);
			if (queryip_discover_ != nullptr) {
				queryip_discover_ = nullptr;
			}
			ble_.stop_scan();
			ble_.disconnect_disable_reconnect();
			scan_stack_->set_radio_layer(BUTTON_SCAN_LAYER);
			visible_updateip_buttons(*list_widget_, true);
*/
		}
	}
}

} // namespace gui2

