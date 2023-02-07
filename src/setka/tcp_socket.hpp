/*
The MIT License (MIT)

Copyright (c) 2015-2022 Ivan Gagis <igagis@gmail.com>

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
#include <utki/span.hpp>

#include "socket.hpp"
#include "address.hpp"

namespace setka{

class tcp_server_socket;

/**
 * @brief a class which represents a TCP socket.
 */
class tcp_socket : public socket{
	friend class setka::tcp_server_socket;
public:
	
	/**
	 * @brief Constructs an empty TCP socket object.
	 */
	tcp_socket() = default;

	/**
	 * @brief Creates and connects the socket.
	 * This constructor connects the socket to remote TCP server socket.
	 * @param address - IP address.
	 * @param disable_naggle - enable/disable Naggle algorithm.
	 */
	tcp_socket(const address& address, bool disable_naggle = false);
	
	tcp_socket(const tcp_socket&) = delete;
	tcp_socket& operator=(const tcp_socket&) = delete;
	
	tcp_socket(tcp_socket&& s) :
			socket(std::move(s))
	{}

	tcp_socket& operator=(tcp_socket&& s){
		this->socket::operator=(std::move(s));
		return *this;
	}

	/**
	 * @brief Send data to connected socket.
	 * Sends data on connected socket. This method does not guarantee that the whole
	 * buffer will be sent completely, it will return the number of bytes actually sent.
	 * @param buf - pointer to the buffer with data to send.
	 * @return the number of bytes actually sent.
	 */
	size_t send(const utki::span<uint8_t> buf);

	/**
	 * @brief Receive data from connected socket.
	 * Receives data available on the socket.
	 * If there is no data available this function does not block, instead it returns 0,
	 * indicating that 0 bytes were received.
	 * If previous WaitSet::Wait() indicated that socket is ready for reading
	 * and tcp_socket::Recv() returns 0, then connection was closed by peer.
	 * @param buf - pointer to the buffer where to put received data.
	 * @return the number of bytes written to the buffer.
	 */
	size_t receive(utki::span<uint8_t> buf);

	/**
	 * @brief Get local IP address and port.
	 * @return IP address and port of the local socket.
	 */
	address get_local_address();

	/**
	 * @brief Get remote IP address and port.
	 * @return IP address and port of the peer socket.
	 */
	address get_remote_address();

#if CFG_OS == CFG_OS_WINDOWS
private:
	void set_waiting_flags(utki::flags<opros::ready> waiting_flags)override;
#endif
};
}
