/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2022-2025, Verdant Consultants, LLC.
 */

#include <cstdlib>
#include <filesystem>
#include <fstream>

#include "logging_tools.h"
#include "gs_config.h"
#include "gs_result_types.h"
#include "gs_http_client.h"
#include "gs_clubs.h"


namespace golf_sim {

	GolfSimClubs::GsClubType GolfSimClubs::current_club_ = GolfSimClubs::kNotSelected;
	long long GolfSimClubs::last_club_file_mod_time_ = 0;

	GolfSimClubs::GsClubType GolfSimClubs::GetCurrentClubType() {

		return current_club_;
	}

	std::string GolfSimClubs::ClubTypeToString(GsClubType club_type) {
		switch (club_type) {
			case GsClubType::kPutter: return "putter";
			case GsClubType::kIron:   return "iron";
			case GsClubType::kDriver: return "driver";
			default:                  return "driver";
		}
	}

	GolfSimClubs::GsClubType GolfSimClubs::ClubTypeFromString(const std::string& s) {
		if (s == "putter") return GsClubType::kPutter;
		if (s == "iron")   return GsClubType::kIron;
		return GsClubType::kDriver;
	}

	std::string GolfSimClubs::GetClubSelectionFilePath() {
		const char* home = std::getenv("HOME");
		if (home == nullptr) {
			return "";
		}
		return std::string(home) + "/.pitrac/config/club_selection.txt";
	}

	void GolfSimClubs::WriteClubSelectionFile(GsClubType club_type) {
		std::string path = GetClubSelectionFilePath();
		if (path.empty()) {
			return;
		}

		try {
			// Avoid re-writing (and re-triggering the file watcher) if the
			// file already holds the current selection
			std::error_code ec;
			if (std::filesystem::exists(path, ec)) {
				std::ifstream in(path);
				std::string existing;
				in >> existing;
				if (existing == ClubTypeToString(club_type)) {
					return;
				}
			}

			std::ofstream out(path, std::ios::trunc);
			out << ClubTypeToString(club_type);
			out.close();

			// Don't treat our own write as an external change
			auto t = std::filesystem::last_write_time(path, ec);
			if (!ec) {
				last_club_file_mod_time_ = (long long)t.time_since_epoch().count();
			}
		}
		catch (std::exception& e) {
			GS_LOG_MSG(warning, "Could not write club selection file: " + std::string(e.what()));
		}
	}

	void GolfSimClubs::CheckForClubSelectionFileChange() {
		std::string path = GetClubSelectionFilePath();
		if (path.empty()) {
			return;
		}

		try {
			std::error_code ec;
			if (!std::filesystem::exists(path, ec)) {
				return;
			}

			auto t = std::filesystem::last_write_time(path, ec);
			if (ec) {
				return;
			}

			long long mod_time = (long long)t.time_since_epoch().count();
			if (mod_time == last_club_file_mod_time_) {
				return;
			}
			last_club_file_mod_time_ = mod_time;

			std::ifstream in(path);
			std::string selection;
			in >> selection;

			GsClubType new_club = ClubTypeFromString(selection);
			if (new_club != current_club_) {
				GS_LOG_MSG(info, "Club selection file changed - switching club type to " + selection);
				SetCurrentClubType(new_club);
			}
		}
		catch (std::exception& e) {
			GS_LOG_MSG(warning, "Could not read club selection file: " + std::string(e.what()));
		}
	}

	void GolfSimClubs::SetCurrentClubType(GsClubType club_type) {
		current_club_ = club_type;

		std::string club_name = ClubTypeToString(club_type);
		GS_LOG_MSG(info, "Club type set to " + club_name);

		// Keep the selection file in sync so the web dashboard reflects club
		// changes that came from elsewhere (e.g., GSPro)
		WriteClubSelectionFile(club_type);

		// Notify the GUI, and possibly any attached Golf Sims about the change
		// TBD - We need a new type of message.
		// For now, just send a zero-results message with the
		// new driver setting.

#ifdef __unix__
		std::string json = "{\"result_type\":" + std::to_string(static_cast<int>(GsIPCResultType::kHit))
			+ ",\"message\":\"Club type was set to " + club_name + "\""
			+ ",\"speed_mps\":0,\"launch_angle\":0,\"side_angle\":0"
			+ ",\"back_spin\":0,\"side_spin\":0,\"carry\":0,\"images\":[]}";
		GsHttpClient::PostResult(json);
#endif
	}


}
