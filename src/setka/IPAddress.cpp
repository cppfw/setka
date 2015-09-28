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


#include <sstream>

#include "IPAddress.hpp"

#include "../config.hpp"
#include "../Buffer.hpp"

#if M_OS == M_OS_LINUX || M_OS == M_OS_MACOSX
#	include <arpa/inet.h>
#elif M_OS == M_OS_WINDOWS
#	include <Ws2tcpip.h>
#else
#	error "Unknown OS"
#endif



using namespace ting::net;



namespace{

bool IsIPv4String(const char* ip){
	for(const char* p = ip; *p != 0; ++p){
		if(*p == '.'){
			return true;
		}
		if(*p == ':'){
			return false;
		}
	}
	return false;
}

}//~namespace



//static
IPAddress::Host IPAddress::Host::Parse(const char* ip){
	if(IsIPv4String(ip)){
		return Host::ParseIPv4(ip);
	}else{
		return Host::ParseIPv6(ip);
	}
}



//static
IPAddress::Host IPAddress::Host::ParseIPv4(const char* ip){
	sockaddr_in a;
	
#if M_OS == M_OS_LINUX || M_OS == M_OS_MACOSX
	int res = inet_pton(
			AF_INET,
			ip,
			&a.sin_addr
		);
	
	if(res != 1){
		throw BadIPHostFormatExc();
	}

#elif M_OS == M_OS_WINDOWS
	INT len = sizeof(a);
	INT res = WSAStringToAddress(
			const_cast<char*>(ip), //NOTE: when compiling in MS Visual Studio, set "Use multi-byte character set" in project properties to avoid usage of wchar_t
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
	
	return Host(ntohl(a.sin_addr.s_addr));
}



//static
IPAddress::Host IPAddress::Host::ParseIPv6(const char* ip){
#if M_OS == M_OS_WINDOWS
	sockaddr_in6 aa;
	in6_addr& a = aa.sin6_addr;
#else
	in6_addr a;
#endif	
		
#if M_OS == M_OS_LINUX || M_OS == M_OS_MACOSX
	int res = inet_pton(
			AF_INET6,
			ip,
			&a
		);
	
	if(res != 1){
		throw BadIPHostFormatExc();
	}

#elif M_OS == M_OS_WINDOWS
	INT len = sizeof(aa);
	INT res = WSAStringToAddress(
			const_cast<char*>(ip),
			AF_INET6,
			NULL,
			reinterpret_cast<sockaddr*>(&aa),
			&len
		);
	if(res != 0){
		TRACE(<< "IPAddress::Host::ParseIPv6(): WSAStringToAddress() failed, error = " <<  WSAGetLastError()<< ". Maybe IPv6 protocol is not installed in the Windows system, WSAStringToAddress() only supports IPv6 if it is installed." << std::endl)
		throw BadIPHostFormatExc();
	}
#else
#	error "Unknown OS"
#endif

#if M_OS == M_OS_MACOSX || M_OS == M_OS_WINDOWS || (M_OS == M_OS_LINUX && M_OS_NAME == M_OS_NAME_ANDROID)
	return Host(
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
	return Host(
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



IPAddress::IPAddress(const char* ip, std::uint16_t p) :
		host(Host::Parse(ip)),
		port(p)
{}



IPAddress::IPAddress(const char* ip){
	if(*ip == 0){//if zero length string
		throw BadIPAddressFormatExc();
	}
	
	if(*ip == '['){//IPv6 with port
		std::array<char, (4 * 6 + 6 + (3 * 4 + 3) + 1)> buf;
		
		++ip;
		
		char* dst;
		for(dst = &*buf.begin(); *ip != ']'; ++dst, ++ip){
			if(*ip == 0 || !ting::Buffer<char>(buf).Overlaps(dst + 1)){
				throw BadIPAddressFormatExc();
			}
			
			*dst = *ip;
		}
		
		ASSERT(ting::Buffer<char>(buf).Overlaps(dst))
		*dst = 0;//null-terminate
				
		this->host = Host::ParseIPv6(&*buf.begin());
		
		++ip;//move to port ':' separator
	}else{
		//IPv4 or IPv6 without port
		
		if(IsIPv4String(ip)){
			std::array<char, (3 * 4 + 3 + 1)> buf;
			
			char* dst;
			for(dst = &*buf.begin(); *ip != ':' && *ip != 0; ++dst, ++ip){
				if(!ting::Buffer<char>(buf).Overlaps(dst + 1)){
					throw BadIPAddressFormatExc();
				}

				*dst = *ip;
			}

			ASSERT(ting::Buffer<char>(buf).Overlaps(dst))
			*dst = 0;//null-terminate

			this->host = Host::ParseIPv4(&*buf.begin());
		}else{
			//IPv6 without port
			this->host = Host::ParseIPv6(ip);
			this->port = 0;
			return;
		}
	}
	
	//parse port
	
	if(*ip != ':'){
		if(*ip == 0){
			this->port = 0;
			return;
		}else{
	//		TRACE(<< "no colon, *ip = " << (*ip) << std::endl)
			throw ting::net::IPAddress::BadIPAddressFormatExc();
		}
	}
	
	++ip;
	
	//move to the end of port number, maximum 5 digits.
	for(unsigned i = 0; '0' <= *ip && *ip <= '9' && i < 5; ++i, ++ip){
	}
	if('0' <= *ip && *ip <= '9'){//if still have one more digit
//		TRACE(<< "still have one more digit" << std::endl)
		throw ting::net::IPAddress::BadIPAddressFormatExc();
	}
	
	--ip;
	
	std::uint32_t port = 0;
	
	for(unsigned i = 0; *ip != ':'; ++i, --ip){
		std::uint32_t pow = 1;
		for(unsigned j = 0; j < i; ++j){
			pow *= 10;
		}
	
		ASSERT('0' <= *ip && *ip <= '9')
		
		port += (*ip - '0') * pow;
	}
	
	if(port > 0xffff){
//		TRACE(<< "port number is bigger than 0xffff" << std::endl)
		throw ting::net::IPAddress::BadIPAddressFormatExc();
	}
	
	this->port = std::uint16_t(port);
}



std::string IPAddress::Host::ToString()const{
	std::stringstream ss;
	if(this->IsIPv4()){
		for(unsigned i = 4;;){
			--i;
			ss << (((this->IPv4Host()) >> (8 * i)) & 0xff);
			if(i == 0){
				break;
			}
			ss << '.';
		}
	}else{
		ss << std::hex;
		for(unsigned i = 8;;){
			--i;
			ss << ((this->host[(i * 2) / 4] >> (16 * (i % 2))) & 0xffff);
			if(i == 0){
				break;
			}
			ss << ':';
		}
	}
	return ss.str();
}
