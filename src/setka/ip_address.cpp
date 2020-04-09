#include <sstream>

#include "ip_address.hpp"

#include <utki/config.hpp>
#include <utki/span.hpp>

#if M_OS == M_OS_LINUX || M_OS == M_OS_MACOSX
#	include <arpa/inet.h>
#elif M_OS == M_OS_WINDOWS
#	include <Ws2tcpip.h>
#else
#	error "Unknown OS"
#endif

using namespace setka;

namespace{
bool is_ip_v4_string(const char* str){
	for(const char* p = str; *p != 0; ++p){
		if(*p == '.'){
			return true;
		}
		if(*p == ':'){
			return false;
		}
	}
	return false;
}
}

ip_address::ip ip_address::ip::parse(const char* str){
	if(is_ip_v4_string(str)){
		return ip::parse_v4(str);
	}else{
		return ip::parse_v6(str);
	}
}

ip_address::ip ip_address::ip::parse_v4(const char* str){
	sockaddr_in a;
	
#if M_OS == M_OS_LINUX || M_OS == M_OS_MACOSX
	int res = inet_pton(
			AF_INET,
			str,
			&a.sin_addr
		);
	
	if(res != 1){
		throw std::runtime_error("bad IP address format");
	}

#elif M_OS == M_OS_WINDOWS
	INT len = sizeof(a);
	INT res = WSAStringToAddress(
			const_cast<char*>(str), //NOTE: when compiling in MS Visual Studio, set "Use multi-byte character set" in project properties to avoid usage of wchar_t
			AF_INET,
			NULL,
			reinterpret_cast<sockaddr*>(&a),
			&len
		);
	if(res != 0){
		throw BadIPHostFormatExc();
	}
#else
#	error "Unknown OS"
#endif
	
	return ip_address::ip(ntohl(a.sin_addr.s_addr));
}

ip_address::ip ip_address::ip::parse_v6(const char* str){
#if M_OS == M_OS_WINDOWS
	sockaddr_in6 aa;
	in6_addr& a = aa.sin6_addr;
#else
	in6_addr a;
#endif	
		
#if M_OS == M_OS_LINUX || M_OS == M_OS_MACOSX
	int res = inet_pton(
			AF_INET6,
			str,
			&a
		);
	
	if(res != 1){
		throw std::runtime_error("bad IP address format");
	}

#elif M_OS == M_OS_WINDOWS
	INT len = sizeof(aa);
	INT res = WSAStringToAddress(
			const_cast<char*>(str),
			AF_INET6,
			NULL,
			reinterpret_cast<sockaddr*>(&aa),
			&len
		);
	if(res != 0){
		TRACE(<< "ip_address::ip::ParseIPv6(): WSAStringToAddress() failed, error = " <<  WSAGetLastError()<< ". Maybe IPv6 protocol is not installed in the Windows system, WSAStringToAddress() only supports IPv6 if it is installed." << std::endl)
		throw std::runtime_error("invalid IPv6 address string");
	}
#else
#	error "Unknown OS"
#endif

#if M_OS == M_OS_MACOSX || M_OS == M_OS_WINDOWS || (M_OS == M_OS_LINUX && M_OS_NAME == M_OS_NAME_ANDROID)
	return ip_address::ip(
			a.s6_addr[0],
			a.s6_addr[1],
			a.s6_addr[2],
			a.s6_addr[3],
			a.s6_addr[4],
			a.s6_addr[5],
			a.s6_addr[6],
			a.s6_addr[7],
			a.s6_addr[8],
			a.s6_addr[9],
			a.s6_addr[10],
			a.s6_addr[11],
			a.s6_addr[12],
			a.s6_addr[13],
			a.s6_addr[14],
			a.s6_addr[15]
		);
#else
	return ip_address::ip(
			a.__in6_u.__u6_addr8[0],
			a.__in6_u.__u6_addr8[1],
			a.__in6_u.__u6_addr8[2],
			a.__in6_u.__u6_addr8[3],
			a.__in6_u.__u6_addr8[4],
			a.__in6_u.__u6_addr8[5],
			a.__in6_u.__u6_addr8[6],
			a.__in6_u.__u6_addr8[7],
			a.__in6_u.__u6_addr8[8],
			a.__in6_u.__u6_addr8[9],
			a.__in6_u.__u6_addr8[10],
			a.__in6_u.__u6_addr8[11],
			a.__in6_u.__u6_addr8[12],
			a.__in6_u.__u6_addr8[13],
			a.__in6_u.__u6_addr8[14],
			a.__in6_u.__u6_addr8[15]
		);
#endif
}

ip_address::ip_address(const char* host_str, uint16_t p) :
		host(ip_address::ip::parse(host_str)),
		port(p)
{}

ip_address::ip_address(const char* str){
	if(*str == 0){ // if zero length string
		throw std::runtime_error("bad IP address format");
	}
	
	if(*str == '['){ // IPv6 with port
		std::array<char, (4 * 6 + 6 + (3 * 4 + 3) + 1)> buf;
		
		++str;
		
		char* dst;
		for(dst = &*buf.begin(); *str != ']'; ++dst, ++str){
			if(*str == 0 || !utki::make_span(buf).overlaps(dst + 1)){
				throw std::runtime_error("bad IP address format");
			}
			
			*dst = *str;
		}
		
		ASSERT(utki::make_span(buf).overlaps(dst))
		*dst = 0; // null-terminate
				
		this->host = ip::parse_v6(&*buf.begin());
		
		++str; // move to port ':' separator
	}else{
		// IPv4 or IPv6 without port
		
		if(is_ip_v4_string(str)){
			std::array<char, (3 * 4 + 3 + 1)> buf;
			
			char* dst;
			for(dst = &*buf.begin(); *str != ':' && *str != 0; ++dst, ++str){
				if(!utki::make_span(buf).overlaps(dst + 1)){
					throw std::runtime_error("bad IP address format");
				}

				*dst = *str;
			}

			ASSERT(utki::make_span(buf).overlaps(dst))
			*dst = 0; // null-terminate

			this->host = ip::parse_v4(&*buf.begin());
		}else{
			// IPv6 without port
			this->host = ip::parse_v6(str);
			this->port = 0;
			return;
		}
	}
	
	// parse port
	
	if(*str != ':'){
		if(*str == 0){
			this->port = 0;
			return;
		}else{
	//		TRACE(<< "no colon, *ip = " << (*ip) << std::endl)
			throw std::runtime_error("bad IP address format");
		}
	}
	
	++str;
	
	// move to the end of port number, maximum 5 digits.
	for(unsigned i = 0; '0' <= *str && *str <= '9' && i < 5; ++i, ++str){
	}
	if('0' <= *str && *str <= '9'){//if still have one more digit
//		TRACE(<< "still have one more digit" << std::endl)
		throw std::runtime_error("bad IP address format");
	}
	
	--str;
	
	uint32_t port = 0;
	
	for(unsigned i = 0; *str != ':'; ++i, --str){
		uint32_t pow = 1;
		for(unsigned j = 0; j < i; ++j){
			pow *= 10;
		}
	
		ASSERT('0' <= *str && *str <= '9')
		
		port += (*str - '0') * pow;
	}
	
	if(port > 0xffff){
//		TRACE(<< "port number is bigger than 0xffff" << std::endl)
		throw std::runtime_error("bad IP address format");
	}
	
	this->port = uint16_t(port);
}

std::string ip_address::ip::to_string()const{
	std::stringstream ss;
	if(this->is_v4()){
		for(unsigned i = 4;;){
			--i;
			ss << (((this->get_v4()) >> (8 * i)) & 0xff);
			if(i == 0){
				break;
			}
			ss << '.';
		}
	}else{
		ss << std::hex;
		for(unsigned i = 8;;){
			--i;
			ss << ((this->quad[(i * 2) / 4] >> (16 * (i % 2))) & 0xffff);
			if(i == 0){
				break;
			}
			ss << ':';
		}
	}
	return ss.str();
}
