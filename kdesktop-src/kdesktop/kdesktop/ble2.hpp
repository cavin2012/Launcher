#ifndef BLE2_HPP_INCLUDED
#define BLE2_HPP_INCLUDED

#include <set>
#include <vector>
#include <map>
#include <filesystem.hpp>
#include "ble.hpp"
#include "thread.hpp"

class tble_plugin
{
public:
	virtual void did_discover_peripheral(SDL_BlePeripheral& peripheral) {}
	virtual void did_release_peripheral(SDL_BlePeripheral& peripheral) {}
	virtual void did_connect_peripheral(SDL_BlePeripheral& peripheral, const int error) {}
	virtual void did_disconnect_peripheral(SDL_BlePeripheral& peripheral, const int error) {}
    virtual void did_discover_services(SDL_BlePeripheral& peripheral, const int error) {}
	virtual void did_discover_characteristics(SDL_BlePeripheral& peripheral, SDL_BleService& service, const int error) {}
	virtual void did_write_characteristic(SDL_BlePeripheral& peripheral, SDL_BleCharacteristic& characteristic, const int error) {}
	virtual void did_notify_characteristic(SDL_BlePeripheral& peripheral, SDL_BleCharacteristic& characteristic, const int error) {}
	virtual void did_start_queryip(SDL_BlePeripheral& peripheral) {}
	virtual void did_query_ip(SDL_BlePeripheral& peripheral, uint32_t ipv4, int prefixlen) {}
	virtual void did_update_ip(SDL_BlePeripheral& peripheral, int resp_code) {}
};

class tble2: public tble
{
public:
	enum {taskid_postready, taskid_queryip, taskid_updateip};
	struct tether3elem
	{
		tether3elem()
		{
			clear();
		}

		bool valid() const;

		void clear()
		{
			ipv4 = 0;
			prefixlen = 0;
			gateway = 0;
		}

		uint32_t ipv4;
		int prefixlen;
		uint32_t gateway;
	};

	static const char* uuid_my_service;
	static const char* uuid_write_characteristic;
	static const char* uuid_notify_characteristic;
	static bool is_discovery_name(const SDL_BlePeripheral& peripheral);

	tble2(base_instance* instance);
	~tble2();

	void set_plugin(tble_plugin* plugin)
    {
        plugin_ = plugin;
    }

	void connect_to(const tmac_addr& mac_addr);
	void disconnect_disable_reconnect();

	const tether3elem& ether3elem() const { return ether3elem_; }
	void set_ether3elem(const tether3elem& ether3elem) { ether3elem_ = ether3elem; }

private:
	bool is_right_services() override;
	void extend_receive_buf(int size, int vsize);

	void app_discover_peripheral(SDL_BlePeripheral& peripheral);
	void app_release_peripheral(SDL_BlePeripheral& peripheral);
	void app_connect_peripheral(SDL_BlePeripheral& peripheral, const int error);
	void app_disconnect_peripheral(SDL_BlePeripheral& peripheral, const int error);
	void app_discover_characteristics(SDL_BlePeripheral& peripheral, SDL_BleService& service, const int error);
	void app_read_characteristic(SDL_BlePeripheral& peripheral, SDL_BleCharacteristic& characteristic, const unsigned char* data, int len);
	void app_write_characteristic(SDL_BlePeripheral& peripheral, SDL_BleCharacteristic& characteristic, const int error);
	void app_notify_characteristic(SDL_BlePeripheral& peripheral, SDL_BleCharacteristic& characteristic, const int error);
	bool app_task_callback(ttask& task, int step_at, bool start);
    void app_calculate_mac_addr(SDL_BlePeripheral& peripheral);
    
public:
	int file_idle_len;

private:
	tble_plugin* plugin_;
	
	char* receive_buf_;
	int receive_buf_size_;

	tether3elem ether3elem_;

	tconnector connector_;
	threading::mutex mutex_;
};

#endif
