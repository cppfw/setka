#include "TCPServerSocket.hpp"

#include <cstring>

#if M_OS == M_OS_LINUX || M_OS == M_OS_MACOSX || M_OS == M_OS_UNIX
#	include <netinet/in.h>
#endif


using namespace setka;



void TCPServerSocket::open(std::uint16_t port, bool disableNaggle, std::uint16_t queueLength){
	if(*this){
		throw setka::Exc("TCPServerSocket::Open(): socket already opened");
	}

	this->disableNaggle = disableNaggle;

#if M_OS == M_OS_WINDOWS
	this->createEventForWaitable();
#endif

	bool ipv4 = false;
	
	this->socket = ::socket(PF_INET6, SOCK_STREAM, 0);
	
	if(this->socket == DInvalidSocket()){
		//maybe IPv6 is not supported by OS, try creating IPv4 socket
		
		this->socket = ::socket(PF_INET, SOCK_STREAM, 0);

		if(this->socket == DInvalidSocket()){
#if M_OS == M_OS_WINDOWS
			this->closeEventForWaitable();
#endif
			throw setka::Exc("TCPServerSocket::Open(): Couldn't create IPv4 socket");
		}
		
		ipv4 = true;
	}
	
	//turn off IPv6 only mode to allow also accepting IPv4 connections
	if(!ipv4){
#if M_OS == M_OS_WINDOWS
		char no = 0;
		const char* noPtr = &no;
#else
		int no = 0;
		void* noPtr = &no;
#endif
		if(setsockopt(this->socket, IPPROTO_IPV6, IPV6_V6ONLY, noPtr, sizeof(no)) != 0){
			//Dual stack is not supported, proceed with IPv4 only.
			
			this->close();//close IPv6 socket
			
			//create IPv4 socket
			
#if M_OS == M_OS_WINDOWS
			this->createEventForWaitable();
#endif			
			
			this->socket = ::socket(PF_INET, SOCK_STREAM, 0);
	
			if(this->socket == DInvalidSocket()){
#if M_OS == M_OS_WINDOWS
				this->closeEventForWaitable();
#endif
				throw setka::Exc("TCPServerSocket::Open(): Couldn't create IPv4 socket");
			}
			
			ipv4 = true;
		}
	}
	
	// allow local address reuse
	{
		int yes = 1;
		setsockopt(this->socket, SOL_SOCKET, SO_REUSEADDR, (char*)&yes, sizeof(yes));
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
		sa.sin6_addr = in6addr_any;//'in6addr_any' allows accepting both IPv4 and IPv6 connections!!!
		sa.sin6_port = htons(port);
		sockAddrLen = sizeof(sa);
	}

	// Bind the socket for listening
	if(bind(
			this->socket,
			reinterpret_cast<sockaddr*>(&sockAddr),
			sockAddrLen
		) == DSocketError())
	{
#if M_OS == M_OS_WINDOWS
		int errorCode = WSAGetLastError();
#elif M_OS == M_OS_LINUX || M_OS == M_OS_MACOSX || M_OS == M_OS_UNIX
		int errorCode = errno;
#else
#	error "Unsupported OS"
#endif
		
		std::stringstream ss;
		ss << "TCPServerSocket::Open(): bind() failed, error code = " << errorCode << ": ";
#if M_COMPILER == M_COMPILER_MSVC
		{
			const size_t msgbufSize = 0xff;
			char msgbuf[msgbufSize];
			strerror_s(msgbuf, msgbufSize, errorCode);
			msgbuf[msgbufSize - 1] = 0;//make sure the string is null-terminated
			ss << msgbuf;
		}
#else
		ss << strerror(errorCode);
#endif
		this->close();
		throw setka::Exc(ss.str());
	}

	if(listen(this->socket, int(queueLength)) == DSocketError()){
		this->close();
		throw setka::Exc("TCPServerSocket::Open(): Couldn't listen to local port");
	}

	this->setNonBlockingMode();
}



TCPSocket TCPServerSocket::accept(){
	if(!*this){
		throw setka::Exc("TCPServerSocket::Accept(): the socket is not opened");
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

	TCPSocket sock;//allocate a new socket object

	sock.socket = ::accept(
			this->socket,
			reinterpret_cast<sockaddr*>(&sockAddr),
			&sock_alen
		);

	if(sock.socket == DInvalidSocket()){
		return sock;//no connections to be accepted, return invalid socket
	}

#if M_OS == M_OS_WINDOWS
	sock.createEventForWaitable();

	//NOTE: accepted socket is associated with the same event object as the listening socket which accepted it.
	//Re-associate the socket with its own event object.
	sock.setWaitingEvents(0);
#endif

	sock.setNonBlockingMode();

	if(this->disableNaggle){
		sock.disableNaggle();
	}

	return sock;//return a newly created socket
}



#if M_OS == M_OS_WINDOWS
//override
void TCPServerSocket::setWaitingEvents(std::uint32_t flagsToWaitFor){
	if(flagsToWaitFor != 0 && flagsToWaitFor != Waitable::READ){
		throw setka::Exc("TCPServerSocket::SetWaitingEvents(): only Waitable::READ flag allowed");
	}

	long flags = FD_CLOSE;
	if((flagsToWaitFor & Waitable::READ) != 0){
		flags |= FD_ACCEPT;
	}
	this->setWaitingEventsForWindows(flags);
}
#endif
