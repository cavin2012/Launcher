#ifndef PBLE2_HPP_INCLUDED
#define PBLE2_HPP_INCLUDED

#include <set>
#include <vector>
#include <map>
#include <filesystem.hpp>
#include "ble.hpp"
#include "thread.hpp"

class tpble2: public tpble
{
public:

	static const char* uuid_my_service;
	static const char* uuid_write_characteristic;
	static const char* uuid_notify_characteristic;

	tpble2(base_instance* instance);
	~tpble2();

	void simulate_read_characteristic();
private:
	void app_read_characteristic(const std::string& characteristic, const unsigned char* data, int len) override;
};

#endif
