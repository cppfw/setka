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

#include "udp_socket.hpp"

#include <cstring>
#include <limits>

#if CFG_OS == CFG_OS_LINUX || CFG_OS == CFG_OS_MACOSX || CFG_OS == CFG_OS_UNIX
#	include <netinet/in.h>
#endif

using namespace setka;

udp_socket::udp_socket(uint16_t port) :
	ipv4(false)
{
#if CFG_OS == CFG_OS_WINDOWS
	this->create_event_for_waitable();
	socket_type& sock = this->win_sock;
#else
	int& sock = this->handle;
#endif

	sock = ::socket(PF_INET6, SOCK_DGRAM, 0);

	if (sock == invalid_socket) {
		// maybe IPv6 is not supported by OS, try to proceed with IPv4 socket then
		sock = ::socket(PF_INET, SOCK_DGRAM, 0);

		if (sock == invalid_socket) {
#if CFG_OS == CFG_OS_WINDOWS
			int error_code = WSAGetLastError();
			this->close_event_for_waitable();
#else
			int error_code = errno;
#endif
			throw std::system_error(error_code, std::generic_category(), "couldn't create socket, socket() failed");
		}

		this->ipv4 = true;
	}

	// turn off IPv6 only mode to allow also accepting IPv4 connections
	if (!this->ipv4) {
#if CFG_OS == CFG_OS_WINDOWS
		char no = 0;
		const char* no_ptr = &no;
#else
		int no = 0;
		void* no_ptr = &no;
#endif
		if (setsockopt(sock, IPPROTO_IPV6, IPV6_V6ONLY, no_ptr, sizeof(no)) != 0) {
			// dual stack is not supported, proceed with IPv4 only

			this->close(); // close IPv6 socket

			// create IPv4 socket

#if CFG_OS == CFG_OS_WINDOWS
			this->create_event_for_waitable();
#endif

			sock = ::socket(PF_INET, SOCK_DGRAM, 0);

			if (sock == invalid_socket) {
#if CFG_OS == CFG_OS_WINDOWS
				int error_code = WSAGetLastError();
				this->close_event_for_waitable();
#else
				int error_code = errno;
#endif
				throw std::system_error(error_code, std::generic_category(), "couldn't create socket, socket() failed");
			}

			this->ipv4 = true;
		}
	}

	try {
		// bind locally, if appropriate
		if (port != 0) {
			sockaddr_storage socket_address{};
			socklen_t socket_address_length = 0;

			if (this->ipv4) {
				// NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
				auto& sa = reinterpret_cast<sockaddr_in&>(socket_address);
				memset(&sa, 0, sizeof(sa));
				sa.sin_family = AF_INET;
				sa.sin_addr.s_addr = INADDR_ANY;
				sa.sin_port = htons(port);
				socket_address_length = sizeof(sa);
			} else {
				// NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
				auto& sa = reinterpret_cast<sockaddr_in6&>(socket_address);
				memset(&sa, 0, sizeof(sa));
				sa.sin6_family = AF_INET6;
				sa.sin6_addr = in6addr_any; // 'in6addr_any' allows accepting both IPv4 and IPv6 connections
				sa.sin6_port = htons(port);
				socket_address_length = sizeof(sa);
			}

			// bind the socket for listening
			if (::bind(
					sock,
					// NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
					reinterpret_cast<struct sockaddr*>(&socket_address),
					socket_address_length
				) == socket_error)
			{
#if CFG_OS == CFG_OS_WINDOWS
				int error_code = WSAGetLastError();
#else
				int error_code = errno;
#endif
				throw std::system_error(
					error_code,
					std::generic_category(),
					"could not bind socket to network address, bind() failed"
				);
			}
		}

		this->set_nonblocking_mode();

		// allow broadcasting
#if CFG_OS == CFG_OS_WINDOWS || CFG_OS == CFG_OS_LINUX || CFG_OS == CFG_OS_MACOSX || CFG_OS == CFG_OS_UNIX
		{
			int yes = 1;
			if (setsockopt(
					sock,
					SOL_SOCKET,
					SO_BROADCAST,
					// NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
					reinterpret_cast<char*>(&yes),
					sizeof(yes)
				) == socket_error)
			{
#	if CFG_OS == CFG_OS_WINDOWS
				int error_code = WSAGetLastError();
#	else
				int error_code = errno;
#	endif
				throw std::system_error(
					error_code,
					std::generic_category(),
					"could not set broadcast option, setsockopt() failed"
				);
			}
		}
#else
#	error "Unsupported OS"
#endif
	} catch (...) {
		this->close();
		throw;
	}
}

size_t udp_socket::send(utki::span<const uint8_t> buf, const address& destination_address)
{
	if (this->is_empty()) {
		throw std::logic_error("udp_socket::send(): socket is empty");
	}

	sockaddr_storage socket_address{};
	socklen_t socket_address_length = 0;

	if(
#if CFG_OS == CFG_OS_MACOSX || CFG_OS == CFG_OS_WINDOWS
			this->ipv4 &&
#endif
			destination_address.host.is_v4()
		)
	{
		// NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
		auto& a = reinterpret_cast<sockaddr_in&>(socket_address);
		memset(&a, 0, sizeof(a));
		a.sin_family = AF_INET;
		a.sin_addr.s_addr = htonl(destination_address.host.get_v4());
		a.sin_port = htons(destination_address.port);
		socket_address_length = sizeof(a);
	} else {
		// NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
		auto& a = reinterpret_cast<sockaddr_in6&>(socket_address);
		memset(&a, 0, sizeof(a));
		a.sin6_family = AF_INET6;
#if CFG_OS == CFG_OS_MACOSX || CFG_OS == CFG_OS_WINDOWS || \
	(CFG_OS == CFG_OS_LINUX && CFG_OS_NAME == CFG_OS_NAME_ANDROID)
		// NOLINTNEXTLINE
		a.sin6_addr.s6_addr[0] = destination_address.host.quad[0] >> (utki::byte_bits * 3);
		// NOLINTNEXTLINE
		a.sin6_addr.s6_addr[1] = (destination_address.host.quad[0] >> (utki::byte_bits * 2)) & utki::byte_mask;
		// NOLINTNEXTLINE
		a.sin6_addr.s6_addr[2] = (destination_address.host.quad[0] >> utki::byte_bits) & utki::byte_mask;
		// NOLINTNEXTLINE
		a.sin6_addr.s6_addr[3] = destination_address.host.quad[0] & utki::byte_mask;
		// NOLINTNEXTLINE
		a.sin6_addr.s6_addr[4] = destination_address.host.quad[1] >> (utki::byte_bits * 3);
		// NOLINTNEXTLINE
		a.sin6_addr.s6_addr[5] = (destination_address.host.quad[1] >> (utki::byte_bits * 2)) & utki::byte_mask;
		// NOLINTNEXTLINE
		a.sin6_addr.s6_addr[6] = (destination_address.host.quad[1] >> utki::byte_bits) & utki::byte_mask;
		// NOLINTNEXTLINE
		a.sin6_addr.s6_addr[7] = destination_address.host.quad[1] & utki::byte_mask;
		// NOLINTNEXTLINE
		a.sin6_addr.s6_addr[8] = destination_address.host.quad[2] >> (utki::byte_bits * 3);
		// NOLINTNEXTLINE
		a.sin6_addr.s6_addr[9] = (destination_address.host.quad[2] >> (utki::byte_bits * 2)) & utki::byte_mask;
		// NOLINTNEXTLINE
		a.sin6_addr.s6_addr[10] = (destination_address.host.quad[2] >> utki::byte_bits) & utki::byte_mask;
		// NOLINTNEXTLINE
		a.sin6_addr.s6_addr[11] = destination_address.host.quad[2] & utki::byte_mask;
		// NOLINTNEXTLINE
		a.sin6_addr.s6_addr[12] = destination_address.host.quad[3] >> (utki::byte_bits * 3);
		// NOLINTNEXTLINE
		a.sin6_addr.s6_addr[13] = (destination_address.host.quad[3] >> (utki::byte_bits * 2)) & utki::byte_mask;
		// NOLINTNEXTLINE
		a.sin6_addr.s6_addr[14] = (destination_address.host.quad[3] >> utki::byte_bits) & utki::byte_mask;
		// NOLINTNEXTLINE
		a.sin6_addr.s6_addr[15] = destination_address.host.quad[3] & utki::byte_mask;
#else
		a.sin6_addr.__in6_u.__u6_addr32[0] = htonl(destination_address.host.quad[0]); // NOLINT
		a.sin6_addr.__in6_u.__u6_addr32[1] = htonl(destination_address.host.quad[1]); // NOLINT
		a.sin6_addr.__in6_u.__u6_addr32[2] = htonl(destination_address.host.quad[2]); // NOLINT
		a.sin6_addr.__in6_u.__u6_addr32[3] = htonl(destination_address.host.quad[3]); // NOLINT
#endif
		a.sin6_port = htons(destination_address.port);
		socket_address_length = sizeof(a);
	}

#if CFG_OS == CFG_OS_WINDOWS
	int len = 0;
	socket_type& sock = this->win_sock;
#else
	ssize_t len = 0;
	int& sock = this->handle;
#endif

	while (true) {
		len = ::sendto(
			sock,
			// NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
			reinterpret_cast<const char*>(buf.data()),
			int(buf.size()),
			0,
			// NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
			reinterpret_cast<struct sockaddr*>(&socket_address),
			socket_address_length
		);

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
					"could not send data over UDP, sendto() failed"
				);
			}
		}
		break;
	}

	ASSERT(buf.size() <= size_t(std::numeric_limits<int>::max()))
	ASSERT(len <= int(buf.size()), [&](auto& o) {
		o << "res = " << len;
	})
	ASSERT((len == int(buf.size())) || (len == 0), [&](auto& o) {
		o << "res = " << len;
	})

	ASSERT(len >= 0)
	return size_t(len);
}

size_t udp_socket::recieve(utki::span<uint8_t> buf, address& out_sender_address)
{
	if (this->is_empty()) {
		throw std::logic_error("udp_socket::recieve(): socket is empty");
	}

	sockaddr_storage socket_address{};

#if CFG_OS == CFG_OS_WINDOWS
	int socket_address_length = sizeof(socket_address);
	int len = 0;
	socket_type& sock = this->win_sock;
#elif CFG_OS == CFG_OS_LINUX || CFG_OS == CFG_OS_MACOSX || CFG_OS == CFG_OS_UNIX
	socklen_t socket_address_length = sizeof(socket_address);
	ssize_t len = 0;
	int& sock = this->handle;
#else
#	error "Unsupported OS"
#endif

	while (true) {
		len = ::recvfrom(
			sock,
			// NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
			reinterpret_cast<char*>(buf.data()),
			int(buf.size()),
			0,
			// NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
			reinterpret_cast<sockaddr*>(&socket_address),
			&socket_address_length
		);

		if (len == socket_error) {
#if CFG_OS == CFG_OS_WINDOWS
			int error_code = WSAGetLastError();
#else
			int error_code = errno;
#endif
			if (error_code == error_interrupted) {
				continue;
			} else if (error_code == error_again) {
				return 0; // no data available, return 0 bytes received
			} else {
				throw std::system_error(
					error_code,
					std::generic_category(),
					"could not receive data over UDP, recvfrom() failed"
				);
			}
		}
		break;
	}

	ASSERT(buf.size() <= size_t(std::numeric_limits<int>::max()))
	ASSERT(len <= int(buf.size()), [&](auto& o) {
		o << "len = " << len;
	})

	if (socket_address.ss_family == AF_INET) {
		// NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
		auto& a = reinterpret_cast<sockaddr_in&>(socket_address);
		out_sender_address = address(ntohl(a.sin_addr.s_addr), uint16_t(ntohs(a.sin_port)));
	} else {
		ASSERT(socket_address.ss_family == AF_INET6, [&](auto& o) {
			o << "socket_address.ss_family = " << unsigned(socket_address.ss_family) << " AF_INET = " << AF_INET
			  << " AF_INET6 = " << AF_INET6;
		})
		// NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
		auto& a = reinterpret_cast<sockaddr_in6&>(socket_address);
		out_sender_address = address(
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
		);
	}

	ASSERT(len >= 0)
	return size_t(len);
}

#if CFG_OS == CFG_OS_WINDOWS
void udp_socket::set_waiting_flags(utki::flags<opros::ready> waiting_flags)
{
	long flags = FD_CLOSE;
	if (waiting_flags.get(opros::ready::read)) {
		flags |= FD_READ;
	}
	if (waiting_flags.get(opros::ready::write)) {
		flags |= FD_WRITE;
	}
	this->set_waiting_events_for_windows(flags);
}
#endif
