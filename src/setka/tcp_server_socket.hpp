/*
The MIT License (MIT)

Copyright (c) 2015-2023 Ivan Gagis <igagis@gmail.com>

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

/* ================ LICENSE END ================ */

#pragma once

#include <utki/config.hpp>

#include "socket.hpp"
#include "tcp_socket.hpp"

/**
 * @brief the main namespace of ting library.
 * All the declarations of ting library are made inside this namespace.
 */
namespace setka {

/**
 * @brief a class which represents a TCP server socket.
 * TCP server socket is the socket which can listen for new connections
 * and accept them creating an ordinary TCP socket for it.
 */
class tcp_server_socket : public socket
{
	// this flag indicates if accepted sockets should be created with disabled Naggle
	bool disable_naggle = false;

public:
	/**
	 * @brief Creates an invalid (unopened) TCP server socket.
	 */
	tcp_server_socket() = default;

	constexpr static const auto max_pending_connections = 50;

	/**
	 * @brief Creates a connected socket and starts listening on it.
	 * Creates a socket and starts listening on the socket for incoming connections.
	 * @param port - IP port number to listen on.
	 * @param disable_naggle - enable/disable Naggle algorithm for all accepted connections.
	 * @param queue_size - the maximum number of pending connections.
	 */
	tcp_server_socket(uint16_t port, bool disable_naggle = false, uint16_t queue_size = max_pending_connections);

	tcp_server_socket(const tcp_server_socket&) = delete;
	tcp_server_socket& operator=(const tcp_server_socket&) = delete;

	tcp_server_socket(tcp_server_socket&& s) noexcept :
		socket(std::move(static_cast<socket&&>(s))),
		disable_naggle(s.disable_naggle)
	{}

	tcp_server_socket& operator=(tcp_server_socket&& s) noexcept
	{
		this->disable_naggle = s.disable_naggle;
		this->socket::operator=(std::move(s));
		return *this;
	}

	~tcp_server_socket()
#if CFG_OS == CFG_OS_WINDOWS
		override
#endif
		= default;

	/**
	 * @brief Accepts one of the pending connections, non-blocking.
	 * Accepts one of the pending connections and returns a TCP socket object which represents
	 * either a non-empty connected socket or an empty socket object.
	 * This function does not block if there is no any pending connections, it just returns invalid
	 * socket object in this case. One can periodically check for incoming connections by calling this method.
	 * One can also wait on the socket for opros::ready::read to wait for connections.
	 * @return tcp_socket object. One can later check if the returned socket object
	 *         is empty or not by calling socket::is_empty() method on that object.
	 *         - if the socket is non-empty then it is a newly connected socket, further it can be used to send or
	 * receive data.
	 *         - if the socket is empty then there was no any connections pending, so no connection was accepted.
	 */
	tcp_socket accept();

#if CFG_OS == CFG_OS_WINDOWS

private:
	void set_waiting_flags(utki::flags<opros::ready> waiting_flags) override;
#endif
};
} // namespace setka
