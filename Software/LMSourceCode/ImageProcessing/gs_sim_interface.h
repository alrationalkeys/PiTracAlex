/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2022-2025, Verdant Consultants, LLC.
 */

#pragma once

#include <atomic>
#include <memory>
#include <thread>

#include <boost/asio.hpp>
#include <boost/thread/mutex.hpp>


#include "logging_tools.h"
#include "golf_ball.h"
#include "gs_results.h"


// Base class for interfaces to 3rd-party golf simulators

namespace golf_sim {

    class GsSimInterface {

    public:
        enum GolfSimulatorType {
            kNone = 0,
            kGSPro = 1,
            kE6 = 2
        };

        GsSimInterface();
        virtual ~GsSimInterface();

        // Create and initialize and sim interfaces that are configured
        static bool InitializeSims();

        // De-initialize and destory and sim interfaces that are configured
        static void DeInitializeSims();

        // Returns true if at least one golf sim is connected to the system.
        static bool SimIsConnected();

        // To be called from the launch monitor
        static bool SendResultsToGolfSims(const GsResults& results);

        // If the interface is present (usually indicated in the config.json file),
        // this method returns true;
        static bool InterfaceIsPresent();

        // Allows the shot counter to be incremented from outside the simulator
        // interface for such purposes and ensuring the counter keeps going even
        // when a failure occurs.

        static void IncrementShotCounter();

        // Will be overridden by each derived class

        virtual bool Initialize();

        // De-initialize and destroy and sim interfaces that are configured
        virtual void DeInitialize();

        // Base class behavior is to simply print out the JSON
        virtual bool SendResults(const GsResults& results);

        // Sends a string without any other side-effects
        // Returns the number of bytes written
        virtual int SendSimMessage(const std::string& message);

        // Deals with whether or not ALL of the connected simulators are armed
        // (ready to take a shot).  Some sims just return true.
        virtual void SetSimSystemArmed(const bool is_armed);
        virtual bool GetSimSystemArmed();

        // These static functions operate at the collection level for all interfaces
        static long GetShotCounter() { return shot_counter_; };

        // Find the GSPro or E6 or whatever interface (if available) by type
        static GsSimInterface *GetSimInterfaceByType(GolfSimulatorType sim_type);

        // Returns true only if each of the available interfaces is armed
        static bool GetAllSystemsArmed();

        // Heartbeat support for external simulators.
        // Records the ball-detected state; the heartbeat thread sends it to the
        // sims about once a second (e.g., for GSPro's ball-ready indicator).
        // Never blocks, so it is safe to call from the FSM at any point.
        static void SendHeartbeat(bool ball_detected);
        static void ResetHeartbeatState();

        // Periodically re-sends the current status to the connected sims.
        // Started by InitializeSims and stopped by DeInitializeSims.
        static void StartHeartbeatThread();
        static void StopHeartbeatThread();

    protected:

        // Builds and sends the current status to all sims.  Called only from
        // the heartbeat thread.
        static void SendHeartbeatMessage();

        // Typical derived-class behavior will be to convert the results into a
        // sim-specific data packet, such as a JSON string
        virtual std::string GenerateResultsDataToSend(const GsResults& results);

        // Called when the LM receives data
        virtual bool ProcessReceivedData(const std::string received_data);

    protected:

        // Holds pointers to derived interfaces for each attached sim
        static std::vector<GsSimInterface*> interfaces_;

        static std::string launch_monitor_id_string_;
        
        // True if all the attached sims have been initialized
        static bool sims_initialized_;

        static long shot_counter_;

        // The most recent ball-detected state; re-sent by the heartbeat thread
        static std::atomic<bool> heartbeat_ball_detected_state_;
        static std::atomic<bool> heartbeat_thread_running_;
        static std::unique_ptr<std::thread> heartbeat_thread_;

        // Serializes sends to the sims between the FSM (shot results) and the
        // heartbeat thread, which share the same sockets
        static boost::mutex send_mutex_;

        // True if all THIS sim has been initialized
        bool initialized_;

        GolfSimulatorType simulator_type_;

        // Must be true before the simulator system is ready to accept shot data
        // Only relevant for derived, non-virtual classes for whom arming is an
        // actual thing.
        bool sim_system_is_armed_ = false;

        boost::mutex sim_arming_mutex_;
    };

}
