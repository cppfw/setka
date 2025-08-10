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
	socket_type& sock = this->win_sock;
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
		sockaddr_storage socket_address{};

		if (ip.host.is_v4()) {
			// NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
			auto& sa = reinterpret_cast<sockaddr_in&>(socket_address);
			memset(&sa, 0, sizeof(sa));
			sa.sin_family = AF_INET;
			sa.sin_addr.s_addr = htonl(ip.host.get_v4());
			sa.sin_port = htons(ip.port);
		} else {
			// NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
			auto& sa = reinterpret_cast<sockaddr_in6&>(socket_address);
			memset(&sa, 0, sizeof(sa));
			sa.sin6_family = AF_INET6;
#if CFG_OS == CFG_OS_MACOSX || CFG_OS == CFG_OS_WINDOWS || \
	(CFG_OS == CFG_OS_LINUX && CFG_OS_NAME == CFG_OS_NAME_ANDROID)
			sa.sin6_addr.s6_addr[0] = ip.host.quad[0] >> (utki::byte_bits * 3); // NOLINT
			sa.sin6_addr.s6_addr[1] = (ip.host.quad[0] >> (utki::byte_bits * 2)) & utki::byte_mask; // NOLINT
			sa.sin6_addr.s6_addr[2] = (ip.host.quad[0] >> utki::byte_bits) & utki::byte_mask; // NOLINT
			sa.sin6_addr.s6_addr[3] = ip.host.quad[0] & utki::byte_mask; // NOLINT
			sa.sin6_addr.s6_addr[4] = ip.host.quad[1] >> (utki::byte_bits * 3); // NOLINT
			sa.sin6_addr.s6_addr[5] = (ip.host.quad[1] >> (utki::byte_bits * 2)) & utki::byte_mask; // NOLINT
			sa.sin6_addr.s6_addr[6] = (ip.host.quad[1] >> utki::byte_bits) & utki::byte_mask; // NOLINT
			sa.sin6_addr.s6_addr[7] = ip.host.quad[1] & utki::byte_mask; // NOLINT
			sa.sin6_addr.s6_addr[8] = ip.host.quad[2] >> (utki::byte_bits * 3); // NOLINT
			sa.sin6_addr.s6_addr[9] = (ip.host.quad[2] >> (utki::byte_bits * 2)) & utki::byte_mask; // NOLINT
			sa.sin6_addr.s6_addr[10] = (ip.host.quad[2] >> utki::byte_bits) & utki::byte_mask; // NOLINT
			sa.sin6_addr.s6_addr[11] = ip.host.quad[2] & utki::byte_mask; // NOLINT
			sa.sin6_addr.s6_addr[12] = ip.host.quad[3] >> (utki::byte_bits * 3); // NOLINT
			sa.sin6_addr.s6_addr[13] = (ip.host.quad[3] >> (utki::byte_bits * 2)) & utki::byte_mask; // NOLINT
			sa.sin6_addr.s6_addr[14] = (ip.host.quad[3] >> utki::byte_bits) & utki::byte_mask; // NOLINT
			sa.sin6_addr.s6_addr[15] = ip.host.quad[3] & utki::byte_mask; // NOLINT

#else
			sa.sin6_addr.__in6_u.__u6_addr32[0] = htonl(ip.host.quad[0]); // NOLINT
			sa.sin6_addr.__in6_u.__u6_addr32[1] = htonl(ip.host.quad[1]); // NOLINT
			sa.sin6_addr.__in6_u.__u6_addr32[2] = htonl(ip.host.quad[2]); // NOLINT
			sa.sin6_addr.__in6_u.__u6_addr32[3] = htonl(ip.host.quad[3]); // NOLINT
#endif
			sa.sin6_port = htons(ip.port);
		}

		// connect to the remote host
		if (connect(
				sock,
				// NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
				reinterpret_cast<sockaddr*>(&socket_address),
				ip.host.is_v4()
					? sizeof(sockaddr_in)
					: sizeof(sockaddr_in6
					  ) // NOTE: on Mac OS for some reason the size should be exactly according to AF_INET/AF_INET6
			) == socket_error)
		{
#if CFG_OS == CFG_OS_WINDOWS
			int error_code = WSAGetLastError();
#elif CFG_OS == CFG_OS_LINUX || CFG_OS == CFG_OS_MACOSX || CFG_OS == CFG_OS_UNIX
			int error_code = errno;
#else
#	error "Unsupported OS"
#endif
			if (error_code == error_interrupted || error_code == error_in_progress) {
				// Ignore error_interrupted,
				// for non-blocking socket the connection request still should remain active.
				// Ignore error_in_progress, since we have non-blocking socket, it is not an error.
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

size_t tcp_socket::send(utki::span<const uint8_t> buf)
{
	if (this->is_empty()) {
		throw std::logic_error("tcp_socket::send(): socket is empty");
	}

#if CFG_OS == CFG_OS_WINDOWS
	int len = 0;
	socket_type& sock = this->win_sock;
#else
	ssize_t len = 0;
	int& sock = this->handle;
#endif

	while (true) {
		len = ::send(
			sock,
			// NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
			reinterpret_cast<const char*>(buf.data()),
			int(buf.size()),
#if CFG_OS == CFG_OS_WINDOWS
			0
#else
			MSG_DONTWAIT | MSG_NOSIGNAL // don't block and don't generate SIGPIPE
#endif
		);
		if (len == socket_error) {
#if CFG_OS == CFG_OS_WINDOWS
			int error_code = WSAGetLastError();
#else
			int error_code = errno;
#endif
			if (error_code == error_interrupted) {
				continue;
			} else if (error_code == error_again || error_code == error_not_connected) {
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
	int len = 0;
	socket_type& sock = this->win_sock;
#else
	ssize_t len = 0;
	int& sock = this->handle;
#endif

	while (true) {
		len = ::recv(
			sock,
			// NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
			reinterpret_cast<char*>(buf.data()),
			int(buf.size()),
#if CFG_OS == CFG_OS_WINDOWS
			0
#else
			MSG_DONTWAIT // don't block
#endif
		);
		if (len == socket_error) {
#if CFG_OS == CFG_OS_WINDOWS
			int error_code = WSAGetLastError();
#else
			int error_code = errno;
#endif

			if (error_code == error_interrupted) {
				continue;
			} else if (error_code == error_again || error_code == error_not_connected) {
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

void tcp_socket::disconnect()
{
	if (this->is_empty()) {
		return;
	}

#if CFG_OS == CFG_OS_WINDOWS
	shutdown(this->win_sock, SD_BOTH);
#elif CFG_OS == CFG_OS_LINUX || CFG_OS == CFG_OS_MACOSX || CFG_OS == CFG_OS_UNIX
	shutdown(this->handle, SHUT_RDWR);
#else
#	error "Unsupported OS"
#endif
}

namespace {
address make_ip_address(const sockaddr_storage& addr)
{
	if (addr.ss_family == AF_INET) {
		// NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
		const auto& a = reinterpret_cast<const sockaddr_in&>(addr);
		return {uint32_t(ntohl(a.sin_addr.s_addr)), uint16_t(ntohs(a.sin_port))};
	} else {
		ASSERT(addr.ss_family == AF_INET6)

		// NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
		const auto& a = reinterpret_cast<const sockaddr_in6&>(addr);

		return {
			address::ip(
#if CFG_OS == CFG_OS_MACOSX || CFG_OS == CFG_OS_WINDOWS || \
	(CFG_OS == CFG_OS_LINUX && CFG_OS_NAME == CFG_OS_NAME_ANDROID)
				(uint32_t(a.sin6_addr.s6_addr[0]) << (utki::byte_bits * 3)) // NOLINT
					| (uint32_t(a.sin6_addr.s6_addr[1]) << (utki::byte_bits * 2)) // NOLINT
					| (uint32_t(a.sin6_addr.s6_addr[2]) << utki::byte_bits) // NOLINT
					| uint32_t(a.sin6_addr.s6_addr[3]), // NOLINT
				(uint32_t(a.sin6_addr.s6_addr[4]) << (utki::byte_bits * 3)) // NOLINT
					| (uint32_t(a.sin6_addr.s6_addr[5]) << (utki::byte_bits * 2)) // NOLINT
					| (uint32_t(a.sin6_addr.s6_addr[6]) << utki::byte_bits) // NOLINT
					| uint32_t(a.sin6_addr.s6_addr[7]), // NOLINT
				(uint32_t(a.sin6_addr.s6_addr[8]) << (utki::byte_bits * 3)) // NOLINT
					| (uint32_t(a.sin6_addr.s6_addr[9]) << (utki::byte_bits * 2)) // NOLINT
					| (uint32_t(a.sin6_addr.s6_addr[10]) << utki::byte_bits) // NOLINT
					| uint32_t(a.sin6_addr.s6_addr[11]), // NOLINT
				(uint32_t(a.sin6_addr.s6_addr[12]) << (utki::byte_bits * 3)) // NOLINT
					| (uint32_t(a.sin6_addr.s6_addr[13]) << (utki::byte_bits * 2)) // NOLINT
					| (uint32_t(a.sin6_addr.s6_addr[14]) << utki::byte_bits) // NOLINT
					| uint32_t(a.sin6_addr.s6_addr[15]) // NOLINT
#else
				uint32_t(ntohl(a.sin6_addr.__in6_u.__u6_addr32[0])), // NOLINT
				uint32_t(ntohl(a.sin6_addr.__in6_u.__u6_addr32[1])), // NOLINT
				uint32_t(ntohl(a.sin6_addr.__in6_u.__u6_addr32[2])), // NOLINT
				uint32_t(ntohl(a.sin6_addr.__in6_u.__u6_addr32[3])) // NOLINT
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

	sockaddr_storage addr{};

#if CFG_OS == CFG_OS_WINDOWS
	int len = sizeof(addr);
	socket_type& sock = this->win_sock;
#else
	socklen_t len = sizeof(addr);
	int& sock = this->handle;
#endif

	// NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
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

	sockaddr_storage addr{};

#if CFG_OS == CFG_OS_WINDOWS
	int len = sizeof(addr);
	socket_type& sock = this->win_sock;
#else
	socklen_t len = sizeof(addr);
	int& sock = this->handle;
#endif

	// NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
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
