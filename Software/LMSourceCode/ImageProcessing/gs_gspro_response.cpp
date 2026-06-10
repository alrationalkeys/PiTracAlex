/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2022-2025, Verdant Consultants, LLC.
 */

#include <boost/program_options.hpp>
#include <boost/property_tree/json_parser.hpp>

#include "logging_tools.h"

#include "gs_gspro_response.h"

namespace golf_sim {

    GsGSProResponse::GsGSProResponse() {
    }


    bool GsGSProResponse::ParseJson(const std::string& gspro_json_string) {

        int return_code = 0;
        std::string message_str;
        std::string handedness_str;
        std::string club_str;

        std::stringstream ss;
        ss << gspro_json_string;

        try {
            boost::property_tree::ptree pt;
            boost::property_tree::read_json(ss, pt);

            GS_LOG_TRACE_MSG(trace, "GsGSProResponse::Parsing return_code and message_str.");

            return_code = pt.get<int>("Code", 0);
            message_str = pt.get<std::string>("Message", "");

            // Player handedness and club may not be set
            boost::optional< boost::property_tree::ptree& > child = pt.get_child_optional("Player");
            if (!child) {
                GS_LOG_MSG(warning, "GsGSProResponse::ParseJson - No player information was provided.");
            }
            else {
                handedness_str = pt.get<std::string>("Player.Handed", "");
                club_str = pt.get<std::string>("Player.Club", "");
            }
        }
        catch (std::exception const& e)
        {
            // For now, return true even if we failed - TBD - need to figure out what garbage at end means
            // from the boost library
            GS_LOG_MSG(error, "GsGSProResponse::ParseJson failed to parse GSPro response: " + std::string(e.what()));
            return true;
        }

        message_ = message_str;

        if (handedness_str == "RH") {
            player_handed_ = PlayerHandedness::kRightHanded;
        }
        else if (handedness_str == "LH") {
            player_handed_ = PlayerHandedness::kLeftHanded;
        }
        else {
            // Don't bail even if this is not set
            GS_LOG_MSG(warning, "GsGSProResponse::ParseJson received unknown player handedness value from GSPro response:" + handedness_str);
        }

        if (club_str == "PT") {
            player_club_ = PlayerClub::kPutter;
        }
        else if (club_str == "I8" || club_str == "I9" ||
                 club_str == "PW" || club_str == "GW" || club_str == "AW" ||
                 club_str == "SW" || club_str == "LW") {
            // High-lofted clubs - the slower, higher-launching ball allows
            // wider strobe pulse spacing
            player_club_ = PlayerClub::kIronOrWedge;
        }
        else if (club_str == "") {
            GS_LOG_MSG(warning, "GsGSProResponse::ParseJson received no player club value from GSPro response. Defaulting to Driver");
            player_club_ = PlayerClub::kDriver;
        }
        else {
            // Everything else (driver, woods, hybrids, long/mid irons) is
            // treated as a fast, low-launch club
            GS_LOG_MSG(info, "GsGSProResponse::ParseJson treating club '" + club_str + "' as a low-lofted (driver-type) club.");
            player_club_ = PlayerClub::kDriver;
        }

        switch (return_code) {
            case 200: {
                return_code_ = kShotReceivedSuccessfully;
                break;
            }

            case 201: {
                return_code_ = kPlayerInformation;
                break;
            }

            case 501: {
                return_code_ = k501Failure;
                break;
            }

            default: {
                if (return_code == 500 ||
                    (return_code > 501 && return_code < 600)) {
                    return_code_ = kShotOtherFailure;
                }
                else {
                    GS_LOG_MSG(error, "Received unknown return_code response from GSPro: " + std::to_string(return_code));
                    return_code_ = kShotOtherFailure;
                }
                break;
            }
        }

        return true;
    }

    GsGSProResponse::~GsGSProResponse() {

    }

    std::string GsGSProResponse::Format() const {
        std::string s;

        std::string handed_str = (player_handed_ == PlayerHandedness::kLeftHanded) ? "LH" : "RH";
        std::string club_str = "Driver";
        if (player_club_ == PlayerClub::kPutter) {
            club_str = "Putter";
        }
        else if (player_club_ == PlayerClub::kIronOrWedge) {
            club_str = "Iron/Wedge";
        }

        s += "Return Code: " + std::to_string(return_code_) + ".";
        s += " Message: " + message_ + "\n";
        s += " Player.Handed: " + handed_str;
        s += " Player.Club: " + club_str;

        return s;
    }

}
