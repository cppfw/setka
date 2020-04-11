#include "socket.hpp"

#include <utki/config.hpp>

#if M_OS == M_OS_LINUX || M_OS == M_OS_MACOSX || M_OS == M_OS_UNIX
#	include <netinet/in.h>
#	include <netinet/tcp.h>
#	include <fcntl.h>
#	include <unistd.h>
#elif M_OS == M_OS_WINDOWS
#	include <ws2tcpip.h>
#endif

using namespace setka;

socket::~socket()noexcept{
	this->close();
}

void socket::close()noexcept{
//		TRACE(<< "socket::Close(): invoked " << this << std::endl)
	ASSERT_INFO(!this->is_added(), "socket::close(): trying to close socket which is added to the WaitSet. Remove the socket from WaitSet before closing.")
	
	if(*this){
		ASSERT(!this->is_added())

#if M_OS == M_OS_WINDOWS
		// Closing socket in Win32.
		// refer to http://tangentsoft.net/wskfaq/newbie.html#howclose for details
		shutdown(this->sock, SD_BOTH);
		closesocket(this->sock);

		this->close_event_for_waitable();
#elif M_OS == M_OS_LINUX || M_OS == M_OS_MACOSX || M_OS == M_OS_UNIX
		::close(this->sock);
#else
#	error "Unsupported OS"
#endif
	}
	this->readiness_flags.clear();
	this->sock = invalid_socket;
}

setka::socket& socket::operator=(socket&& s){
//	TRACE(<< "socket::operator=(): invoked " << this << std::endl)
	if(this == &s){//detect self-assignment
		return *this;
	}

	// first, assign as Waitable, it may throw an exception
	// if the waitable is added to some waitset
	this->waitable::operator=(std::move(s));

	this->close();
	this->sock = s.sock;

#if M_OS == M_OS_WINDOWS
	this->event_for_waitable = s.event_for_waitable;
	const_cast<socket&>(s).event_for_waitable= WSA_INVALID_EVENT;
#endif

	const_cast<socket&>(s).sock = invalid_socket;
	return *this;
}

void socket::disable_naggle(){
	if(!*this){
		throw std::logic_error("socket::disable_naggle(): socket is not valid");
	}

#if M_OS == M_OS_WINDOWS || M_OS == M_OS_LINUX || M_OS == M_OS_MACOSX || M_OS == M_OS_UNIX
	{
		int yes = 1;
		setsockopt(this->sock, IPPROTO_TCP, TCP_NODELAY, (char*)&yes, sizeof(yes));
	}
#else
#	error "Unsupported OS"
#endif
}

void socket::set_nonblocking_mode(){
	if(!*this){
		throw std::logic_error("socket::set_nonblocking_mode(): socket is not valid");
	}

#if M_OS == M_OS_WINDOWS
	{
		u_long mode = 1;
		if(ioctlsocket(this->sock, FIONBIO, &mode) != 0){
			throw std::system_error(WSAGetLastError(), std::generic_category(), "could not set socket non-blocking mode, ioctlsocket(FIONBIO) failed");
		}
	}
	
#elif M_OS == M_OS_LINUX || M_OS == M_OS_MACOSX || M_OS == M_OS_UNIX
	{
		int flags = fcntl(this->sock, F_GETFL, 0);
		if(flags == -1){
			throw std::system_error(errno, std::generic_category(), "could not set socket non-blocking mode, fcntl(F_GETFL) failed");
		}
		if(fcntl(this->sock, F_SETFL, flags | O_NONBLOCK) != 0){
			throw std::system_error(errno, std::generic_category(), "could not set socket non-blocking mode, fcntl(F_SETFL) failed");
		}
	}
#else
#	error "Unsupported OS"
#endif
}

uint16_t socket::get_local_port(){
	if(!*this){
		throw std::logic_error("socket::GetLocalPort(): socket is not valid");
	}

	sockaddr_storage addr;

#if M_OS == M_OS_WINDOWS
	int len = sizeof(addr);
#elif M_OS == M_OS_LINUX || M_OS == M_OS_MACOSX || M_OS == M_OS_UNIX
	socklen_t len = sizeof(addr);
#else
#	error "Unsupported OS"
#endif

	if(getsockname(
			this->sock,
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

#if M_OS == M_OS_WINDOWS
HANDLE socket::get_handle(){
	return this->event_for_waitable;
}

bool socket::check_signaled(){
	WSANETWORKEVENTS events;
	memset(&events, 0, sizeof(events));
	ASSERT(*this)
	if(WSAEnumNetworkEvents(this->sock, this->event_for_waitable, &events) != 0){
		throw std::system_error(WSAGetLastError(), std::generic_category(), "could not check for network events, WSAEnumNetworkEvents() failed");
	}

	// NOTE: sometimes no events are reported, don't know why.
//	ASSERT(events.lNetworkEvents != 0)

	if((events.lNetworkEvents & FD_CLOSE) != 0){
		this->readiness_flags.set(opros::ready::error);
	}

	if((events.lNetworkEvents & FD_READ) != 0){
		this->readiness_flags.set(opros::ready::read);
		ASSERT(FD_READ_BIT < FD_MAX_EVENTS)
		if(events.iErrorCode[FD_READ_BIT] != 0){
			this->readiness_flags.set(opros::ready::error);
		}
	}

	if((events.lNetworkEvents & FD_ACCEPT) != 0){
		this->readiness_flags.set(opros::ready::read);
		ASSERT(FD_ACCEPT_BIT < FD_MAX_EVENTS)
		if(events.iErrorCode[FD_ACCEPT_BIT] != 0){
			this->readiness_flags.set(opros::ready::error);
		}
	}

	if((events.lNetworkEvents & FD_WRITE) != 0){
		this->readiness_flags.set(opros::ready::write);
		ASSERT(FD_WRITE_BIT < FD_MAX_EVENTS)
		if(events.iErrorCode[FD_WRITE_BIT] != 0){
			this->readiness_flags.set(opros::ready::error);
		}
	}

	if((events.lNetworkEvents & FD_CONNECT) != 0){
		this->readiness_flags.set(opros::ready::write);
		ASSERT(FD_CONNECT_BIT < FD_MAX_EVENTS)
		if(events.iErrorCode[FD_CONNECT_BIT] != 0){
			this->readiness_flags.set(opros::ready::error);
		}
	}

#ifdef DEBUG
	// if some event occurred then some of readiness flags should be set
	if(events.lNetworkEvents != 0){
		ASSERT_ALWAYS(!this->readiness_flags.is_clear())
	}
#endif

	return this->waitable::check_signaled();
}

void socket::create_event_for_waitable(){
	ASSERT(this->event_for_waitable== WSA_INVALID_EVENT)
	this->event_for_waitable= WSACreateEvent();
	if(this->event_for_waitable== WSA_INVALID_EVENT){
		throw std::system_error(WSAGetLastError(), std::generic_category(), "could not create event, WSACreateEvent() failed");
	}
}

void socket::close_event_for_waitable(){
	ASSERT(this->event_for_waitable != WSA_INVALID_EVENT)
	WSACloseEvent(this->event_for_waitable);
	this->event_for_waitable= WSA_INVALID_EVENT;
}

void socket::set_waiting_events_for_windows(long flags){
	ASSERT_INFO(*this && (this->event_for_waitable != WSA_INVALID_EVENT), "HINT: Most probably, you are trying to remove the _closed_ socket from WaitSet. If so, you should first remove the socket from WaitSet and only then call the Close() method.")

	if(WSAEventSelect(
			this->sock,
			this->event_for_waitable,
			flags
		) != 0)
	{
		throw std::system_error(WSAGetLAstError(), std::generic_category(), "could not associate event with socket, WSAEventSelect() failed");
	}
}

#elif M_OS == M_OS_LINUX || M_OS == M_OS_MACOSX || M_OS == M_OS_UNIX

int socket::get_handle(){
	return this->sock;
}

#else
#	error "unsupported OS"
#endif
