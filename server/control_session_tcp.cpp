/***
    This file is part of snapcast
    Copyright (C) 2014-2020  Johannes Pohl

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
***/

#include "control_session_tcp.hpp"
#include "common/aixlog.hpp"
#include "message/pcm_chunk.hpp"

using namespace std;

// https://stackoverflow.com/questions/7754695/boost-asio-async-write-how-to-not-interleaving-async-write-calls/7756894


ControlSessionTcp::ControlSessionTcp(ControlMessageReceiver* receiver, boost::asio::io_context& ioc, tcp::socket&& socket)
    : ControlSession(receiver), socket_(std::move(socket)), strand_(ioc)
{
}


ControlSessionTcp::~ControlSessionTcp()
{
    LOG(DEBUG) << "ControlSessionTcp::~ControlSessionTcp()\n";
    stop();
}


void ControlSessionTcp::do_read()
{
    const std::string delimiter = "\n";
    boost::asio::async_read_until(
        socket_, streambuf_, delimiter,
        boost::asio::bind_executor(strand_, [ this, self = shared_from_this(), delimiter ](const std::error_code& ec, std::size_t bytes_transferred) {
            if (ec)
            {
                LOG(ERROR) << "Error while reading from control socket: " << ec.message() << "\n";
                return;
            }

            // Extract up to the first delimiter.
            std::string line{buffers_begin(streambuf_.data()), buffers_begin(streambuf_.data()) + bytes_transferred - delimiter.length()};
            if (!line.empty())
            {
                if (line.back() == '\r')
                    line.resize(line.size() - 1);
                // LOG(DEBUG) << "received: " << line << "\n";
                if ((message_receiver_ != nullptr) && !line.empty())
                {
                    string response = message_receiver_->onMessageReceived(this, line);
                    if (!response.empty())
                        sendAsync(response);
                }
            }
            streambuf_.consume(bytes_transferred);
            do_read();
        }));
}


void ControlSessionTcp::start()
{
    do_read();
}


void ControlSessionTcp::stop()
{
    LOG(DEBUG) << "ControlSession::stop\n";
    boost::system::error_code ec;
    socket_.shutdown(boost::asio::ip::tcp::socket::shutdown_both, ec);
    if (ec)
        LOG(ERROR) << "Error in socket shutdown: " << ec.message() << "\n";
    socket_.close(ec);
    if (ec)
        LOG(ERROR) << "Error in socket close: " << ec.message() << "\n";
    LOG(DEBUG) << "ControlSession ControlSession stopped\n";
}


void ControlSessionTcp::sendAsync(const std::string& message)
{
    strand_.post([ this, self = shared_from_this(), message ]() {
        messages_.emplace_back(message);
        if (messages_.size() > 1)
        {
            LOG(DEBUG) << "TCP session outstanding async_writes: " << messages_.size() << "\n";
            return;
        }
        send_next();
    });
}

void ControlSessionTcp::send_next()
{
    auto message = messages_.front();
    boost::asio::async_write(socket_, boost::asio::buffer(message + "\r\n"),
                             boost::asio::bind_executor(strand_, [ this, self = shared_from_this() ](std::error_code ec, std::size_t length) {
                                 messages_.pop_front();
                                 if (ec)
                                 {
                                     LOG(ERROR) << "Error while writing to control socket: " << ec.message() << "\n";
                                 }
                                 else
                                 {
                                     LOG(DEBUG) << "Wrote " << length << " bytes to control socket\n";
                                 }
                                 if (!messages_.empty())
                                     send_next();
                             }));
}

bool ControlSessionTcp::send(const std::string& message)
{
    boost::system::error_code ec;
    boost::asio::write(socket_, boost::asio::buffer(message + "\r\n"), ec);
    return !ec;
}
