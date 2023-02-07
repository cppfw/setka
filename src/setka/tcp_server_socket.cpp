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

#include "tcp_server_socket.hpp"

#include <cstring>

#if CFG_OS == CFG_OS_LINUX || CFG_OS == CFG_OS_MACOSX || CFG_OS == CFG_OS_UNIX
#	include <netinet/in.h>
#endif

using namespace setka;

tcp_server_socket::tcp_server_socket(uint16_t port, bool disable_naggle, uint16_t queue_size) :
	disable_naggle(disable_naggle)
{
#if CFG_OS == CFG_OS_WINDOWS
	this->create_event_for_waitable();
	int& sock = this->win_sock;
#else
	int& sock = this->handle;
#endif

	bool ipv4 = false;

	sock = ::socket(PF_INET6, SOCK_STREAM, 0);

	if (sock == invalid_socket) {
		// maybe IPv6 is not supported by OS, try creating IPv4 socket

		sock = ::socket(PF_INET, SOCK_STREAM, 0);

		if (sock == invalid_socket) {
#if CFG_OS == CFG_OS_WINDOWS
			this->close_event_for_waitable();
#endif
			throw std::system_error(
				errno,
				std::generic_category(),
				"couldn't create IPv4 TCP server socket, socket() failed"
			);
		}

		ipv4 = true;
	}

	// turn off IPv6 only mode to allow also accepting IPv4 connections
	if (!ipv4) {
#if CFG_OS == CFG_OS_WINDOWS
		char no = 0;
		const char* noPtr = &no;
#else
		int no = 0;
		void* noPtr = &no;
#endif
		if (setsockopt(sock, IPPROTO_IPV6, IPV6_V6ONLY, noPtr, sizeof(no)) != 0) {
			// Dual stack is not supported, proceed with IPv4 only.

			this->close(); // close IPv6 socket

			// create IPv4 socket

#if CFG_OS == CFG_OS_WINDOWS
			this->create_event_for_waitable();
#endif

			sock = ::socket(PF_INET, SOCK_STREAM, 0);

			if (sock == invalid_socket) {
#if CFG_OS == CFG_OS_WINDOWS
				this->close_event_for_waitable();
#endif
				throw std::system_error(
					errno,
					std::generic_category(),
					"couldn't create IPv4 server socket, socket() failed"
				);
			}

			ipv4 = true;
		}
	}

	// allow local address reuse
	{
		int yes = 1;
		setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (char*)&yes, sizeof(yes));
	}

	sockaddr_storage socket_address;
	socklen_t socket_address_length;

	if (ipv4) {
		sockaddr_in& sa = reinterpret_cast<sockaddr_in&>(socket_address);
		memset(&sa, 0, sizeof(sa));
		sa.sin_family = AF_INET;
		sa.sin_addr.s_addr = INADDR_ANY;
		sa.sin_port = htons(port);
		socket_address_length = sizeof(sa);
	} else {
		sockaddr_in6& sa = reinterpret_cast<sockaddr_in6&>(socket_address);
		memset(&sa, 0, sizeof(sa));
		sa.sin6_family = AF_INET6;
		sa.sin6_addr = in6addr_any; // 'in6addr_any' allows accepting both IPv4 and IPv6 connections!!!
		sa.sin6_port = htons(port);
		socket_address_length = sizeof(sa);
	}

	// Bind the socket for listening
	if (bind(sock, reinterpret_cast<sockaddr*>(&socket_address), socket_address_length) == socket_error) {
#if CFG_OS == CFG_OS_WINDOWS
		int errorCode = WSAGetLastError();
#elif CFG_OS == CFG_OS_LINUX || CFG_OS == CFG_OS_MACOSX || CFG_OS == CFG_OS_UNIX
		int errorCode = errno;
#else
#	error "Unsupported OS"
#endif

		this->close();

		throw std::system_error(errorCode, std::generic_category(), "could not bind socket, bind() failed");
	}

	if (listen(sock, int(queue_size)) == socket_error) {
#if CFG_OS == CFG_OS_WINDOWS
		int errorCode = WSAGetLastError();
#elif CFG_OS == CFG_OS_LINUX || CFG_OS == CFG_OS_MACOSX || CFG_OS == CFG_OS_UNIX
		int errorCode = errno;
#else
#	error "Unsupported OS"
#endif

		this->close();

		throw std::system_error(
			errorCode,
			std::generic_category(),
			"couldn't listen on the local port, listen() failed"
		);
	}

	try {
		this->set_nonblocking_mode();
	} catch (...) {
		this->close();
		throw;
	}
}

tcp_socket tcp_server_socket::accept()
{
	if (this->is_empty()) {
		throw std::logic_error("tcp_server_socket::accept(): the socket is not opened");
	}

	sockaddr_storage socket_address;

	tcp_socket s;

#if CFG_OS == CFG_OS_WINDOWS
	int sock_alen = sizeof(socket_address);
	int& sock = this->win_sock;
	int& accepted_sock = s.win_sock;
#elif CFG_OS == CFG_OS_LINUX || CFG_OS == CFG_OS_MACOSX || CFG_OS == CFG_OS_UNIX
	socklen_t sock_alen = sizeof(socket_address);
	int& sock = this->handle;
	int& accepted_sock = s.handle;
#else
#	error "Unsupported OS"
#endif

#if CFG_OS == CFG_OS_WINDOWS
	s.create_event_for_waitable();
#endif

	accepted_sock = ::accept(sock, reinterpret_cast<sockaddr*>(&socket_address), &sock_alen);

	if (accepted_sock == invalid_socket) {
#if CFG_OS == CFG_OS_WINDOWS
		s.close_event_for_waitable();
#endif
		return s; // no connections to be accepted, return invalid socket
	}

	try {
#if CFG_OS == CFG_OS_WINDOWS
		// NOTE: accepted socket is associated with the same event object as the listening socket which accepted it.
		// Re-associate the socket with its own event object.
		s.set_waiting_flags(utki::make_flags<opros::ready>({}));
#endif
		s.set_nonblocking_mode();

		if (this->disable_naggle) {
			s.disable_naggle();
		}

		return s; // return a newly created socket
	} catch (...) {
		s.close();
		throw;
	}
}

#if CFG_OS == CFG_OS_WINDOWS
void tcp_server_socket::set_waiting_flags(utki::flags<opros::ready> waiting_flags)
{
	if (!waiting_flags.is_clear() && !waiting_flags.get(opros::ready::read)) {
		throw std::logic_error("tcp_server_socket::SetWaitingEvents(): only READ flag allowed");
	}

	long flags = FD_CLOSE;
	if (waiting_flags.get(opros::ready::read)) {
		flags |= FD_ACCEPT;
	}
	this->set_waiting_events_for_windows(flags);
}
#endif
