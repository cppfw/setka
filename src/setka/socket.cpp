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

#include "socket.hpp"

#include <utki/config.hpp>

#if CFG_OS == CFG_OS_LINUX || CFG_OS == CFG_OS_MACOSX || CFG_OS == CFG_OS_UNIX
#	include <netinet/in.h>
#	include <netinet/tcp.h>
#	include <fcntl.h>
#	include <unistd.h>
#elif CFG_OS == CFG_OS_WINDOWS
#	include <ws2tcpip.h>
#endif

using namespace setka;

socket::~socket()noexcept{
	this->close();
}

void socket::close()noexcept{
//		TRACE(<< "socket::Close(): invoked " << this << std::endl)
	if(this->is_empty()){
		return;
	}

#if CFG_OS == CFG_OS_WINDOWS
		// Closing socket in Win32.
		// refer to http://tangentsoft.net/wskfaq/newbie.html#howclose for details
		shutdown(this->win_sock, SD_BOTH);
		closesocket(this->win_sock);

		this->close_event_for_waitable();
		this->win_sock = invalid_socket;
#elif CFG_OS == CFG_OS_LINUX || CFG_OS == CFG_OS_MACOSX || CFG_OS == CFG_OS_UNIX
		::close(this->handle);
		this->handle = invalid_socket;
#else
#	error "Unsupported OS"
#endif
}

setka::socket& socket::operator=(socket&& s){
	if(this == &s){ // detect self-assignment
		return *this;
	}

	if(!this->is_empty()){
		throw std::logic_error("socket::operator=(socket&&): cannot not move to non-empty socket");
	}

	this->handle = s.handle;

#if CFG_OS == CFG_OS_WINDOWS
	this->win_sock = s.win_sock;
	s.win_sock = invalid_socket;
	s.handle = WSA_INVALID_EVENT;
#elif CFG_OS == CFG_OS_LINUX || CFG_OS == CFG_OS_MACOSX || CFG_OS == CFG_OS_UNIX
	s.handle = invalid_socket;
#else
#	error "Unknown OS"
#endif

	return *this;
}

void socket::disable_naggle(){
	if(this->is_empty()){
		throw std::logic_error("socket::disable_naggle(): socket is empty");
	}

#if CFG_OS == CFG_OS_WINDOWS || CFG_OS == CFG_OS_LINUX || CFG_OS == CFG_OS_MACOSX || CFG_OS == CFG_OS_UNIX
	{
		int yes = 1;
		setsockopt(
#if CFG_OS == CFG_OS_WINDOWS
			this->win_sock,
#else
			this->handle,
#endif
			IPPROTO_TCP,
			TCP_NODELAY,
			(char*)&yes,
			sizeof(yes)
		);
	}
#else
#	error "Unsupported OS"
#endif
}

void socket::set_nonblocking_mode(){
	if(this->is_empty()){
		throw std::logic_error("socket::set_nonblocking_mode(): socket is empty");
	}

#if CFG_OS == CFG_OS_WINDOWS
	{
		u_long mode = 1;
		if(ioctlsocket(this->win_sock, FIONBIO, &mode) != 0){
			throw std::system_error(WSAGetLastError(), std::generic_category(), "could not set socket non-blocking mode, ioctlsocket(FIONBIO) failed");
		}
	}
	
#elif CFG_OS == CFG_OS_LINUX || CFG_OS == CFG_OS_MACOSX || CFG_OS == CFG_OS_UNIX
	{
		int flags = fcntl(this->handle, F_GETFL, 0);
		if(flags == -1){
			throw std::system_error(errno, std::generic_category(), "could not set socket non-blocking mode, fcntl(F_GETFL) failed");
		}
		if(fcntl(this->handle, F_SETFL, flags | O_NONBLOCK) != 0){
			throw std::system_error(errno, std::generic_category(), "could not set socket non-blocking mode, fcntl(F_SETFL) failed");
		}
	}
#else
#	error "Unsupported OS"
#endif
}

uint16_t socket::get_local_port(){
	if(this->is_empty()){
		throw std::logic_error("socket::GetLocalPort(): socket is not valid");
	}

	sockaddr_storage addr;

#if CFG_OS == CFG_OS_WINDOWS
	int len = sizeof(addr);
	int sock = this->win_sock;
#elif CFG_OS == CFG_OS_LINUX || CFG_OS == CFG_OS_MACOSX || CFG_OS == CFG_OS_UNIX
	socklen_t len = sizeof(addr);
	int sock = this->handle;
#else
#	error "Unsupported OS"
#endif

	if(getsockname(
			sock,
			reinterpret_cast<sockaddr*>(&addr),
			&len
		) < 0)
	{
		throw std::system_error(errno, std::generic_category(), "could not get local port, getsockname() failed");
	}
	
	if(addr.ss_family == AF_INET){
		sockaddr_in& a = reinterpret_cast<sockaddr_in&>(addr);
		return uint16_t(ntohs(a.sin_port));
	}else{
		ASSERT(addr.ss_family == AF_INET6)
		sockaddr_in6& a = reinterpret_cast<sockaddr_in6&>(addr);
		return uint16_t(ntohs(a.sin6_port));
	}
}

#if CFG_OS == CFG_OS_WINDOWS
utki::flags<ready> socket::get_readiness_flags(){
	WSANETWORKEVENTS events;
	memset(&events, 0, sizeof(events));
	ASSERT(!this->is_empty())
	if(WSAEnumNetworkEvents(this->win_sock, this->handle, &events) != 0){
		throw std::system_error(WSAGetLastError(), std::generic_category(), "could not check for network events, WSAEnumNetworkEvents() failed");
	}

	// NOTE: sometimes no events are reported, don't know why.
//	ASSERT(events.lNetworkEvents != 0)

	utki::flags<ready> flags;

	if((events.lNetworkEvents & FD_CLOSE) != 0){
		flags.set(opros::ready::error);
	}

	if((events.lNetworkEvents & FD_READ) != 0){
		flags.set(opros::ready::read);
		ASSERT(FD_READ_BIT < FD_MAX_EVENTS)
		if(events.iErrorCode[FD_READ_BIT] != 0){
			flags.set(opros::ready::error);
		}
	}

	if((events.lNetworkEvents & FD_ACCEPT) != 0){
		flags.set(opros::ready::read);
		ASSERT(FD_ACCEPT_BIT < FD_MAX_EVENTS)
		if(events.iErrorCode[FD_ACCEPT_BIT] != 0){
			flags.set(opros::ready::error);
		}
	}

	if((events.lNetworkEvents & FD_WRITE) != 0){
		flags.set(opros::ready::write);
		ASSERT(FD_WRITE_BIT < FD_MAX_EVENTS)
		if(events.iErrorCode[FD_WRITE_BIT] != 0){
			flags.set(opros::ready::error);
		}
	}

	if((events.lNetworkEvents & FD_CONNECT) != 0){
		flags.set(opros::ready::write);
		ASSERT(FD_CONNECT_BIT < FD_MAX_EVENTS)
		if(events.iErrorCode[FD_CONNECT_BIT] != 0){
			flags.set(opros::ready::error);
		}
	}

#ifdef DEBUG
	// if some event occurred then some of readiness flags should be set
	if(events.lNetworkEvents != 0){
		utki::assert(!flags.is_clear(), SL);
	}
#endif

	return flags;
}

void socket::create_event_for_waitable(){
	this->handle = WSACreateEvent();
	if(this->handle == WSA_INVALID_EVENT){
		throw std::system_error(WSAGetLastError(), std::generic_category(), "could not create event, WSACreateEvent() failed");
	}
}

void socket::close_event_for_waitable(){
	ASSERT(this->handle != WSA_INVALID_EVENT)
	WSACloseEvent(this->handle);
}

void socket::set_waiting_events_for_windows(long flags){
	ASSERT(
		!this->is_empty() && (this->handle != WSA_INVALID_EVENT),
		[&](auto&o){o << "HINT: Most probably, you are trying to remove the _closed_ socket from WaitSet. If so, you should first remove the socket from WaitSet and only then call the Close() method.";}
	)

	if(WSAEventSelect(
			this->win_sock,
			this->handle,
			flags
		) != 0)
	{
		throw std::system_error(WSAGetLastError(), std::generic_category(), "could not associate event with socket, WSAEventSelect() failed");
	}
}

#elif CFG_OS == CFG_OS_LINUX || CFG_OS == CFG_OS_MACOSX || CFG_OS == CFG_OS_UNIX
#else
#	error "unsupported OS"
#endif
