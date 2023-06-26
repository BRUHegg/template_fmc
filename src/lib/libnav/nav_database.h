#pragma once

#include <vector>
#include <unordered_map>
#include <fstream>
#include <future>
#include <utility>
#include <atomic>
#include <mutex>
#include "common.h"
#include "geo_utils.h"


#define N_NAVAID_LINES_IGNORE 3; //Number of lines at the beginning of the .dat file to ignore
#define N_TILE_DEV_DEG 5; //Maximum deviation of POI in lat/lon. Any POI with less than this deviation will be included in a cache tile
#define N_RNW_ITEMS_IGNORE_BEGINNING 8; // Number of items to ignore at the beginning of the land runway declaration.
#define N_RNW_ITEMS_IGNORE_END 5;
#define N_DOUBLE_OUT_PRECISION 9; // Number of indices after the decimal in the string representation of a double number
#define MIN_RWY_LENGTH_M 2000; // If the longest runway of the airport is less than this, the airport will not be included in the database


enum xplm_arpt_row_codes {
	LAND_ARPT = 1,
	MISC_DATA = 1302,
	LAND_RUNWAY = 100,
	DB_EOF = 99
};

enum navaid_types {
	NAV_NDB = 2,
	NAV_VOR = 3,
	NAV_ILS_LOC = 4,
	NAV_ILS_LOC_ONLY = 5,
	NAV_ILS_GS = 6,
	NAV_ILS_FULL = 10,
	NAV_DME = 12,
	NAV_DME_ONLY = 13,
	NAV_VOR_DME = 15,
	NAV_ILS_DME = 18
};

enum POI_types
{
	POI_WAYPOINT = 1,
	POI_NAVAID = 2,
	POI_AIRPORT = 3
};

namespace navdb
{
	struct runway_entry
	{
		geo::point start, end;
		int displ_threshold_m;
		double impl_length_m = -1;

		double get_implied_length_meters()
		{
			if (impl_length_m <= 0)
			{
				impl_length_m = start.getGreatCircleDistanceNM(end) * NM_TO_M;
			}
			return impl_length_m;
		}
	};

	struct runway
	{
		std::string id;
		runway_entry data;
	};

	struct navaid_entry
	{
		uint16_t type, max_recv;
		geo::point wpt;
		double elevation, mag_var, freq;
	};

	struct airport_data
	{
		geo::point pos;
		uint32_t elevation_ft, transition_alt_ft, transition_level;
	};

	struct airport_entry
	{
		std::unordered_map<std::string, runway_entry> runways;
		airport_data data;
	};

	struct arpt_data
	{
		std::string icao;
		airport_data data;
	};

	struct rnw_data
	{
		std::string icao;
		std::vector<runway> runways;
	};

	struct POI
	{
		std::string id;
		std::vector<geo::point> wpt;
		std::vector<navaid_entry> navaid;
		airport_entry arpt;
		uint8_t type;
	};

	class ArptDB
	{
	public:
		// Aircraft latitude and longitude

		double ac_lat;
		double ac_lon;

		ArptDB(std::unordered_map<std::string, airport_data>* a_db, std::unordered_map<std::string, std::unordered_map<std::string, runway_entry>>* r_db,
			   std::string sim_arpt_path, std::string custom_arpt_path, std::string custom_rnw_path, double lat, double lon);

		int get_load_status();

		int load_from_sim_db();

		void write_to_arpt_db();

		void write_to_rnw_db();

		void load_from_custom_arpt(); // Load data from custom airport database

		void load_from_custom_rnw(); // Load data from custom runway database

		size_t get_airport_data(std::string icao_code, airport_data* out);

	private:
		std::string custom_arpt_db_sign = "ARPTDB";
		std::string custom_rnw_db_sign = "RNWDB";
		bool apt_db_created = false;
		bool rnw_db_created = false;

		// Data for creating a custom airport database

		std::atomic<bool> write_arpt_db{false};

		std::vector<arpt_data> arpt_queue;
		std::vector<rnw_data> rnw_queue;

		std::mutex arpt_queue_mutex;
		std::mutex rnw_queue_mutex;

		std::mutex arpt_db_mutex;
		std::mutex rnw_db_mutex;

		std::string sim_arpt_db_path;
		std::string custom_arpt_db_path;
		std::string custom_rnw_db_path;

		std::future<int> sim_db_loaded;
		std::future<void> arpt_db_task;
		std::future<void> rnw_db_task;

		std::unordered_map<std::string, airport_data>* arpt_db;
		std::unordered_map<std::string, std::unordered_map<std::string, runway_entry>>* rnw_db;

		static bool does_db_exist(std::string path, std::string sign);

		double parse_runway(std::string line, std::vector<runway>* rnw); // Returns runway length in meters

		void add_to_arpt_queue(arpt_data arpt);

		void add_to_rnw_queue(rnw_data rnw);

		
	};

	class NavaidDB
	{
	public:
		//std::string sim_awy_db_path;
		//std::string sim_arpt_db_path;
		//std::string arpt_db_path;

		NavaidDB(std::string wpt_path, std::string navaid_path,
			  std::unordered_map<std::string, std::vector<geo::point>>* wpt_db,
			  std::unordered_map<std::string, std::vector<navaid_entry>>* navaid_db);

		int get_load_status();

		//void update_cache();

		int load_waypoints();

		int load_navaids();

		// get_wpt_info returns 0 if waypoint is not in the database. 
		// Otherwise, returns number of items written to out.
		size_t get_wpt_info(std::string id, std::vector<geo::point>* out); 

		// get_navaid_info returns 0 if waypoint is not in the database. 
		// Otherwise, returns number of items written to out.
		size_t get_navaid_info(std::string id, std::vector<navaid_entry>* out);

		//size_t get_poi_info(std::string id, POI* out);

		~NavaidDB();

	private:
		int comp_types[NAV_ILS_DME + 1] = { 0 };
		int max_comp = NAV_ILS_DME;
		std::string sim_wpt_db_path;
		std::string sim_navaid_db_path;

		std::future<int> wpt_loaded;
		std::future<int> navaid_loaded;

		// For checking existence
		std::unordered_map<std::string, uint64_t> airports;

		std::unordered_map<std::string, std::vector<geo::point>>* wpt_cache;
		std::unordered_map<std::string, std::vector<navaid_entry>>* navaid_cache;

	};

	class NavDB
	{
	public:
		NavDB(NavaidDB* navaid_ptr, ArptDB* arpt_ptr);

		size_t get_poi_info(std::string id, POI* out);

	private:
		NavaidDB* navaid_db;
		ArptDB* arpt_db;
	};
}
