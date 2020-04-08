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

		this->closeEventForWaitable();
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
	this->eventForWaitable = s.eventForWaitable;
	const_cast<Socket&>(s).eventForWaitable = WSA_INVALID_EVENT;
#endif

	const_cast<socket&>(s).sock = invalid_socket;
	return *this;
}

void socket::disable_naggle(){
	if(!*this){
		throw setka::Exc("socket::DisableNaggle(): socket is not valid");
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
		throw setka::Exc("socket::SetNonBlockingMode(): socket is not valid");
	}

#if M_OS == M_OS_WINDOWS
	{
		u_long mode = 1;
		if(ioctlsocket(this->sock, FIONBIO, &mode) != 0){
			throw setka::Exc("socket::SetNonBlockingMode(): ioctlsocket(FIONBIO) failed");
		}
	}
	
#elif M_OS == M_OS_LINUX || M_OS == M_OS_MACOSX || M_OS == M_OS_UNIX
	{
		int flags = fcntl(this->sock, F_GETFL, 0);
		if(flags == -1){
			throw setka::Exc("socket::SetNonBlockingMode(): fcntl(F_GETFL) failed");
		}
		if(fcntl(this->sock, F_SETFL, flags | O_NONBLOCK) != 0){
			throw setka::Exc("socket::SetNonBlockingMode(): fcntl(F_SETFL) failed");
		}
	}
#else
#	error "Unsupported OS"
#endif
}



std::uint16_t socket::get_local_port(){
	if(!*this){
		throw setka::Exc("socket::GetLocalPort(): socket is not valid");
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
		throw setka::Exc("socket::GetLocalPort(): getsockname() failed");
	}
	
	if(addr.ss_family == AF_INET){
		sockaddr_in& a = reinterpret_cast<sockaddr_in&>(addr);
		return std::uint16_t(ntohs(a.sin_port));
	}else{
		ASSERT(addr.ss_family == AF_INET6)
		sockaddr_in6& a = reinterpret_cast<sockaddr_in6&>(addr);
		return std::uint16_t(ntohs(a.sin6_port));
	}
}



#if M_OS == M_OS_WINDOWS
HANDLE socket::getHandle(){
	//return event handle
	return this->eventForWaitable;
}



bool socket::checkSignaled(){
	WSANETWORKEVENTS events;
	memset(&events, 0, sizeof(events));
	ASSERT(*this)
	if(WSAEnumNetworkEvents(this->sock, this->eventForWaitable, &events) != 0){
		throw setka::Exc("socket::CheckSignaled(): WSAEnumNetworkEvents() failed");
	}

	//NOTE: sometimes no events are reported, don't know why.
//		ASSERT(events.lNetworkEvents != 0)

	if((events.lNetworkEvents & FD_CLOSE) != 0){
		this->setErrorFlag();
	}

	if((events.lNetworkEvents & FD_READ) != 0){
		this->setCanReadFlag();
		if(events.iErrorCode[ASSCOND(FD_READ_BIT, < FD_MAX_EVENTS)] != 0){
			this->setErrorFlag();
		}
	}

	if((events.lNetworkEvents & FD_ACCEPT) != 0){
		this->setCanReadFlag();
		if(events.iErrorCode[ASSCOND(FD_ACCEPT_BIT, < FD_MAX_EVENTS)] != 0){
			this->setErrorFlag();
		}
	}

	if((events.lNetworkEvents & FD_WRITE) != 0){
		this->setCanWriteFlag();
		if(events.iErrorCode[ASSCOND(FD_WRITE_BIT, < FD_MAX_EVENTS)] != 0){
			this->setErrorFlag();
		}
	}

	if((events.lNetworkEvents & FD_CONNECT) != 0){
		this->setCanWriteFlag();
		if(events.iErrorCode[ASSCOND(FD_CONNECT_BIT, < FD_MAX_EVENTS)] != 0){
			this->setErrorFlag();
		}
	}

#ifdef DEBUG
	//if some event occurred then some of readiness flags should be set
	if(events.lNetworkEvents != 0){
		ASSERT_ALWAYS(this->readinessFlags != 0)
	}
#endif

	return this->Waitable::checkSignaled();
}



void socket::createEventForWaitable(){
	ASSERT(this->eventForWaitable == WSA_INVALID_EVENT)
	this->eventForWaitable = WSACreateEvent();
	if(this->eventForWaitable == WSA_INVALID_EVENT){
		throw setka::Exc("socket::CreateEventForWaitable(): could not create event (Win32) for implementing Waitable");
	}
}



void socket::closeEventForWaitable(){
	ASSERT(this->eventForWaitable != WSA_INVALID_EVENT)
	WSACloseEvent(this->eventForWaitable);
	this->eventForWaitable = WSA_INVALID_EVENT;
}



void socket::setWaitingEventsForWindows(long flags){
	ASSERT_INFO(*this && (this->eventForWaitable != WSA_INVALID_EVENT), "HINT: Most probably, you are trying to remove the _closed_ socket from WaitSet. If so, you should first remove the socket from WaitSet and only then call the Close() method.")

	if(WSAEventSelect(
			this->sock,
			this->eventForWaitable,
			flags
		) != 0)
	{
		throw setka::Exc("socket::setWaitingEventsForWindows(): could not associate event (Win32) with socket");
	}
}



#elif M_OS == M_OS_LINUX || M_OS == M_OS_MACOSX || M_OS == M_OS_UNIX

int socket::get_handle(){
	return this->sock;
}

#else
#	error "unsupported OS"
#endif
