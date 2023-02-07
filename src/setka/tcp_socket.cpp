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

#include "tcp_socket.hpp"

#include <cstring>

#if CFG_OS == CFG_OS_LINUX || CFG_OS == CFG_OS_MACOSX || CFG_OS == CFG_OS_UNIX
#	include <netinet/in.h>
#endif

using namespace setka;

tcp_socket::tcp_socket(const address& ip, bool disable_naggle)
{
	if (!this->is_empty()) {
		throw std::logic_error("tcp_socket::open(): socket is already connected");
	}

#if CFG_OS == CFG_OS_WINDOWS
	this->create_event_for_waitable();
	int& sock = this->win_sock;
#elif CFG_OS == CFG_OS_LINUX || CFG_OS == CFG_OS_MACOSX || CFG_OS == CFG_OS_UNIX
	int& sock = this->handle;
#else
#	error "Unknown OS"
#endif

	sock = ::socket(ip.host.is_v4() ? PF_INET : PF_INET6, SOCK_STREAM, 0);
	if (sock == invalid_socket) {
#if CFG_OS == CFG_OS_WINDOWS
		int error_code = WSAGetLastError();
		this->close_event_for_waitable();
#else
		int error_code = errno;
#endif
		throw std::system_error(error_code, std::generic_category(), "couldn't create socket, ::socket() failed");
	}

	try {
		// disable Naggle algorithm if required
		if (disable_naggle) {
			this->disable_naggle();
		}

		this->set_nonblocking_mode();

		// connecting to remote host
		sockaddr_storage socket_address;

		if (ip.host.is_v4()) {
			auto& sa = reinterpret_cast<sockaddr_in&>(socket_address);
			memset(&sa, 0, sizeof(sa));
			sa.sin_family = AF_INET;
			sa.sin_addr.s_addr = htonl(ip.host.get_v4());
			sa.sin_port = htons(ip.port);
		} else {
			auto& sa = reinterpret_cast<sockaddr_in6&>(socket_address);
			memset(&sa, 0, sizeof(sa));
			sa.sin6_family = AF_INET6;
#if CFG_OS == CFG_OS_MACOSX || CFG_OS == CFG_OS_WINDOWS \
	|| (CFG_OS == CFG_OS_LINUX && CFG_OS_NAME == CFG_OS_NAME_ANDROID)
			sa.sin6_addr.s6_addr[0] = ip.host.quad[0] >> 24;
			sa.sin6_addr.s6_addr[1] = (ip.host.quad[0] >> 16) & 0xff;
			sa.sin6_addr.s6_addr[2] = (ip.host.quad[0] >> 8) & 0xff;
			sa.sin6_addr.s6_addr[3] = ip.host.quad[0] & 0xff;
			sa.sin6_addr.s6_addr[4] = ip.host.quad[1] >> 24;
			sa.sin6_addr.s6_addr[5] = (ip.host.quad[1] >> 16) & 0xff;
			sa.sin6_addr.s6_addr[6] = (ip.host.quad[1] >> 8) & 0xff;
			sa.sin6_addr.s6_addr[7] = ip.host.quad[1] & 0xff;
			sa.sin6_addr.s6_addr[8] = ip.host.quad[2] >> 24;
			sa.sin6_addr.s6_addr[9] = (ip.host.quad[2] >> 16) & 0xff;
			sa.sin6_addr.s6_addr[10] = (ip.host.quad[2] >> 8) & 0xff;
			sa.sin6_addr.s6_addr[11] = ip.host.quad[2] & 0xff;
			sa.sin6_addr.s6_addr[12] = ip.host.quad[3] >> 24;
			sa.sin6_addr.s6_addr[13] = (ip.host.quad[3] >> 16) & 0xff;
			sa.sin6_addr.s6_addr[14] = (ip.host.quad[3] >> 8) & 0xff;
			sa.sin6_addr.s6_addr[15] = ip.host.quad[3] & 0xff;

#else
			sa.sin6_addr.__in6_u.__u6_addr32[0] = htonl(ip.host.quad[0]);
			sa.sin6_addr.__in6_u.__u6_addr32[1] = htonl(ip.host.quad[1]);
			sa.sin6_addr.__in6_u.__u6_addr32[2] = htonl(ip.host.quad[2]);
			sa.sin6_addr.__in6_u.__u6_addr32[3] = htonl(ip.host.quad[3]);
#endif
			sa.sin6_port = htons(ip.port);
		}

		// connect to the remote host
		if (connect(
				sock,
				reinterpret_cast<sockaddr*>(&socket_address),
				ip.host.is_v4()
					? sizeof(sockaddr_in)
					: sizeof(sockaddr_in6
					) // NOTE: on Mac OS for some reason the size should be exactly according to AF_INET/AF_INET6
			)
			== socket_error)
		{
#if CFG_OS == CFG_OS_WINDOWS
			int error_code = WSAGetLastError();
#elif CFG_OS == CFG_OS_LINUX || CFG_OS == CFG_OS_MACOSX || CFG_OS == CFG_OS_UNIX
			int error_code = errno;
#else
#	error "Unsupported OS"
#endif
			if (error_code == error_interrupted) {
				// do nothing, for non-blocking socket the connection request still should remain active
			} else if (error_code == error_in_progress) {
				// do nothing, this is not an error, we have non-blocking socket
			} else {
				throw std::system_error(
					error_code,
					std::generic_category(),
					"could not connect to remote host, connect() failed"
				);
			}
		}
	} catch (...) {
		this->close();
		throw;
	}
}

size_t tcp_socket::send(const utki::span<uint8_t> buf)
{
	if (this->is_empty()) {
		throw std::logic_error("tcp_socket::send(): socket is empty");
	}

#if CFG_OS == CFG_OS_WINDOWS
	int len;
	int& sock = this->win_sock;
#else
	ssize_t len;
	int& sock = this->handle;
#endif

	while (true) {
		len = ::send(sock, reinterpret_cast<const char*>(&*buf.begin()), int(buf.size()), 0);
		if (len == socket_error) {
#if CFG_OS == CFG_OS_WINDOWS
			int error_code = WSAGetLastError();
#else
			int error_code = errno;
#endif
			if (error_code == error_interrupted) {
				continue;
			} else if (error_code == error_again) {
				// can't send more bytes, return 0 bytes sent
				len = 0;
			} else {
				throw std::system_error(
					error_code,
					std::generic_category(),
					"could not send data over network, send() failed"
				);
			}
		}
		break;
	}

	ASSERT(len >= 0)
	return size_t(len);
}

size_t tcp_socket::receive(utki::span<uint8_t> buf)
{
	if (this->is_empty()) {
		throw std::logic_error("tcp_socket::receive(): socket is empty");
	}

#if CFG_OS == CFG_OS_WINDOWS
	int len;
	int& sock = this->win_sock;
#else
	ssize_t len;
	int& sock = this->handle;
#endif

	while (true) {
		len = ::recv(sock, reinterpret_cast<char*>(&*buf.begin()), int(buf.size()), 0);
		if (len == socket_error) {
#if CFG_OS == CFG_OS_WINDOWS
			int error_code = WSAGetLastError();
#else
			int error_code = errno;
#endif

			if (error_code == error_interrupted) {
				continue;
			} else if (error_code == error_again) {
				// no data available, return 0 bytes received
				len = 0;
			} else {
				throw std::system_error(
					error_code,
					std::generic_category(),
					"could not receive data form network, recv() failed"
				);
			}
		}
		break;
	}

	ASSERT(len >= 0)
	return size_t(len);
}

namespace {
address make_ip_address(const sockaddr_storage& addr)
{
	if (addr.ss_family == AF_INET) {
		const auto& a = reinterpret_cast<const sockaddr_in&>(addr);
		return {uint32_t(ntohl(a.sin_addr.s_addr)), uint16_t(ntohs(a.sin_port))};
	} else {
		ASSERT(addr.ss_family == AF_INET6)

		const auto& a = reinterpret_cast<const sockaddr_in6&>(addr);

		return
		{
			address::ip(
#if CFG_OS == CFG_OS_MACOSX || CFG_OS == CFG_OS_WINDOWS \
	|| (CFG_OS == CFG_OS_LINUX && CFG_OS_NAME == CFG_OS_NAME_ANDROID)
				(uint32_t(a.sin6_addr.s6_addr[0]) << 24) | (uint32_t(a.sin6_addr.s6_addr[1]) << 16)
					| (uint32_t(a.sin6_addr.s6_addr[2]) << 8) | uint32_t(a.sin6_addr.s6_addr[3]),
				(uint32_t(a.sin6_addr.s6_addr[4]) << 24) | (uint32_t(a.sin6_addr.s6_addr[5]) << 16)
					| (uint32_t(a.sin6_addr.s6_addr[6]) << 8) | uint32_t(a.sin6_addr.s6_addr[7]),
				(uint32_t(a.sin6_addr.s6_addr[8]) << 24) | (uint32_t(a.sin6_addr.s6_addr[9]) << 16)
					| (uint32_t(a.sin6_addr.s6_addr[10]) << 8) | uint32_t(a.sin6_addr.s6_addr[11]),
				(uint32_t(a.sin6_addr.s6_addr[12]) << 24) | (uint32_t(a.sin6_addr.s6_addr[13]) << 16)
					| (uint32_t(a.sin6_addr.s6_addr[14]) << 8) | uint32_t(a.sin6_addr.s6_addr[15])
#else
				uint32_t(ntohl(a.sin6_addr.__in6_u.__u6_addr32[0])),
				uint32_t(ntohl(a.sin6_addr.__in6_u.__u6_addr32[1])),
				uint32_t(ntohl(a.sin6_addr.__in6_u.__u6_addr32[2])),
				uint32_t(ntohl(a.sin6_addr.__in6_u.__u6_addr32[3]))
#endif
			),
				uint16_t(ntohs(a.sin6_port))
		};
	}
}
} // namespace

address tcp_socket::get_local_address()
{
	if (this->is_empty()) {
		throw std::logic_error("tcp_socket::get_local_address(): socket is empty");
	}

	sockaddr_storage addr;

#if CFG_OS == CFG_OS_WINDOWS
	int len = sizeof(addr);
	int& sock = this->win_sock;
#else
	socklen_t len = sizeof(addr);
	int& sock = this->handle;
#endif

	if (getsockname(sock, reinterpret_cast<sockaddr*>(&addr), &len) == socket_error) {
#if CFG_OS == CFG_OS_WINDOWS
		int error_code = WSAGetLastError();
#else
		int error_code = errno;
#endif
		throw std::system_error(
			error_code,
			std::generic_category(),
			"could not get local address, getsockname() failed"
		);
	}

	return make_ip_address(addr);
}

address tcp_socket::get_remote_address()
{
	if (this->is_empty()) {
		throw std::logic_error("tcp_socket::get_remote_address(): socket is empty");
	}

	sockaddr_storage addr;

#if CFG_OS == CFG_OS_WINDOWS
	int len = sizeof(addr);
	int& sock = this->win_sock;
#else
	socklen_t len = sizeof(addr);
	int& sock = this->handle;
#endif

	if (getpeername(sock, reinterpret_cast<sockaddr*>(&addr), &len) == socket_error) {
#if CFG_OS == CFG_OS_WINDOWS
		int error_code = WSAGetLastError();
#else
		int error_code = errno;
#endif
		throw std::system_error(
			error_code,
			std::generic_category(),
			"could not get remote address, getpeername() failed"
		);
	}

	return make_ip_address(addr);
}

#if CFG_OS == CFG_OS_WINDOWS
void tcp_socket::set_waiting_flags(utki::flags<opros::ready> waiting_flags)
{
	long flags = FD_CLOSE;
	if (waiting_flags.get(opros::ready::read)) {
		flags |= FD_READ;
		// NOTE: since it is not a tcp_server_socket, FD_ACCEPT is not needed here.
	}
	if (waiting_flags.get(opros::ready::write)) {
		flags |= FD_WRITE | FD_CONNECT;
	}
	this->set_waiting_events_for_windows(flags);
}
#endif
