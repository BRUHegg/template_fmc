#include "nav_database.h"


namespace navdb
{

	//ArptDB definitions:

	ArptDB::ArptDB(std::unordered_map<std::string, airport_data>* a_db, std::unordered_map<std::string, std::unordered_map<std::string, runway_entry>>* r_db,
				   std::string sim_arpt_path, std::string custom_arpt_path, std::string custom_rnw_path, double lat, double lon)
	{
		arpt_db = a_db;
		rnw_db = r_db;
		sim_arpt_db_path = sim_arpt_path;
		custom_arpt_db_path = custom_arpt_path;
		custom_rnw_db_path = custom_rnw_path;
		ac_lat = lat;
		ac_lon = lon;
		if (!does_db_exist(custom_arpt_db_path, custom_arpt_db_sign) || !does_db_exist(custom_rnw_db_path, custom_rnw_db_sign))
		{
			write_arpt_db.store(true, std::memory_order_seq_cst);
			sim_db_loaded = std::async(std::launch::async, [](ArptDB* ptr) -> int { return ptr->load_from_sim_db(); }, this);
			if (!does_db_exist(custom_arpt_db_path, custom_arpt_db_sign))
			{
				apt_db_created = true;
				arpt_db_task = std::async(std::launch::async, [](ArptDB* ptr) {ptr->write_to_arpt_db(); }, this);
			}
			if (!does_db_exist(custom_rnw_db_path, custom_rnw_db_sign))
			{
				rnw_db_created = true;
				rnw_db_task = std::async(std::launch::async, [](ArptDB* ptr) {ptr->write_to_rnw_db(); }, this);
			}
		}
		else
		{
			arpt_db_task = std::async(std::launch::async, [](ArptDB* ptr) {ptr->load_from_custom_arpt(); }, this);
			rnw_db_task = std::async(std::launch::async, [](ArptDB* ptr) {ptr->load_from_custom_rnw(); }, this);
		}
	}

	int ArptDB::get_load_status()
	{
		//Wait until all of the threads finish
		arpt_db_task.get();
		rnw_db_task.get();
		if (apt_db_created || rnw_db_created)
		{
			return sim_db_loaded.get();
		}
		return 1;
	}

	bool ArptDB::does_db_exist(std::string path, std::string sign)
	{
		std::ifstream file(path, std::ifstream::in);
		if (file.is_open())
		{
			std::string line;
			getline(file, line);
			if (line == sign)
			{
				file.close();
				return true;
			}
		}
		file.close();
		return false;
	}

	double ArptDB::parse_runway(std::string line, std::vector<runway>* rnw)
	{
		std::stringstream s(line);
		int limit_1 = N_RNW_ITEMS_IGNORE_BEGINNING + 1; // Add 1 because we don't need the row code
		int limit_2 = N_RNW_ITEMS_IGNORE_END;
		std::string junk;
		runway rnw_1;
		runway rnw_2;
		for (int i = 0; i < limit_1; i++)
		{
			s >> junk;
		}
		s >> rnw_1.id >> rnw_1.data.start.lat_deg >> rnw_1.data.start.lon_deg >> rnw_1.data.displ_threshold_m;
		for (int i = 0; i < limit_2; i++)
		{
			s >> junk;
		}
		s >> rnw_2.id >> rnw_1.data.end.lat_deg >> rnw_1.data.end.lon_deg >> rnw_2.data.displ_threshold_m;
		rnw_2.data.start.lat_deg = rnw_1.data.end.lat_deg;
		rnw_2.data.start.lon_deg = rnw_1.data.end.lon_deg;
		rnw_2.data.end.lat_deg = rnw_1.data.start.lat_deg;
		rnw_2.data.end.lon_deg = rnw_1.data.start.lon_deg;

		rnw->push_back(rnw_1);
		rnw->push_back(rnw_2);

		return rnw_1.data.get_implied_length_meters();
	}

	void ArptDB::add_to_arpt_queue(arpt_data arpt)
	{
		std::lock_guard<std::mutex> lock(arpt_queue_mutex);
		arpt_queue.push_back(arpt);
	}

	void ArptDB::add_to_rnw_queue(rnw_data rnw)
	{
		std::lock_guard<std::mutex> lock(rnw_queue_mutex);
		rnw_queue.push_back(rnw);
	}

	int ArptDB::load_from_sim_db()
	{
		std::ifstream file(sim_arpt_db_path, std::ifstream::in);
		if (file.is_open())
		{
			std::string line;
			int i = 0;
			int limit = N_NAVAID_LINES_IGNORE;
			arpt_data tmp_arpt = { "", {{0, 0}, 0, 0} };
			rnw_data tmp_rnw = { "", {} };
			double max_rnw_length_m = 0;

			while (getline(file, line))
			{
				if (i >= limit && line != "")
				{
					int row_code;
					std::string junk;
					std::stringstream s(line);
					s >> row_code;

					if (tmp_arpt.icao != "" && tmp_rnw.icao != "" && (row_code == LAND_ARPT || row_code == DB_EOF))
					{
						// Offload airport data
						double threshold = MIN_RWY_LENGTH_M;

						if (max_rnw_length_m >= threshold && tmp_arpt.data.transition_alt_ft + tmp_arpt.data.transition_level > 0)
						{
							std::unordered_map<std::string, runway_entry> apt_runways;
							size_t n_runways = tmp_rnw.runways.size();

							for (int i = 0; i < n_runways; i++)
							{
								runway rnw = tmp_rnw.runways.at(i);
								std::pair<std::string, runway_entry> tmp = std::make_pair(rnw.id, rnw.data);
								apt_runways.insert(tmp);
								tmp_arpt.data.pos.lat_deg += rnw.data.start.lat_deg;
								tmp_arpt.data.pos.lon_deg += rnw.data.start.lon_deg;
							}

							tmp_arpt.data.pos.lat_deg /= n_runways;
							tmp_arpt.data.pos.lon_deg /= n_runways;

							// Update queues
							add_to_arpt_queue(tmp_arpt);
							add_to_rnw_queue(tmp_rnw);

							// Update internal data 
							std::pair<std::string, airport_data> apt = std::make_pair(tmp_arpt.icao, tmp_arpt.data);
							std::pair<std::string, std::unordered_map<std::string, runway_entry>> rnw_pair = std::make_pair(tmp_arpt.icao, apt_runways);
							arpt_db->insert(apt);
							rnw_db->insert(rnw_pair);
						}

						tmp_arpt.icao = "";
						tmp_rnw.icao = "";
						tmp_arpt.data.pos = { 0, 0 };
						tmp_arpt.data.transition_alt_ft = 0;
						tmp_arpt.data.transition_level = 0;
						tmp_rnw.runways.clear();
						max_rnw_length_m = 0;
					}

					// Parse data

					if (row_code == LAND_ARPT)
					{
						s >> tmp_arpt.data.elevation_ft;
					}
					else if (row_code == MISC_DATA)
					{
						std::string var_name;
						s >> var_name;
						if (var_name == "icao_code")
						{
							std::string icao_code;
							s >> icao_code;
							tmp_arpt.icao = icao_code;
							tmp_rnw.icao = icao_code;
						}
						else if (var_name == "transition_alt")
						{
							s >> tmp_arpt.data.transition_alt_ft;
						}
						else if (var_name == "transition_level")
						{
							s >> tmp_arpt.data.transition_level;
						}
					}
					else if (row_code == LAND_RUNWAY && tmp_arpt.icao != "")
					{
						double tmp = parse_runway(line, &tmp_rnw.runways);
						if (tmp > max_rnw_length_m)
						{
							max_rnw_length_m = tmp;
						}
					}
					else if (row_code == DB_EOF)
					{
						break;
					}
				}
				i++;
			}
			file.close();
			write_arpt_db.store(false, std::memory_order_seq_cst);
			return 1;
		}
		file.close();
		return 0;
	}

	void ArptDB::write_to_arpt_db()
	{
		std::ofstream out(custom_arpt_db_path, std::ofstream::out);
		out << "ARPTDB\n";
		while (arpt_queue.size() || write_arpt_db.load(std::memory_order_seq_cst))
		{
			if (arpt_queue.size())
			{
				std::lock_guard<std::mutex> lock(arpt_queue_mutex);
				uint8_t precision = N_DOUBLE_OUT_PRECISION;
				arpt_data data = arpt_queue[0];
				arpt_queue.erase(arpt_queue.begin());

				std::string arpt_lat = common::double_to_str(data.data.pos.lat_deg, precision);
				std::string arpt_lon = common::double_to_str(data.data.pos.lon_deg, precision);
				std::string arpt_icao_pos = data.icao + " " + arpt_lat + " " + arpt_lon;

				out << arpt_icao_pos << " " << data.data.elevation_ft << " " << data.data.transition_alt_ft << " " << data.data.transition_level << "\n";
			}
		}
		out.close();
	}

	void ArptDB::write_to_rnw_db()
	{
		std::ofstream out(custom_rnw_db_path, std::ofstream::out);
		out << "RNWDB\n";
		while (rnw_queue.size() || write_arpt_db.load(std::memory_order_seq_cst))
		{
			if (rnw_queue.size())
			{
				std::lock_guard<std::mutex> lock(rnw_queue_mutex);
				uint8_t precision = N_DOUBLE_OUT_PRECISION;
				rnw_data data = rnw_queue[0];
				rnw_queue.erase(rnw_queue.begin());
				for (int i = 0; i < data.runways.size(); i++)
				{
					std::string rnw_start_lat = common::double_to_str(data.runways[i].data.start.lat_deg, precision);
					std::string rnw_start_lon = common::double_to_str(data.runways[i].data.start.lon_deg, precision);
					std::string rnw_end_lat = common::double_to_str(data.runways[i].data.end.lat_deg, precision);
					std::string rnw_end_lon = common::double_to_str(data.runways[i].data.end.lon_deg, precision);

					std::string rnw_start = rnw_start_lat + " " + rnw_start_lon;
					std::string rnw_end = rnw_end_lat + " " + rnw_end_lon;

					std::string rnw_icao_pos = data.icao + " " + data.runways[i].id + " " + rnw_start + " " + rnw_end;

					out << rnw_icao_pos << " " << data.runways[i].data.displ_threshold_m << "\n";
				}
			}
		}
		out.close();
	}

	void ArptDB::load_from_custom_arpt()
	{
		std::ifstream file(custom_arpt_db_path, std::ifstream::in);
		if (file.is_open())
		{
			std::string line;
			while (getline(file, line))
			{
				if (line != custom_arpt_db_sign)
				{
					std::string icao;
					airport_data tmp;
					std::stringstream s(line);
					s >> icao >> tmp.pos.lat_deg >> tmp.pos.lon_deg >> tmp.elevation_ft >> tmp.transition_alt_ft >> tmp.transition_level;
					std::pair<std::string, airport_data> tmp_pair = std::make_pair(icao, tmp);
					arpt_db->insert(tmp_pair);
				}
			}
			file.close();
		}
		file.close();
	}

	void ArptDB::load_from_custom_rnw()
	{
		std::ifstream file(custom_rnw_db_path, std::ifstream::in);
		if (file.is_open())
		{
			std::string line;
			std::string curr_icao = "";
			std::unordered_map<std::string, runway_entry> runways = {};
			while (getline(file, line))
			{
				if (line != custom_rnw_db_sign)
				{
					std::string icao, rnw_id;
					runway_entry tmp;
					std::stringstream s(line);
					s >> icao;
					if (icao != curr_icao)
					{
						if (curr_icao != "")
						{
							std::pair<std::string, std::unordered_map<std::string, runway_entry>> icao_runways = std::make_pair(curr_icao, runways);
							rnw_db->insert(icao_runways);
						}
						curr_icao = icao;
						runways.clear();
					}
					s >> rnw_id >> tmp.start.lat_deg >> tmp.start.lon_deg >> tmp.end.lat_deg >> tmp.end.lon_deg >> tmp.displ_threshold_m;
					std::pair<std::string, runway_entry> str_rnw_entry = std::make_pair(rnw_id, tmp);
					runways.insert(str_rnw_entry);
				}
			}
			file.close();
		}
		file.close();
	}

	size_t ArptDB::get_airport_data(std::string icao_code, airport_data* out)
	{
		std::lock_guard<std::mutex> lock(arpt_db_mutex);
		if (arpt_db->find(icao_code) != arpt_db->end())
		{
			airport_data* tmp = &arpt_db->at(icao_code);
			out->pos.lat_deg = tmp->pos.lat_deg;
			out->pos.lon_deg = tmp->pos.lon_deg;
			out->elevation_ft = tmp->elevation_ft;
			out->transition_alt_ft = tmp->transition_alt_ft;
			out->transition_level = tmp->transition_level;
			return 1;
		}
		return 0;
	}

	//NavDB definitions:

	NavaidDB::NavaidDB(std::string wpt_path, std::string navaid_path,
				 std::unordered_map<std::string, std::vector<geo::point>>* wpt_db,
				 std::unordered_map<std::string, std::vector<navaid_entry>>* navaid_db)
	{
		//Pre-defined stuff

		comp_types[NAV_ILS_FULL] = 1;
		comp_types[NAV_VOR_DME] = 1;
		comp_types[NAV_ILS_DME] = 1;

		//Paths

		sim_wpt_db_path = wpt_path;
		sim_navaid_db_path = navaid_path;

		wpt_cache = wpt_db;
		navaid_cache = navaid_db;

		wpt_loaded = std::async(std::launch::async, [](NavaidDB* db) -> int {return db->load_waypoints();}, this);
		navaid_loaded = std::async(std::launch::async, [](NavaidDB* db) -> int {return db->load_navaids();}, this);
	}

	int NavaidDB::get_load_status()
	{
		return wpt_loaded.get() * navaid_loaded.get();
	}

	NavaidDB::~NavaidDB()
	{
		//Free the memory

	}

	int NavaidDB::load_waypoints()
	{
		std::ifstream file(sim_wpt_db_path);
		if (file.is_open())
		{
			std::string line;
			int i = 0;
			int limit = N_NAVAID_LINES_IGNORE;
			while (getline(file, line) && line != "99")
			{
				if (i >= limit)
				{
					//Construct a navaid entry.
					std::stringstream s(line);
					double lat, lon;
					std::string name;
					std::string junk;
					geo::point tmp;
					s >> lat >> lon >> name >> junk;
					tmp.lat_deg = lat;
					tmp.lon_deg = lon;
					//Find the navaid in the database by name.
					if (wpt_cache->find(name) != wpt_cache->end())
					{
						//If there is a navaid with the same name in the database,
						//add new entry to the vector.
						wpt_cache->at(name).push_back(tmp);
					}
					else
					{
						//If there is no navaid with the same name in the database,
						//add a vector with tmp
						std::pair<std::string, std::vector<geo::point>> p;
						p = std::make_pair(name, std::vector<geo::point>{tmp});
						wpt_cache->insert(p);
					}
				}
				i++;
			}
			file.close();
			return 1;
		}
		return 0;
	}

	int NavaidDB::load_navaids()
	{
		std::ifstream file(sim_navaid_db_path);
		if (file.is_open())
		{
			std::string line;
			int i = 0;
			int limit = N_NAVAID_LINES_IGNORE;
			while (getline(file, line))
			{
				std::string check_val;
				std::stringstream s(line);
				s >> check_val;
				if (i >= limit && check_val != "99")
				{
					//Construct a navaid entry.
					std::stringstream s(line);
					uint16_t type, max_recv;
					double lat, lon, elevation, mag_var;
					uint32_t freq;
					std::string name;
					std::string junk;
					navaid_entry tmp;
					s >> type >> lat >> lon >> elevation >> freq >> max_recv >> mag_var >> name >> junk;
					tmp.type = type;
					tmp.max_recv = max_recv;
					tmp.wpt.lat_deg = lat;
					tmp.wpt.lon_deg = lon;
					tmp.elevation = elevation;
					tmp.mag_var = mag_var;
					tmp.freq = freq;
					//Find the navaid in the database by name.
					if (navaid_cache->find(name) != navaid_cache->end())
					{
						//If there is a navaid with the same name in the database,
						//add new entry to the vector.
						bool is_colocated = false;
						std::vector<navaid_entry>* entries = &navaid_cache->at(name);
						for (int i = 0; i < entries->size(); i++)
						{
							navaid_entry* navaid = &entries->at(i);
							double ang_dev = abs(navaid->wpt.lat_deg - tmp.wpt.lat_deg) + abs(navaid->wpt.lon_deg - tmp.wpt.lon_deg);
							int type_sum = tmp.type + navaid->type;
							int is_composite = 0;
							if (type_sum <= max_comp)
							{
								is_composite = comp_types[type_sum];
							}
							if (ang_dev < 0.001 && is_composite && tmp.freq == navaid->freq)
							{
								navaid->type = type_sum;
								is_colocated = true;
								break;
							}
						}
						if (!is_colocated)
						{
							entries->push_back(tmp);
						}
					}
					else
					{
						//If there is no navaid with the same name in the database,
						//add a vector with tmp
						std::pair<std::string, std::vector<navaid_entry>> p;
						p = std::make_pair(name, std::vector<navaid_entry>{tmp});
						navaid_cache->insert(p);
					}
				}
				else if (check_val == "99")
				{
					break;
				}
				i++;
			}
			file.close();
			return 1;
		}
		return 0;
	}

	size_t NavaidDB::get_wpt_info(std::string id, std::vector<geo::point>* out)
	{
		if (wpt_cache->find(id) != wpt_cache->end())
		{
			std::vector<geo::point>* waypoints = &wpt_cache->at(id);
			size_t n_waypoints = waypoints->size();
			for (int i = 0; i < n_waypoints; i++)
			{
				out->push_back(waypoints->at(i));
			}
			return n_waypoints;
		}
		return 0;
	}

	size_t NavaidDB::get_navaid_info(std::string id, std::vector<navaid_entry>* out)
	{
		if (navaid_cache->find(id) != navaid_cache->end())
		{
			std::vector<navaid_entry>* navaids = &navaid_cache->at(id);
			size_t n_navaids = navaids->size();
			for (size_t i = 0; i < n_navaids; i++)
			{
				out->push_back(navaids->at(i));
			}
			return n_navaids;
		}
		return 0;
	}

	NavDB::NavDB(NavaidDB* navaid_ptr, ArptDB* arpt_ptr)
	{
		navaid_db = navaid_ptr;
		arpt_db = arpt_ptr;
	}

	size_t NavDB::get_poi_info(std::string id, POI* out)
	{
		size_t n_airports = arpt_db->get_airport_data(id, &out->arpt.data);
		if (n_airports)
		{
			out->id = id;
			out->type = POI_AIRPORT;
			return n_airports;
		}
		else
		{
			size_t n_navaids = navaid_db->get_navaid_info(id, &out->navaid);
			if (n_navaids)
			{
				out->id = id;
				out->type = POI_NAVAID;
				return n_navaids;
			}
			else
			{
				size_t n_waypoints = navaid_db->get_wpt_info(id, &out->wpt);
				if (n_waypoints)
				{
					out->id = id;
					out->type = POI_WAYPOINT;
					return n_waypoints;
				}
			}
		}
		return 0;
	}
}
