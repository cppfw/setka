/* The MIT License:

Copyright (c) 2009-2013 Ivan Gagis <igagis@gmail.com>

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE. */

// Home page: http://ting.googlecode.com



/**
 * @author Ivan Gagis <igagis@gmail.com>
 */

#pragma once


#include <string>

#include "Exc.hpp"

#include "../types.hpp"



namespace ting{
namespace net{



/**
 * @brief a structure which holds IP address.
 * IP address consists of IP host address and an IP port.
 */
class IPAddress{
public:
	/**
	 * @brief Bad IP address format error.
	 * This exception is thrown when trying to parse and IP address from string and
	 * that string does not contain a valid IP address.
	 */
	class BadIPAddressFormatExc : public ting::net::Exc{
	public:
		BadIPAddressFormatExc(){}
	};
	
	/**
	 * @brief IP host address.
	 * This class encapsulates an IP address.
	 * The address is IPv6. IPv4 addresses are represented as IPv4 mapped to IPv6 addresses.
	 */
	class Host{
		std::uint32_t host[4];//IPv6 address
	public:
		
		/**
		 * @brief 0th quad of IPv6 address.
		 * For example, if address is 1234:5678:9345:4243::2342, then
		 * The return value will be 0x12345678.
		 * @return 32 bit value, zeroth quad of IPv6 address.
		 */
		inline std::uint32_t Quad0()const noexcept{
			return this->host[0];
		}
		
		/**
		 * @brief 1st quad of IPv6 address.
		 * For example, if address is 1234:5678:9345:4243::2342, then
		 * The return value will be 0x93454243.
		 * @return 32 bit value, first quad of IPv6 address.
		 */
		inline std::uint32_t Quad1()const noexcept{
			return this->host[1];
		}
		
		/**
		 * @brief 2nd quad of IPv6 address.
		 * For example, if address is 1234:5678:9345:4243:2222:3333:1111:2342, then
		 * The return value will be 0x22223333.
		 * @return 32 bit value, second quad of IPv6 address.
		 */
		inline std::uint32_t Quad2()const noexcept{
			return this->host[2];
		}
		
		/**
		 * @brief 3rd quad of IPv6 address.
		 * For example, if address is 1234:5678:9345:4243:2222:3333:1111:2342, then
		 * The return value will be 0x11112342.
		 * @return 32 bit value, third quad of IPv6 address.
		 */
		inline std::uint32_t Quad3()const noexcept{
			return this->host[3];
		}
		
		/**
		 * @brief Initialize to given quads.
		 * Initialize this Host object using given quads.
		 * @param q0 - zeroth quad.
		 * @param q1 - first quad.
		 * @param q2 - second quad.
		 * @param q3 - third quad.
		 */
		inline void Init(std::uint32_t q0, std::uint32_t q1, std::uint32_t q2, std::uint32_t q3)noexcept{
			this->host[0] = q0;
			this->host[1] = q1;
			this->host[2] = q2;
			this->host[3] = q3;
		}
		
		/**
		 * @brief Initialize to given IPv4 address.
		 * Initializes this Host object to a IPv6 mapped IPv4 address.
		 * @param h - IPv4 host address.
		 */
		inline void Init(std::uint32_t h)noexcept{
			this->Init(0, 0, 0xffff, h);
		}
		
		/**
		 * @brief Initialize to given IPv6 numbers.
		 * @param a0 - zeroth number.
		 * @param a1 - first number.
		 * @param a2 - second number.
		 * @param a3 - third number.
		 * @param a4 - fourth number.
		 * @param a5 - fifth number.
		 * @param a6 - sixth number.
		 * @param a7 - sevens number.
		 */
		inline void Init(std::uint16_t a0, std::uint16_t a1, std::uint16_t a2, std::uint16_t a3, std::uint16_t a4, std::uint16_t a5, std::uint16_t a6, std::uint16_t a7)noexcept{
			this->Init(
					(std::uint32_t(a0) << 16) | std::uint32_t(a1),
					(std::uint32_t(a2) << 16) | std::uint32_t(a3),
					(std::uint32_t(a4) << 16) | std::uint32_t(a5),
					(std::uint32_t(a6) << 16) | std::uint32_t(a7)
				);
		}
		
		/**
		 * @brief Initialize to given bytes.
		 */
		inline void Init(std::uint8_t a0, std::uint8_t a1, std::uint8_t a2, std::uint8_t a3, std::uint8_t a4, std::uint8_t a5, std::uint8_t a6, std::uint8_t a7, std::uint8_t a8, std::uint8_t a9, std::uint8_t a10, std::uint8_t a11, std::uint8_t a12, std::uint8_t a13, std::uint8_t a14, std::uint8_t a15)noexcept{
			this->Init(
					(std::uint16_t(a0) << 8) | std::uint16_t(a1),
					(std::uint16_t(a2) << 8) | std::uint16_t(a3),
					(std::uint16_t(a4) << 8) | std::uint16_t(a5),
					(std::uint16_t(a6) << 8) | std::uint16_t(a7),
					(std::uint16_t(a8) << 8) | std::uint16_t(a9),
					(std::uint16_t(a10) << 8) | std::uint16_t(a11),
					(std::uint16_t(a12) << 8) | std::uint16_t(a13),
					(std::uint16_t(a14) << 8) | std::uint16_t(a15)
				);
		}
	
		/**
		 * @brief Bad IP host address format error.
		 * This exception is thrown when trying to parse and IP host address from string and
		 * that string does not contain a valid IP address.
		 */
		class BadIPHostFormatExc : public BadIPAddressFormatExc{
		public:
			BadIPHostFormatExc(){}
		};
		
		/**
		 * @brief Creates an undefined Host object.
		 */
		Host()noexcept{}
		
		/**
		 * @brief Creates a host object initialized to IPv6 mapped IPv4 using given IPv4.
		 * @param h - IPv4 host to use for initialization.
		 */
		Host(std::uint32_t h)noexcept{
			this->Init(h);
		}
		
		/**
		 * @brief Creates a Host object using given IPv6 quads.
		 */
		inline Host(std::uint32_t q0, std::uint32_t q1, std::uint32_t q2, std::uint32_t q3)noexcept{
			this->Init(q0, q1, q2, q3);
		}
		
		/**
		 * @brief Creates a Host object using given IPv6 numbers.
         */
		inline Host(std::uint16_t a0, std::uint16_t a1, std::uint16_t a2, std::uint16_t a3, std::uint16_t a4, std::uint16_t a5, std::uint16_t a6, std::uint16_t a7)noexcept{
			this->Init(a0, a1, a2, a3, a4, a5, a6, a7);
		}
		
		/**
		 * @brief Creates a Host object using IPv6 bytes.
         */
		inline Host(std::uint8_t a0, std::uint8_t a1, std::uint8_t a2, std::uint8_t a3, std::uint8_t a4, std::uint8_t a5, std::uint8_t a6, std::uint8_t a7, std::uint8_t a8, std::uint8_t a9, std::uint8_t a10, std::uint8_t a11, std::uint8_t a12, std::uint8_t a13, std::uint8_t a14, std::uint8_t a15)noexcept{
			this->Init(a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15);
		}
		
		/**
		 * @brief Parse host from string.
		 * String may contain either IPv4 or IPv6 address.
         * @param ip - string containing IP host address.
         * @return Host object initialized to a parsed address.
		 * @throw BadIPHostFormatExc if string does not contain well formed IPv4 or IPv6 host address.
         */
		static Host Parse(const char* ip);
		
		/**
		 * @brief Parse IPv4 from string.
		 * String may contain only IPv4 address.
         * @param ip - string containing IPv4 host address.
         * @return Host object initialized to a parsed address.
		 * @throw BadIPHostFormatExc if string does not contain well formed IPv4 host address.
         */
		static Host ParseIPv4(const char* ip);
		
		/**
		 * @brief Parse IPv6 from string.
		 * String may contain only IPv6 address.
         * @param ip - string containing IPv6 host address.
         * @return Host object initialized to a parsed address.
		 * @throw BadIPHostFormatExc if string does not contain well formed IPv6 host address.
         */
		static Host ParseIPv6(const char* ip);
		
		/**
		 * @brief Check if it is a IPv4 mapped to IPv6.
         * @return true if this Host object holds IPv4 address mapped to IPv6.
		 * @return false otherwise.
         */
		inline bool IsIPv4()const noexcept{
			return this->host[2] == 0xffff && this->host[1] == 0 && this->host[0] == 0;
		}
		
		/**
		 * @brief Get IPv4 address.
         * @return IPv4 host if this is a IPv4 mapped to IPv6.
		 * @return undefined value otherwise.
         */
		inline std::uint32_t IPv4Host()const noexcept{
			return this->host[3];
		}
		
		/**
		 * @brief Check if the IP host address is valid.
		 * Checks if this IP address is not an invalid address, which is all zeroes.
         * @return true if this IP address is not a zero address.
		 * @return false if this IP address is all zeroes.
         */
		inline bool IsValid()const noexcept{
			if(this->IsIPv4()){
				return this->IPv4Host() != 0;
			}
			
			return this->host[3] != 0 || this->host[2] != 0 || this->host[1] != 0 || this->host[0] != 0;
		}
		
		/**
		 * @brief Compare two IP host addresses.
         * @param h - IP host address to compare this IP host address to.
         * @return true if two IP addresses are identical.
		 * @return false otherwise.
         */
		inline bool operator==(const Host& h){
			return (this->host[0] == h.host[0])
					&& (this->host[1] == h.host[1])
					&& (this->host[2] == h.host[2])
					&& (this->host[3] == h.host[3])
				;
		}
		
		/**
		 * @brief Convert this IP host address to string.
         * @return String representing an IP host address.
         */
		std::string ToString()const;
	};
	
	Host host;///< IPv6 address
	std::uint16_t port;///< IP port number
	
	/**
	 * @brief Construct IP address with undefined host and port.
     */
	inline IPAddress()noexcept{}

	/**
	 * @brief Create IPv4-address specifying exact IP-address and port number.
	 * @param h - IPv4 address. For example, 0x7f000001 represents "127.0.0.1" IP address value.
	 * @param p - IP port number.
	 */
	inline IPAddress(std::uint32_t h, std::uint16_t p)noexcept :
			host(h),
			port(p)
	{}

	/**
	 * @brief Create IPv4-address specifying exact IP-address as 4 bytes and port number.
	 * The IPv4-address can be specified as 4 separate byte values, for example:
	 * @code
	 * ting::net::IPAddress ip(127, 0, 0, 1, 80); //"127.0.0.1" port 80
	 * @endcode
	 * @param h1 - 1st triplet of IPv4 address.
	 * @param h2 - 2nd triplet of IPv4 address.
	 * @param h3 - 3rd triplet of IPv4 address.
	 * @param h4 - 4th triplet of IPv4 address.
	 * @param p - IP port number.
	 */
	inline IPAddress(std::uint8_t h1, std::uint8_t h2, std::uint8_t h3, std::uint8_t h4, std::uint16_t p)noexcept :
			host((std::uint32_t(h1) << 24) + (std::uint32_t(h2) << 16) + (std::uint32_t(h3) << 8) + std::uint32_t(h4)),
			port(p)
	{}

	/**
	 * @brief Construct IP address from given host and port.
     * @param h - host to use for construction.
     * @param p - port to use for construction.
     */
	inline IPAddress(Host h, std::uint16_t p)noexcept :
			host(h),
			port(p)
	{}
	
	/**
	 * @brief Create IP address specifying IP host address as string and port number.
	 * The string passed as argument should contain properly formatted IPv4 or IPv6 host address.
	 * @param ip - IPv4 or IPv6 host address null-terminated string. Example: "127.0.0.1".
	 * @param p - IP port number.
	 * @throw BadIPAddressFormatExc - when passed string does not contain properly formatted IP address.
	 */
	IPAddress(const char* ip, std::uint16_t p);
	
	/**
	 * @brief Create IP address specifying IP host address and IP port as string.
	 * The string passed for parsing should contain the IP host address with the port number.
	 * If there is no port number specified after the IP-address the format of the IP-address
	 * is regarded as invalid and corresponding exception is thrown.
     * @param ip - null-terminated string representing IP address with port number, e.g. "127.0.0.1:80" or "[42f4:234a::23]:432".
	 * @throw BadIPAddressFormatExc - when passed string does not contain properly formatted IP-address.
     */
	IPAddress(const char* ip);

	/**
	 * @brief compares two IP addresses for equality.
	 * @param ip - IP address to compare with.
	 * @return true if hosts and ports of the two IP addresses are equal accordingly.
	 * @return false otherwise.
	 */
	inline bool operator==(const IPAddress& ip){
		return (this->host == ip.host) && (this->port == ip.port);
	}
};//~class IPAddress



}//~namespace
}//~namespace
