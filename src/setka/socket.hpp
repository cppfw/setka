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

#include <string>
#include <sstream>

#include <utki/config.hpp>
#include <utki/debug.hpp>

#if M_OS == M_OS_WINDOWS
#	include <winsock2.h>
#	include <ws2tcpip.h>
#	include <utki/windows.hpp>

// on older windows SDKs IPV6_V6ONLY is not defined, let's define it here if necessary
#	ifndef IPV6_V6ONLY
#		define IPV6_V6ONLY 27
#	endif

#elif M_OS == M_OS_LINUX || M_OS == M_OS_MACOSX || M_OS == M_OS_UNIX
#	include <sys/socket.h>

#else
#	error "Unsupported OS"
#endif

#include <opros/waitable.hpp>

namespace setka{

/**
 * @brief Basic socket class.
 * This is a base class for all socket types such as TCP sockets or UDP sockets.
 */
class socket : public opros::waitable{
protected:
#if M_OS == M_OS_WINDOWS
	typedef SOCKET socket_type;

	static const socket_type invalid_socket = INVALID_SOCKET;
	static const int socket_error = SOCKET_ERROR;
	static const int error_interrupted = WSAEINTR;
	static const int error_again = WSAEWOULDBLOCK;
	static const int error_in_progress = WSAEWOULDBLOCK;

#elif M_OS == M_OS_LINUX || M_OS == M_OS_MACOSX || M_OS == M_OS_UNIX
	typedef int socket_type;

	static const socket_type invalid_socket = -1;
	static const socket_type socket_error = -1;
	static const int error_interrupted = EINTR;
	static const int error_again = EAGAIN;
	static const int error_in_progress = EINPROGRESS;

#else
#	error "Unsupported OS"
#endif

#if M_OS == M_OS_WINDOWS
	WSAEVENT event_for_waitable;
#endif

	socket_type sock;

	socket() :
#if M_OS == M_OS_WINDOWS
			event_for_waitable(WSA_INVALID_EVENT),
#endif
			sock(invalid_socket)
	{
//		TRACE(<< "Socket::Socket(): invoked " << this << std::endl)
	}

	socket& operator=(socket&& s);

	void disable_naggle();

	void set_nonblocking_mode();

public:
	socket(socket&& s) :
			// NOTE: operator=() will call close(), so the socket should be in invalid state!!!
			// Therefore, initialize variables to invalid values.
			socket()
	{
		this->operator=(std::move(s));
	}

	virtual ~socket()noexcept;

	/**
	 * @brief Tells whether the socket is opened or not.
	 * @return Returns true if the socket is opened or false otherwise.
	 */

	bool is_open()const noexcept{
		return this->sock != invalid_socket;
	}

	/**
	 * @brief Closes the socket disconnecting it if necessary.
	 */
	void close()noexcept;

	/**
	 * @brief Returns local port this socket is bound to.
	 * @return local port number to which this socket is bound,
	 *         0 means that the socket is not bound to a port.
	 */
	uint16_t get_local_port();

#if M_OS == M_OS_WINDOWS
private:
	HANDLE get_handle()override;

	bool check_signaled()override;

protected:
	void create_event_for_waitable();

	void close_event_for_waitable();

	void set_waiting_events_for_windows(long flags);

#elif M_OS == M_OS_LINUX || M_OS == M_OS_MACOSX || M_OS == M_OS_UNIX
private:
	int get_handle()override;
#else
#	error "Unsupported OS"
#endif
};

}
