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

#include "address.hpp"

#include <sstream>

#include <utki/config.hpp>
#include <utki/span.hpp>
#include <utki/string.hpp>

#if CFG_OS == CFG_OS_LINUX || CFG_OS == CFG_OS_MACOSX
#	include <arpa/inet.h>
#elif CFG_OS == CFG_OS_WINDOWS
#	include <Ws2tcpip.h>
#else
#	error "Unknown OS"
#endif

#ifdef max
#	undef max
#endif

using namespace setka;

namespace {
bool is_ip_v4_string(const char* str)
{
	// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
	for (const char* p = str; *p != 0; ++p) {
		if (*p == '.') {
			return true;
		}
		if (*p == ':') {
			return false;
		}
	}
	return false;
}
} // namespace

address::ip address::ip::parse(const char* str)
{
	if (is_ip_v4_string(str)) {
		return ip::parse_v4(str);
	} else {
		return ip::parse_v6(str);
	}
}

address::ip address::ip::parse_v4(const char* str)
{
	sockaddr_in a{};

#if CFG_OS == CFG_OS_LINUX || CFG_OS == CFG_OS_MACOSX || CFG_OS == CFG_OS_WINDOWS
	int res = inet_pton(AF_INET, str, &a.sin_addr);

	if (res != 1) {
		throw std::runtime_error("bad IP address format");
	}

#else
#	error "Unknown OS"
#endif

	return {ntohl(a.sin_addr.s_addr)};
}

address::ip address::ip::parse_v6(const char* str)
{
#if CFG_OS == CFG_OS_WINDOWS
	sockaddr_in6 aa{};
	in6_addr& a = aa.sin6_addr;
#else
	in6_addr a{};
#endif

#if CFG_OS == CFG_OS_LINUX || CFG_OS == CFG_OS_MACOSX || CFG_OS == CFG_OS_WINDOWS
	int res = inet_pton(AF_INET6, str, &a);

	if (res != 1) {
		throw std::runtime_error("bad IP address format");
	}

#else
#	error "Unknown OS"
#endif

#if CFG_OS == CFG_OS_MACOSX || CFG_OS == CFG_OS_WINDOWS || \
	(CFG_OS == CFG_OS_LINUX && CFG_OS_NAME == CFG_OS_NAME_ANDROID)
	return {
		a.s6_addr[0],
		a.s6_addr[1],
		a.s6_addr[2],
		a.s6_addr[3],
		a.s6_addr[4],
		a.s6_addr[5], // NOLINT
		a.s6_addr[6], // NOLINT
		a.s6_addr[7], // NOLINT
		a.s6_addr[8], // NOLINT
		a.s6_addr[9], // NOLINT
		a.s6_addr[10], // NOLINT
		a.s6_addr[11], // NOLINT
		a.s6_addr[12], // NOLINT
		a.s6_addr[13], // NOLINT
		a.s6_addr[14], // NOLINT
		a.s6_addr[15] // NOLINT
	};
#else
	return {
		a.__in6_u.__u6_addr8[0], // NOLINT
		a.__in6_u.__u6_addr8[1], // NOLINT
		a.__in6_u.__u6_addr8[2], // NOLINT
		a.__in6_u.__u6_addr8[3], // NOLINT
		a.__in6_u.__u6_addr8[4], // NOLINT
		a.__in6_u.__u6_addr8[5], // NOLINT
		a.__in6_u.__u6_addr8[6], // NOLINT
		a.__in6_u.__u6_addr8[7], // NOLINT
		a.__in6_u.__u6_addr8[8], // NOLINT
		a.__in6_u.__u6_addr8[9], // NOLINT
		a.__in6_u.__u6_addr8[10], // NOLINT
		a.__in6_u.__u6_addr8[11], // NOLINT
		a.__in6_u.__u6_addr8[12], // NOLINT
		a.__in6_u.__u6_addr8[13], // NOLINT
		a.__in6_u.__u6_addr8[14], // NOLINT
		a.__in6_u.__u6_addr8[15] // NOLINT
	};
#endif
}

address::address(const char* host_str, uint16_t p) :
	host(address::ip::parse(host_str)),
	port(p)
{}

address::address(const char* str)
{
	if (*str == 0) { // if zero length string
		throw std::runtime_error("bad IP address format");
	}

	if (*str == '[') { // IPv6 with port
		// NOLINTNEXTLINE
		std::array<char, (4 * 6 + 6 + (3 * 4 + 3) + 1)> buf;

		// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
		++str;

		// NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg)
		char* dst = nullptr;
		// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
		for (dst = buf.data(); *str != ']'; ++dst, ++str) {
			// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
			if (*str == 0 || !utki::make_span(buf).overlaps(dst + 1)) {
				throw std::runtime_error("bad IP address format");
			}

			*dst = *str;
		}

		ASSERT(utki::make_span(buf).overlaps(dst))
		*dst = 0; // null-terminate

		this->host = ip::parse_v6(buf.data());

		// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
		++str; // move to port ':' separator
	} else {
		// IPv4 or IPv6 without port

		if (is_ip_v4_string(str)) {
			// NOLINTNEXTLINE(cppcoreguidelines-pro-type-member-init)
			std::array<char, (3 * 4 + 3 + 1)> buf;

			// NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg)
			char* dst = nullptr;
			// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
			for (dst = buf.data(); *str != ':' && *str != 0; ++dst, ++str) {
				// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
				if (!utki::make_span(buf).overlaps(dst + 1)) {
					throw std::runtime_error("bad IP address format");
				}

				*dst = *str;
			}

			ASSERT(utki::make_span(buf).overlaps(dst))
			*dst = 0; // null-terminate

			this->host = ip::parse_v4(buf.data());
		} else {
			// IPv6 without port
			this->host = ip::parse_v6(str);
			this->port = 0;
			return;
		}
	}

	// parse port

	if (*str != ':') {
		if (*str == 0) {
			this->port = 0;
			return;
		} else {
			//		TRACE(<< "no colon, *ip = " << (*ip) << std::endl)
			throw std::runtime_error("bad IP address format");
		}
	}

	// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
	++str;

	constexpr auto max_port_digits = 5;

	// move to the end of port number, maximum 5 digits.
	// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
	for (unsigned i = 0; '0' <= *str && *str <= '9' && i < max_port_digits; ++i, ++str) {
	}
	if ('0' <= *str && *str <= '9') { // if still have one more digit
		//		TRACE(<< "still have one more digit" << std::endl)
		throw std::runtime_error("bad IP address format");
	}

	// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
	--str;

	uint32_t port = 0;

	// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
	for (unsigned i = 0; *str != ':'; ++i, --str) {
		uint32_t pow = 1;
		for (unsigned j = 0; j < i; ++j) {
			pow *= utki::to_int(utki::integer_base::dec);
		}

		ASSERT('0' <= *str && *str <= '9')

		port += (*str - '0') * pow;
	}

	if (port > std::numeric_limits<uint16_t>::max()) {
		//		TRACE(<< "port number is bigger than 0xffff" << std::endl)
		throw std::runtime_error("bad IP address format");
	}

	this->port = uint16_t(port);
}

std::string address::ip::to_string() const
{
	std::stringstream ss;
	if (this->is_v4()) {
		for (unsigned i = 4;;) {
			--i;
			ss << (((this->get_v4()) >> (utki::byte_bits * i)) & utki::byte_mask);
			if (i == 0) {
				break;
			}
			ss << '.';
		}
	} else {
		constexpr auto num_ip_v6_parts = 8;

		ss << std::hex;
		for (unsigned i = num_ip_v6_parts;;) {
			--i;
			// NOLINTNEXTLINE(cppcoreguidelines-avoid-magic-numbers, cppcoreguidelines-pro-bounds-constant-array-index)
			ss << ((this->quad[(i * 2) / 4] >> (16 * (i % 2))) & 0xffff);
			if (i == 0) {
				break;
			}
			ss << ':';
		}
	}
	return ss.str();
}
