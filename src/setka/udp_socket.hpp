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

#include <string>

#include <utki/config.hpp>
#include <utki/span.hpp>

#include "address.hpp"
#include "socket.hpp"

namespace setka {

/**
 * @brief UDP socket class.
 * Socket for User Datagram Protocol.
 * NOTE: Win32 specific: when using UDP socket with wait_set be aware that waiting on UDP socket for writing does not
 * work on Win32 OS. On other operating systems it works OK.
 */
class udp_socket : public socket
{
	bool ipv4 = true;

public:
	udp_socket() = default;

	/**
	 * @brief Create and open the socket.
	 * Creates and opens the socket, this socket can further be used to send or receive data.
	 * @param port - IP port number on which the socket will listen for incoming datagrams.
	 *               If 0 is passed then system will assign some free port if any. If there
	 *               are no free ports, then it is an error and an exception will be thrown.
	 */
	udp_socket(uint16_t port);

	udp_socket(const udp_socket&) = delete;
	udp_socket& operator=(const udp_socket&) = delete;

	udp_socket(udp_socket&& s) noexcept :
		socket(std::move(s))
	{}

	udp_socket& operator=(udp_socket&& s) noexcept
	{
		this->ipv4 = s.ipv4;
		this->socket::operator=(std::move(s));
		return *this;
	}

	~udp_socket()
#if CFG_OS == CFG_OS_WINDOWS
		override
#endif
		= default;

	/**
	 * @brief Send datagram over UDP socket.
	 * The datagram is sent to UDP socket all at once. If the datagram cannot be
	 * sent at once at the current moment, 0 will be returned.
	 * Note, that underlying protocol limits the maximum size of the datagram,
	 * trying to send the bigger datagram will result in an exception to be thrown.
	 * @param buf - buffer containing the datagram to send.
	 * @param destination_address - the destination IP address to send the datagram to.
	 * @return number of bytes actually sent. Actually it is either 0 or the size of the
	 *         datagram passed in as argument.
	 */
	size_t send(utki::span<const uint8_t> buf, const address& destination_address);

	/**
	 * @brief Receive datagram.
	 * Writes a datagram to the given buffer at once if it is available.
	 * If there is no received datagram available a 0 will be returned.
	 * Note, that it will always write out the whole datagram at once. I.e. it is either all or nothing.
	 * Except for the case when the given buffer is not large enough to store the datagram,
	 * in which case the datagram is truncated to the size of the buffer and the rest of the data is lost.
	 * @param buf - reference to the buffer the received datagram will be stored to. The buffer
	 *              should be large enough to store the whole datagram. If datagram
	 *              does not fit the passed buffer, then the datagram tail will be truncated
	 *              and this tail data will be lost.
	 * @param out_sender_address - reference to the IP-address structure where the IP-address
	 *                             of the sender will be stored.
	 * @return number of bytes stored in the output buffer.
	 */
	size_t recieve(utki::span<uint8_t> buf, address& out_sender_address);

#if CFG_OS == CFG_OS_WINDOWS

private:
	void set_waiting_flags(utki::flags<opros::ready> waiting_flags) override;
#endif
};
} // namespace setka
