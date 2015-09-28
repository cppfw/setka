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



#include "TCPServerSocket.hpp"

#if M_OS == M_OS_LINUX || M_OS == M_OS_MACOSX || M_OS == M_OS_UNIX
#	include <netinet/in.h>
#endif


using namespace ting::net;



void TCPServerSocket::Open(std::uint16_t port, bool disableNaggle, std::uint16_t queueLength){
	if(*this){
		throw net::Exc("TCPServerSocket::Open(): socket already opened");
	}

	this->disableNaggle = disableNaggle;

#if M_OS == M_OS_WINDOWS
	this->CreateEventForWaitable();
#endif

	bool ipv4 = false;
	
	this->socket = ::socket(PF_INET6, SOCK_STREAM, 0);
	
	if(this->socket == DInvalidSocket()){
		//maybe IPv6 is not supported by OS, try creating IPv4 socket
		
		this->socket = ::socket(PF_INET, SOCK_STREAM, 0);

		if(this->socket == DInvalidSocket()){
#if M_OS == M_OS_WINDOWS
			this->CloseEventForWaitable();
#endif
			throw net::Exc("TCPServerSocket::Open(): Couldn't create IPv4 socket");
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
			
			this->Close();//close IPv6 socket
			
			//create IPv4 socket
			
#if M_OS == M_OS_WINDOWS
			this->CreateEventForWaitable();
#endif			
			
			this->socket = ::socket(PF_INET, SOCK_STREAM, 0);
	
			if(this->socket == DInvalidSocket()){
#if M_OS == M_OS_WINDOWS
				this->CloseEventForWaitable();
#endif
				throw net::Exc("TCPServerSocket::Open(): Couldn't create IPv4 socket");
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
		this->Close();
		throw net::Exc(ss.str());
	}

	if(listen(this->socket, int(queueLength)) == DSocketError()){
		this->Close();
		throw net::Exc("TCPServerSocket::Open(): Couldn't listen to local port");
	}

	this->SetNonBlockingMode();
}



TCPSocket TCPServerSocket::Accept(){
	if(!*this){
		throw net::Exc("TCPServerSocket::Accept(): the socket is not opened");
	}

	this->ClearCanReadFlag();

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
	sock.CreateEventForWaitable();

	//NOTE: accepted socket is associated with the same event object as the listening socket which accepted it.
	//Re-associate the socket with its own event object.
	sock.SetWaitingEvents(0);
#endif

	sock.SetNonBlockingMode();

	if(this->disableNaggle){
		sock.DisableNaggle();
	}

	return sock;//return a newly created socket
}



#if M_OS == M_OS_WINDOWS
//override
void TCPServerSocket::SetWaitingEvents(std::uint32_t flagsToWaitFor){
	if(flagsToWaitFor != 0 && flagsToWaitFor != Waitable::READ){
		throw ting::Exc("TCPServerSocket::SetWaitingEvents(): only Waitable::READ flag allowed");
	}

	long flags = FD_CLOSE;
	if((flagsToWaitFor & Waitable::READ) != 0){
		flags |= FD_ACCEPT;
	}
	this->SetWaitingEventsForWindows(flags);
}
#endif
