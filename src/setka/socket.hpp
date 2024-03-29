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

#include <sstream>
#include <string>

#include <utki/config.hpp>
#include <utki/debug.hpp>

#if CFG_OS == CFG_OS_WINDOWS
#	include <winsock2.h>
#	include <ws2tcpip.h>
#	include <utki/windows.hpp>

// on older windows SDKs IPV6_V6ONLY is not defined, let's define it here if necessary
#	ifndef IPV6_V6ONLY
#		define IPV6_V6ONLY 27
#	endif

#elif CFG_OS == CFG_OS_LINUX || CFG_OS == CFG_OS_MACOSX || CFG_OS == CFG_OS_UNIX
#	include <sys/socket.h>

#else
#	error "Unsupported OS"
#endif

#include <opros/waitable.hpp>

namespace setka {

/**
 * @brief Basic socket class.
 * This is a base class for all socket types such as TCP sockets or UDP sockets.
 */
// TODO: this lint suppression is only for WINDOWS,
//       consider making opros::waitable destructor non-virtual
// NOLINTNEXTLINE(cppcoreguidelines-virtual-class-destructor)
class socket : public opros::waitable
{
protected:
#if CFG_OS == CFG_OS_WINDOWS
	using socket_type = SOCKET;

	constexpr static const socket_type invalid_socket = INVALID_SOCKET;
	constexpr static const int socket_error = SOCKET_ERROR;
	constexpr static const int error_interrupted = WSAEINTR;
	constexpr static const int error_again = WSAEWOULDBLOCK;
	constexpr static const int error_in_progress = WSAEWOULDBLOCK;
	constexpr static const int error_not_connected = WSAENOTCONN;

#elif CFG_OS == CFG_OS_LINUX || CFG_OS == CFG_OS_MACOSX || CFG_OS == CFG_OS_UNIX
	using socket_type = int;

	constexpr static const socket_type invalid_socket = -1;
	constexpr static const socket_type socket_error = -1;
	constexpr static const int error_interrupted = EINTR;
	constexpr static const int error_again = EAGAIN;
	constexpr static const int error_in_progress = EINPROGRESS;
	constexpr static const int error_not_connected = ENOTCONN;

#else
#	error "Unsupported OS"
#endif

#if CFG_OS == CFG_OS_WINDOWS
	socket_type win_sock = invalid_socket;
#endif

	socket() :
		opros::waitable(
#if CFG_OS == CFG_OS_WINDOWS
			WSA_INVALID_EVENT
#elif CFG_OS == CFG_OS_LINUX || CFG_OS == CFG_OS_MACOSX
			invalid_socket
#else
#	error "Unknown OS"
#endif
		)
	{
		//		TRACE(<< "Socket::Socket(): invoked " << this << std::endl)
	}

	// TODO: remove lint suppression when https://github.com/llvm/llvm-project/issues/55143 is fixed
	// NOLINTNEXTLINE(bugprone-exception-escape)
	socket& operator=(socket&& s) noexcept(false);

	void disable_naggle();

	void set_nonblocking_mode();

protected:
	// socket is not supposed to be used as polymorphic class,
	// hence the destructor is protected
	~socket() noexcept
#if CFG_OS == CFG_OS_WINDOWS
		override
#endif
		;

	/**
	 * @brief Closes the socket disconnecting it if necessary.
	 */
	void close() noexcept;

public:
	socket(const socket&) = delete;
	socket& operator=(const socket&) = delete;

	// TODO: remove lint suppression when https://github.com/llvm/llvm-project/issues/55143 is fixed
	// NOLINTNEXTLINE(bugprone-exception-escape)
	socket(socket&& s) noexcept(false) :
		// NOTE: operator=() will call close(), so the socket should be in invalid state!!!
		// Therefore, initialize variables to invalid values.
		socket()
	{
		this->operator=(std::move(s));
	}

	/**
	 * @brief Tells whether the socket is empty or not.
	 * @return Returns true if the socket is empty or false otherwise.
	 */

	bool is_empty() const noexcept
	{
#if CFG_OS == CFG_OS_WINDOWS
		return this->handle == WSA_INVALID_EVENT;
#elif CFG_OS == CFG_OS_LINUX || CFG_OS == CFG_OS_MACOSX
		return this->handle == invalid_socket;
#else
#	error "Unknown OS"
#endif
	}

	/**
	 * @brief Returns local port this socket is bound to.
	 * @return local port number to which this socket is bound,
	 *         0 means that the socket is not bound to a port.
	 */
	uint16_t get_local_port();

#if CFG_OS == CFG_OS_WINDOWS

private:
	utki::flags<opros::ready> get_readiness_flags() override;

protected:
	void create_event_for_waitable();

	void close_event_for_waitable();

	void set_waiting_events_for_windows(long flags);

#elif CFG_OS == CFG_OS_LINUX || CFG_OS == CFG_OS_MACOSX || CFG_OS == CFG_OS_UNIX
#else
#	error "Unsupported OS"
#endif
};

} // namespace setka
