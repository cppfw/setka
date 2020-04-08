#pragma once

#include <string>
#include <array>

#include <utki/config.hpp>

#include "Exc.hpp"

namespace setka{

/**
 * @brief a structure which holds IP address.
 * IP address consists of IP host address and an IP port.
 */
class ip_address{
public:
	/**
	 * @brief IP host address.
	 * This class encapsulates an IP address.
	 * The address is IPv6. IPv4 addresses are represented as IPv4 mapped to IPv6 addresses.
	 */
	class ip{
	public:
		/**
		 * @brief Quads of the IPv6 address.
		 * For example, if address is 1234:5678:9345:4243:2222:3333:1111:2342, then
		 * quad[0] = 0x12345678, quad[1] = 0x93454243, quad[2] = 0x22223333, quad[3] = 0x11112342.
		 */
		std::array<uint32_t, 4> quad;
		
		/**
		 * @brief Creates an undefined ip object.
		 */
		ip()noexcept{}
		
		/**
		 * @brief Creates a ip object using given IPv6 quads.
		 * Construct and initialize the object using given quads.
		 * @param q0 - zeroth quad.
		 * @param q1 - first quad.
		 * @param q2 - second quad.
		 * @param q3 - third quad.
		 */
		ip(uint32_t q0, uint32_t q1, uint32_t q2, uint32_t q3) :
				quad({{q0, q1, q2, q3}})
		{}

		/**
		 * @brief Creates a host object initialized to IPv6 mapped IPv4 using given IPv4.
		 * Construct and initialize the object to an IPv6 mapped IPv4 address.
		 * @param h - IPv4 host to use for initialization.
		 */
		ip(uint32_t h) :
				ip(0, 0, 0xffff, h)
		{}
		
		/**
		 * @brief Creates a ip object using given IPv6 numbers.
		 * Initialize the created object to given IPv6 numbers.
		 * @param a0 - zeroth number.
		 * @param a1 - first number.
		 * @param a2 - second number.
		 * @param a3 - third number.
		 * @param a4 - fourth number.
		 * @param a5 - fifth number.
		 * @param a6 - sixth number.
		 * @param a7 - sevens number.
         */
		ip(uint16_t a0, uint16_t a1, uint16_t a2, uint16_t a3, uint16_t a4, uint16_t a5, uint16_t a6, uint16_t a7) :
				ip(
						(uint32_t(a0) << 16) | uint32_t(a1),
						(uint32_t(a2) << 16) | uint32_t(a3),
						(uint32_t(a4) << 16) | uint32_t(a5),
						(uint32_t(a6) << 16) | uint32_t(a7)
					)
		{}
		
		/**
		 * @brief Creates a ip object using IPv6 bytes.
		 * Initialize the created object to given bytes.
		 * @param a0 - 0th byte of the IPv6 address.
		 * @param a1 - 1st byte of the IPv6 address.
		 * @param a2 - 2nd byte of the IPv6 address.
		 * @param a3 - 3rd byte of the IPv6 address.
		 * @param a4 - 4th byte of the IPv6 address.
		 * @param a5 - 5th byte of the IPv6 address.
		 * @param a6 - 6th byte of the IPv6 address.
		 * @param a7 - 7th byte of the IPv6 address.
		 * @param a8 - 8th byte of the IPv6 address.
		 * @param a9 - 9th byte of the IPv6 address.
		 * @param a10 - 10th byte of the IPv6 address.
		 * @param a11 - 11th byte of the IPv6 address.
		 * @param a12 - 12th byte of the IPv6 address.
		 * @param a13 - 13th byte of the IPv6 address.
		 * @param a14 - 14th byte of the IPv6 address.
		 * @param a15 - 15th byte of the IPv6 address.
         */
		ip(
				uint8_t a0,
				uint8_t a1,
				uint8_t a2,
				uint8_t a3,
				uint8_t a4,
				uint8_t a5,
				uint8_t a6,
				uint8_t a7,
				uint8_t a8,
				uint8_t a9,
				uint8_t a10,
				uint8_t a11,
				uint8_t a12,
				uint8_t a13,
				uint8_t a14,
				uint8_t a15
			) :
				ip(
						(uint16_t(a0) << 8) | uint16_t(a1),
						(uint16_t(a2) << 8) | uint16_t(a3),
						(uint16_t(a4) << 8) | uint16_t(a5),
						(uint16_t(a6) << 8) | uint16_t(a7),
						(uint16_t(a8) << 8) | uint16_t(a9),
						(uint16_t(a10) << 8) | uint16_t(a11),
						(uint16_t(a12) << 8) | uint16_t(a13),
						(uint16_t(a14) << 8) | uint16_t(a15)
					)
		{}
		
		/**
		 * @brief Parse IP address from string.
		 * String may contain either IPv4 or IPv6 address.
         * @param str - string containing IP address.
         * @return ip object initialized to a parsed address.
		 * @throw std::runtime_error if string does not contain well formed IPv4 or IPv6 address.
         */
		static ip parse(const char* str);
		
		/**
		 * @brief Parse IPv4 from string.
		 * String may contain only IPv4 address.
         * @param str - string containing IPv4 address.
         * @return ip object initialized to a parsed address.
		 * @throw std::runtime_error if string does not contain well formed IPv4 address.
         */
		static ip parse_v4(const char* str);
		
		/**
		 * @brief Parse IPv6 from string.
		 * String may contain only IPv6 address.
         * @param ip - string containing IPv6 address.
         * @return ip object initialized to a parsed address.
		 * @throw std::runtime_error if string does not contain well formed IPv6 address.
         */
		static ip parse_v6(const char* ip);
		
		/**
		 * @brief Check if it is a IPv4 mapped to IPv6.
         * @return true if this ip object holds IPv4 address mapped to IPv6.
		 * @return false otherwise.
         */
		bool is_v4()const noexcept{
			return this->quad[2] == 0xffff && this->quad[1] == 0 && this->quad[0] == 0;
		}
		
		/**
		 * @brief Get IPv4 address.
         * @return IPv4 host if this is a IPv4 mapped to IPv6.
		 * @return undefined value otherwise.
         */
		uint32_t get_v4()const noexcept{
			return this->quad[3];
		}
		
		/**
		 * @brief Check if the IP host address is valid.
		 * Checks if this IP address is not an invalid address, which is all zeroes.
         * @return true if this IP address is not a zero address.
		 * @return false if this IP address is all zeroes.
         */
		bool is_valid()const noexcept{
			if(this->is_v4()){
				return this->get_v4() != 0;
			}
			
			return this->quad[3] != 0 || this->quad[2] != 0 || this->quad[1] != 0 || this->quad[0] != 0;
		}
		
		/**
		 * @brief Compare two IP host addresses.
         * @param h - IP host address to compare this IP host address to.
         * @return true if two IP addresses are identical.
		 * @return false otherwise.
         */
		bool operator==(const ip& h){
			return this->quad == h.quad;
		}
		
		/**
		 * @brief Convert this IP host address to string.
         * @return String representing an IP host address.
         */
		std::string to_string()const;
	};
	
	ip host; ///< IPv6 address
	uint16_t port; ///< IP port number
	
	/**
	 * @brief Construct IP address with undefined host and port.
     */
	ip_address()noexcept{}

	/**
	 * @brief Create IPv4-address specifying exact IP-address and port number.
	 * @param h - IPv4 address. For example, 0x7f000001 represents "127.0.0.1" IP address value.
	 * @param p - IP port number.
	 */
	ip_address(uint32_t h, uint16_t p)noexcept :
			host(h),
			port(p)
	{}

	/**
	 * @brief Create IPv4-address specifying exact IP-address as 4 bytes and port number.
	 * The IPv4-address can be specified as 4 separate byte values, for example:
	 * @code
	 * ting::net::ip_address ip(127, 0, 0, 1, 80); //"127.0.0.1" port 80
	 * @endcode
	 * @param h1 - 1st triplet of IPv4 address.
	 * @param h2 - 2nd triplet of IPv4 address.
	 * @param h3 - 3rd triplet of IPv4 address.
	 * @param h4 - 4th triplet of IPv4 address.
	 * @param p - IP port number.
	 */
	ip_address(uint8_t h1, uint8_t h2, uint8_t h3, uint8_t h4, uint16_t p)noexcept :
			host((uint32_t(h1) << 24) | (uint32_t(h2) << 16) | (uint32_t(h3) << 8) | uint32_t(h4)),
			port(p)
	{}

	/**
	 * @brief Construct IP address from given host and port.
     * @param h - host to use for construction.
     * @param p - port to use for construction.
     */
	ip_address(ip h, uint16_t p)noexcept :
			host(h),
			port(p)
	{}
	
	/**
	 * @brief Create IP address specifying IP host address as string and port number.
	 * The string passed as argument should contain properly formatted IPv4 or IPv6 host address.
	 * @param host_str - IPv4 or IPv6 host address null-terminated string. Example: "127.0.0.1".
	 * @param p - IP port number.
	 * @throw Badip_addressFormatExc - when passed string does not contain properly formatted IP address.
	 */
	ip_address(const char* host_str, uint16_t p);
	
	/**
	 * @brief Create IP address specifying IP host address and IP port as string.
	 * The string passed for parsing should contain the IP host address with the port number.
	 * If there is no port number specified after the IP-address the format of the IP-address
	 * is regarded as invalid and corresponding exception is thrown.
     * @param str - null-terminated string representing IP address with port number, e.g. "127.0.0.1:80" or "[42f4:234a::23]:432".
	 * @throw Badip_addressFormatExc - when passed string does not contain properly formatted IP-address.
     */
	ip_address(const char* str);

	/**
	 * @brief compares two IP addresses for equality.
	 * @param ip - IP address to compare with.
	 * @return true if hosts and ports of the two IP addresses are equal accordingly.
	 * @return false otherwise.
	 */
	bool operator==(const ip_address& ip){
		return this->host == ip.host && this->port == ip.port;
	}
};
}
