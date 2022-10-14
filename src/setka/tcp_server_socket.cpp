/*
The MIT License (MIT)

Copyright (c) 2015-2022 Ivan Gagis <igagis@gmail.com>

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

#if M_OS == M_OS_LINUX || M_OS == M_OS_MACOSX || M_OS == M_OS_UNIX
#	include <netinet/in.h>
#endif

using namespace setka;

void tcp_server_socket::open(uint16_t port, bool disable_naggle, uint16_t queueLength){
	if(this->is_open()){
		throw std::logic_error("socket already opened");
	}

	this->disable_naggle = disable_naggle;

#if M_OS == M_OS_WINDOWS
	this->create_event_for_waitable();
#endif

	bool ipv4 = false;
	
	this->sock = ::socket(PF_INET6, SOCK_STREAM, 0);
	
	if(this->sock == invalid_socket){
		// maybe IPv6 is not supported by OS, try creating IPv4 socket
		
		this->sock = ::socket(PF_INET, SOCK_STREAM, 0);

		if(this->sock == invalid_socket){
#if M_OS == M_OS_WINDOWS
			this->close_event_for_waitable();
#endif
			throw std::system_error(errno, std::generic_category(), "couldn't create IPv4 TCP server socket, socket() failed");
		}
		
		ipv4 = true;
	}
	
	// turn off IPv6 only mode to allow also accepting IPv4 connections
	if(!ipv4){
#if M_OS == M_OS_WINDOWS
		char no = 0;
		const char* noPtr = &no;
#else
		int no = 0;
		void* noPtr = &no;
#endif
		if(setsockopt(this->sock, IPPROTO_IPV6, IPV6_V6ONLY, noPtr, sizeof(no)) != 0){
			// Dual stack is not supported, proceed with IPv4 only.
			
			this->close(); // close IPv6 socket
			
			// create IPv4 socket
			
#if M_OS == M_OS_WINDOWS
			this->create_event_for_waitable();
#endif			
			
			this->sock = ::socket(PF_INET, SOCK_STREAM, 0);
	
			if(this->sock == invalid_socket){
#if M_OS == M_OS_WINDOWS
				this->close_event_for_waitable();
#endif
				throw std::system_error(errno, std::generic_category(), "couldn't create IPv4 server socket, socket() failed");
			}
			
			ipv4 = true;
		}
	}
	
	// allow local address reuse
	{
		int yes = 1;
		setsockopt(this->sock, SOL_SOCKET, SO_REUSEADDR, (char*)&yes, sizeof(yes));
	}

	sockaddr_storage sockAddr;
	socklen_t sockAddrLen;
	
	if(ipv4){
		sockaddr_in& sa = reinterpret_cast<sockaddr_in&>(sockAddr);
		memset(&sa, 0, sizeof(sa));
		sa.sin_family = AF_INET;
		sa.sin_addr.s_addr = INADDR_ANY;
		sa.sin_port = htons(port);
		sockAddrLen = sizeof(sa);
	}else{
		sockaddr_in6& sa = reinterpret_cast<sockaddr_in6&>(sockAddr);
		memset(&sa, 0, sizeof(sa));
		sa.sin6_family = AF_INET6;
		sa.sin6_addr = in6addr_any; // 'in6addr_any' allows accepting both IPv4 and IPv6 connections!!!
		sa.sin6_port = htons(port);
		sockAddrLen = sizeof(sa);
	}

	// Bind the socket for listening
	if(bind(
			this->sock,
			reinterpret_cast<sockaddr*>(&sockAddr),
			sockAddrLen
		) == socket_error)
	{
#if M_OS == M_OS_WINDOWS
		int errorCode = WSAGetLastError();
#elif M_OS == M_OS_LINUX || M_OS == M_OS_MACOSX || M_OS == M_OS_UNIX
		int errorCode = errno;
#else
#	error "Unsupported OS"
#endif
		
		this->close();

		throw std::system_error(errorCode, std::generic_category(), "could not bind socket, bind() failed");
	}

	if(listen(this->sock, int(queueLength)) == socket_error){
#if M_OS == M_OS_WINDOWS
		int errorCode = WSAGetLastError();
#elif M_OS == M_OS_LINUX || M_OS == M_OS_MACOSX || M_OS == M_OS_UNIX
		int errorCode = errno;
#else
#	error "Unsupported OS"
#endif

		this->close();
		
		throw std::system_error(errorCode, std::generic_category(), "couldn't listen on the local port, listen() failed");
	}

	this->set_nonblocking_mode();
}

tcp_socket tcp_server_socket::accept(){
	if(!this->is_open()){
		throw std::logic_error("tcp_server_socket::accept(): the socket is not opened");
	}

	this->readiness_flags.clear(opros::ready::read);

	sockaddr_storage sockAddr;

#if M_OS == M_OS_WINDOWS
	int sock_alen = sizeof(sockAddr);
#elif M_OS == M_OS_LINUX || M_OS == M_OS_MACOSX || M_OS == M_OS_UNIX
	socklen_t sock_alen = sizeof(sockAddr);
#else
#	error "Unsupported OS"
#endif

	tcp_socket s;

	s.sock = ::accept(
			this->sock,
			reinterpret_cast<sockaddr*>(&sockAddr),
			&sock_alen
		);

	if(s.sock == invalid_socket){
		return s; // no connections to be accepted, return invalid socket
	}

#if M_OS == M_OS_WINDOWS
	s.create_event_for_waitable();

	// NOTE: accepted socket is associated with the same event object as the listening socket which accepted it.
	// Re-associate the socket with its own event object.
	s.set_waiting_flags(utki::make_flags<opros::ready>({}));
#endif

	s.set_nonblocking_mode();

	if(this->disable_naggle){
		s.disable_naggle();
	}

	return s; // return a newly created socket
}

#if M_OS == M_OS_WINDOWS
void tcp_server_socket::set_waiting_flags(utki::flags<opros::ready> waiting_flags){
	if(!waiting_flags.is_clear() && !waiting_flags.get(opros::ready::read)){
		throw std::logic_error("tcp_server_socket::SetWaitingEvents(): only READ flag allowed");
	}

	long flags = FD_CLOSE;
	if(waiting_flags.get(opros::ready::read)){
		flags |= FD_ACCEPT;
	}
	this->set_waiting_events_for_windows(flags);
}
#endif
