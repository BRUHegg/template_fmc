#pragma once

#include "common.h"
#include <unordered_map>


namespace XPDataBus
{
	class DataRefCache
	{
	public:
		DataRefCache();

		generic_val get_val(std::string dr_name);

		std::string get_val_s(std::string dr_name);

		void set_val(std::string dr_name, generic_val val);

		void set_val_s(std::string dr_name, std::string in);
	private:
		std::unordered_map<std::string, generic_val> cache;
	};
}
