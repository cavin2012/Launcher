#ifndef GUI_DIALOGS_SCAN_HPP_INCLUDED
#define GUI_DIALOGS_SCAN_HPP_INCLUDED

#include "gui/dialogs/dialog.hpp"
#include "ble2.hpp"

#include <net/base/ip_endpoint.h>

namespace gui2 {

class tlistbox;
class ttoggle_panel;
class tbutton;
class tstack;
class tlabel;
class tgrid;

class tscan: public tdialog, public tble_plugin
{
public:
	enum tresult {DESKTOP = 1};
	enum {LABEL_SCAN_LAYER, BUTTON_SCAN_LAYER};

	enum {queryip_no, queryip_request, queryip_response, queryip_ok, 
		queryip_connectfail, queryip_getservicesfail, queryip_errorservices, queryip_unknownfail, queryip_invalidsn};
	struct tdiscover {
		tdiscover(const tble2& ble, SDL_BlePeripheral* peripheral, const tmac_addr& _mac_addr);

		SDL_BlePeripheral* peripheral;
		tmac_addr mac_addr;
		int queryip_status;
		net::IPAddress ip;
		int prefixlen;
		net::IPAddress update_to_ip;
		int at;
		std::string sn;
	};

	explicit tscan(tble2& ble);

private:
	/** Inherited from tdialog. */
	void pre_show() override;

	/** Inherited from tdialog. */
	void post_show() override;

	/** Inherited from tdialog, implemented by REGISTER_DIALOG. */
	virtual const std::string& window_id() const;

	void pre_scan_label(tgrid& grid);
	void pre_scan_button(tgrid& grid);
	void app_timer_handler(uint32_t now);

	void did_discover_peripheral(SDL_BlePeripheral& peripheral) override;
	void did_connect_peripheral(SDL_BlePeripheral& peripheral, const int error) override;
	void did_disconnect_peripheral(SDL_BlePeripheral& peripheral, const int error) override;
	void did_start_queryip(SDL_BlePeripheral& peripheral) override;
	void did_query_ip(SDL_BlePeripheral& peripheral, uint32_t ipv4, int prefixlen) override;
	void did_update_ip(SDL_BlePeripheral& peripheral, int resp_code) override;

	void reload_discovers(tlistbox& list);
	void visible_updateip_buttons(tlistbox& list, bool visible);
	void update_ip_stopped_ui();
	void click_scan(tbutton& widget);
	void click_updateip(ttoggle_panel& row, tbutton& widget);
	void click_rdp(ttoggle_panel& row, tbutton& widget);
	void query_ip(tdiscover& discover);
	void update_ip(tdiscover& discover, const tble2::tether3elem& ether3elem);
	void start_scan_internal();
	void stop_scan_internal();
	void stop_action_internal(bool querying);
	void set_scan_description_label(uint32_t now);
	void advance_end_queryip(uint32_t now);

	tdiscover* find_discover(const SDL_BlePeripheral& peripheral);
	void disconnect_and_rescan();
	std::string queryip_str(const tdiscover& discover, bool query) const;
	void set_discover_row_ip_label(const tdiscover& discover, bool query);
	void set_discover_row_label(const tdiscover& discover, const std::string& id, const std::string& label);

private:
	tble2& ble_;
	tlistbox* list_widget_;
	tstack* scan_stack_;
	tlabel* scan_desc_widget_;
	const std::string ip_id_;
	const std::string rssi_id_;
	bool closing_;

	bool scaning_;
	uint32_t stop_scan_ticks_;

	std::vector<tdiscover> discovers_;

	const tdiscover* queryip_discover_;
	uint32_t queryip_halt_ticks_;
	uint32_t updateip_halt_ticks_;
};

} // namespace gui2

#endif

