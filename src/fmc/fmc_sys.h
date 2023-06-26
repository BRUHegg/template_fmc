/*
	This header file contains definitions of classes, functions, etc 
	used in the fmc implementation.
*/

#pragma once

#include "dr_cache.h"
#include "databus.h"
#include "nav_database.h"
#include <cstring>


enum fmc_pages
{
	RTE = 1,
	REF_NAV_DATA = 2
};


namespace StratosphereAvionics
{

	struct fmc_in_drs
	{
		//std::string curr_page;
		std::string ref_nav_in_id;
	};

	struct fmc_out_drs
	{
		//std::string scratch_pad_msg;
		//std::vector<std::string> screen_lines;
		std::string apt_lat, apt_lon, apt_elevation;
	};

	class AvionicsSys 
	{
	public:
		char path_sep[2];
		std::string xplane_path;
		std::string prefs_path;
		std::string sim_apt_path;
		std::string default_data_path;
		int xplane_version;
		std::atomic<bool> sim_shutdown{false};

		std::shared_ptr<XPDataBus::DataBus> xp_databus;

		std::unordered_map<std::string, std::vector<navdb::navaid_entry>> navaids;
		std::unordered_map<std::string, std::vector<geo::point>> waypoints;

		std::unordered_map<std::string, navdb::airport_data> airports;
		std::unordered_map<std::string, std::unordered_map<std::string, navdb::runway_entry>> runways;

		navdb::ArptDB* apt_db;
		navdb::NavaidDB* navaid_db;

		AvionicsSys(std::shared_ptr<XPDataBus::DataBus> databus);

		void update_sys();

		void main_loop();

		~AvionicsSys();

	private:
		std::string icao_entry_last;

		void update_load_status();
	};

	class FMC
	{
	public:
		std::atomic<bool> sim_shutdown{ false };

		FMC(std::shared_ptr<AvionicsSys> av, fmc_in_drs* in, fmc_out_drs* out);

		void update_ref_nav(); // Updates ref nav data page

		void main_loop();

		~FMC();
	private:
		navdb::ArptDB* apt_db;
		std::shared_ptr<AvionicsSys> avionics;
		fmc_in_drs in_drs;
		fmc_out_drs out_drs;

		std::shared_ptr<XPDataBus::DataBus> xp_databus;

		XPDataBus::DataRefCache* dr_cache;
	};
}
