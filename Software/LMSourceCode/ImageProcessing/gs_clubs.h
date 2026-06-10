/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2022-2025, Verdant Consultants, LLC.
 */

#pragma once

#include <string>

// Tracks the type of club currently being hit.  The club type selects,
// for example, which strobe pulse timing vector is used for the shot.

namespace golf_sim {

	class GolfSimClubs {

	public:

		// kDriver covers all low-lofted, fast-ball clubs (driver, woods,
		// hybrids, long/mid irons).  kIron covers high-lofted, slower-ball
		// clubs (short irons and wedges), which can use wider strobe spacing.
		enum GsClubType	{
			kNotSelected = 0,
			kDriver = 1,
			kIron = 2,
			kPutter = 3
		};

		static GsClubType current_club_;

		static GsClubType GetCurrentClubType();
		static void SetCurrentClubType(GsClubType club_type);

		// The club selection file allows the web dashboard (a separate
		// process) to change the current club.  The LM checks the file for
		// changes while waiting for a ball, and re-writes it whenever the
		// club changes for any other reason (e.g., a GSPro club message), so
		// that the dashboard always reflects the current state.
		static void CheckForClubSelectionFileChange();

		static std::string ClubTypeToString(GsClubType club_type);
		static GsClubType ClubTypeFromString(const std::string& s);

	private:
		static std::string GetClubSelectionFilePath();
		static void WriteClubSelectionFile(GsClubType club_type);

		// Modification time (as time_t) of the selection file when last read
		static long long last_club_file_mod_time_;
	};

}
