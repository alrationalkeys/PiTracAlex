/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2022-2025, Verdant Consultants, LLC.
 */

#pragma once

#include <chrono>

#include <boost/asio.hpp>
#include <boost/thread.hpp>

#include "gs_results.h"
#include "gs_sim_interface.h"

using namespace boost::asio;
using ip::tcp;

// Base class for representing and transferring Golf Sim results over sockets

namespace golf_sim {

    class GsSimSocketInterface : public GsSimInterface {

    public:
        GsSimSocketInterface();
        virtual ~GsSimSocketInterface();

        // Returns true iff the SimSocket interface is to be used
        static bool InterfaceIsPresent();

        // Must be called before SendResults is called.
        virtual bool Initialize();

        // Deals with, for example, shutting down any socket connection
        virtual void DeInitialize();

        virtual bool SendResults(const GsResults& results);

        virtual void ReceiveSocketData();

    public:

        std::string socket_connect_address_;
        std::string socket_connect_port_;

    protected:

        virtual std::string GenerateResultsDataToSend(const GsResults& results);
        
        virtual bool ProcessReceivedData(const std::string received_data);

        // Default behavior here is just to send the message to the socket and
        // return the number of bytes written
        virtual int SendSimMessage(const std::string& message);

        // Rate-limits how often keepalive/status messages may trigger a
        // re-connection attempt.  Re-connecting can block (TCP timeouts), and
        // the heartbeat thread retries every second, so without this guard a
        // dead sim connection would cause constant reconnection churn.
        bool KeepaliveReconnectDue();

    protected:

        tcp::socket* socket_ = nullptr;
        boost::asio::io_context* io_context_ = nullptr;

        std::unique_ptr<std::thread> receiver_thread_ = nullptr;

        // TBD - Is this thread safe?
        bool receive_thread_exited_ = false;

        // When the last keepalive-triggered reconnection attempt was made
        std::chrono::steady_clock::time_point last_keepalive_reconnect_attempt_{};

        boost::mutex sim_socket_receive_mutex_;
        boost::mutex sim_socket_send_mutex_;
    };

}
