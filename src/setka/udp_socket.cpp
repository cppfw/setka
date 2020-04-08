#include "udp_socket.hpp"

#include <limits>
#include <cstring>

#if M_OS == M_OS_LINUX || M_OS == M_OS_MACOSX || M_OS == M_OS_UNIX
#	include <netinet/in.h>
#endif

using namespace setka;

void udp_socket::open(uint16_t port){
	if(*this){
		throw std::logic_error("udp_socket::Open(): the socket is already opened");
	}

#if M_OS == M_OS_WINDOWS
	this->create_event_for_waitable();
#endif

	this->ipv4 = false;
	
	this->sock = ::socket(PF_INET6, SOCK_DGRAM, 0);
	
	if(this->sock == invalid_socket){
		// maybe IPv6 is not supported by OS, try to proceed with IPv4 socket then
		this->sock = ::socket(PF_INET, SOCK_DGRAM, 0);

		if(this->sock == invalid_socket){
#if M_OS == M_OS_WINDOWS
			this->closeEventForWaitable();
#endif
			// TODO: use std::system_error?
			throw std::runtime_error("tcp_server_socket::Open(): Couldn't create socket");
		}

		this->ipv4 = true;
	}
	
	// turn off IPv6 only mode to allow also accepting IPv4 connections
	if(!this->ipv4){
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
			
			this->sock = ::socket(PF_INET, SOCK_DGRAM, 0);
	
			if(this->sock == invalid_socket){
#if M_OS == M_OS_WINDOWS
				this->close_event_for_waitable();
#endif
				// TODO: use std::system_error?
				throw std::runtime_error("tcp_server_socket::Open(): Couldn't create socket");
			}
			
			this->ipv4 = true;
		}
	}
	
	// bind locally, if appropriate
	if(port != 0){
		sockaddr_storage sockAddr;
		socklen_t sockAddrLen;

		if(this->ipv4){
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
			sa.sin6_addr = in6addr_any; // 'in6addr_any' allows accepting both IPv4 and IPv6 connections
			sa.sin6_port = htons(port);
			sockAddrLen = sizeof(sa);
		}

		// bind the socket for listening
		if(::bind(
				this->sock,
				reinterpret_cast<struct sockaddr*>(&sockAddr),
				sockAddrLen
			) == socket_error)
		{
			this->close();
			
#if M_OS == M_OS_WINDOWS
			int errorCode = WSAGetLastError();
#else
			int errorCode = errno;
#endif
				std::stringstream ss;
				ss << "udp_socket::Open(): could not bind to local port, error code = " << errorCode << ": ";
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
			// TODO: use std::system_error?
			throw std::runtime_error(ss.str());
		}
	}

	this->set_nonblocking_mode();

	// allow broadcasting
#if M_OS == M_OS_WINDOWS || M_OS == M_OS_LINUX || M_OS == M_OS_MACOSX || M_OS == M_OS_UNIX
	{
		int yes = 1;
		if(setsockopt(
				this->sock,
				SOL_SOCKET,
				SO_BROADCAST,
				reinterpret_cast<char*>(&yes),
				sizeof(yes)
			) == socket_error)
		{
			this->close();
			// TODO: use std::system_error?
			throw std::runtime_error("udp_socket::Open(): failed setting broadcast option");
		}
	}
#else
#	error "Unsupported OS"
#endif

	this->readiness_flags.clear();
}

size_t udp_socket::send(const utki::span<uint8_t> buf, const ip_address& destination_address){
	if(!*this){
		throw std::logic_error("udp_socket::Send(): socket is not opened");
	}

	this->readiness_flags.clear(opros::ready::write);

	sockaddr_storage sockAddr;
	socklen_t sockAddrLen;
	
	if(
#if M_OS == M_OS_MACOSX || M_OS == M_OS_WINDOWS
			this->ipv4 &&
#endif
			destination_address.host.is_v4()
		)
	{
		sockaddr_in& a = reinterpret_cast<sockaddr_in&>(sockAddr);
		memset(&a, 0, sizeof(a));
		a.sin_family = AF_INET;
		a.sin_addr.s_addr = htonl(destination_address.host.get_v4());
		a.sin_port = htons(destination_address.port);
		sockAddrLen = sizeof(a);
	}else{
		sockaddr_in6& a = reinterpret_cast<sockaddr_in6&>(sockAddr);
		memset(&a, 0, sizeof(a));
		a.sin6_family = AF_INET6;
#if M_OS == M_OS_MACOSX || M_OS == M_OS_WINDOWS || (M_OS == M_OS_LINUX && M_OS_NAME == M_OS_NAME_ANDROID)
		a.sin6_addr.s6_addr[0] = destination_address.host.quad[0] >> 24;
		a.sin6_addr.s6_addr[1] = (destination_address.host.quad[0] >> 16) & 0xff;
		a.sin6_addr.s6_addr[2] = (destination_address.host.quad[0] >> 8) & 0xff;
		a.sin6_addr.s6_addr[3] = destination_address.host.quad[0] & 0xff;
		a.sin6_addr.s6_addr[4] = destination_address.host.quad[1] >> 24;
		a.sin6_addr.s6_addr[5] = (destination_address.host.quad[1] >> 16) & 0xff;
		a.sin6_addr.s6_addr[6] = (destination_address.host.quad[1] >> 8) & 0xff;
		a.sin6_addr.s6_addr[7] = destination_address.host.quad[1] & 0xff;
		a.sin6_addr.s6_addr[8] = destination_address.host.quad[2] >> 24;
		a.sin6_addr.s6_addr[9] = (destination_address.host.quad[2] >> 16) & 0xff;
		a.sin6_addr.s6_addr[10] = (destination_address.host.quad[2] >> 8) & 0xff;
		a.sin6_addr.s6_addr[11] = destination_address.host.quad[2] & 0xff;
		a.sin6_addr.s6_addr[12] = destination_address.host.quad[3] >> 24;
		a.sin6_addr.s6_addr[13] = (destination_address.host.quad[3] >> 16) & 0xff;
		a.sin6_addr.s6_addr[14] = (destination_address.host.quad[3] >> 8) & 0xff;
		a.sin6_addr.s6_addr[15] = destination_address.host.quad[3] & 0xff;
#else
		a.sin6_addr.__in6_u.__u6_addr32[0] = htonl(destination_address.host.quad[0]);
		a.sin6_addr.__in6_u.__u6_addr32[1] = htonl(destination_address.host.quad[1]);
		a.sin6_addr.__in6_u.__u6_addr32[2] = htonl(destination_address.host.quad[2]);
		a.sin6_addr.__in6_u.__u6_addr32[3] = htonl(destination_address.host.quad[3]);
#endif
		a.sin6_port = htons(destination_address.port);
		sockAddrLen = sizeof(a);
	}

#if M_OS == M_OS_WINDOWS
	int len;
#else
	ssize_t len;
#endif

	while(true){
		len = ::sendto(
				this->sock,
				reinterpret_cast<const char*>(buf.begin()),
				int(buf.size()),
				0,
				reinterpret_cast<struct sockaddr*>(&sockAddr),
				sockAddrLen
			);

		if(len == socket_error){
#if M_OS == M_OS_WINDOWS
			int errorCode = WSAGetLastError();
			
			if(errorCode == WSAEAFNOSUPPORT){
				// TODO: use std::system_error?
				throw std::runtime_error("Address family is not supported by protocol family. Note, that libting on WinXP does not support IPv6.");
			}
#else
			int errorCode = errno;
#endif
			if(errorCode == error_interrupted){
				continue;
			}else if(errorCode == error_again){
				// can't send more bytes, return 0 bytes sent
				len = 0;
			}else{
				std::stringstream ss;
				ss << "udp_socket::Send(): sendto() failed, error code = " << errorCode << ": ";
#if M_COMPILER == M_COMPILER_MSVC
				{
					const size_t msgbufSize = 0xff;
					char msgbuf[msgbufSize];
					strerror_s(msgbuf, msgbufSize, errorCode);
					msgbuf[msgbufSize - 1] = 0; // make sure the string is null-terminated
					ss << msgbuf;
				}
#else
				ss << strerror(errorCode);
#endif
				// TODO: use std::system_error?
				throw std::runtime_error(ss.str());
			}
		}
		break;
	}

	ASSERT(buf.size() <= size_t(std::numeric_limits<int>::max()))
	ASSERT_INFO(len <= int(buf.size()), "res = " << len)
	ASSERT_INFO((len == int(buf.size())) || (len == 0), "res = " << len)

	ASSERT(len >= 0)
	return size_t(len);
}

size_t udp_socket::recieve(utki::span<uint8_t> buf, ip_address &out_sender_address){
	if(!*this){
		throw std::logic_error("udp_socket::Recv(): socket is not opened");
	}

	// The "can read" flag shall be cleared even if this function fails.
	// This is to avoid subsequent calls to Recv() because of it indicating
	// that there's an activity.
	// So, do it at the beginning of the function.
	this->readiness_flags.clear(opros::ready::read);

	sockaddr_storage sockAddr;

#if M_OS == M_OS_WINDOWS
	int sockLen = sizeof(sockAddr);
#elif M_OS == M_OS_LINUX || M_OS == M_OS_MACOSX || M_OS == M_OS_UNIX
	socklen_t sockLen = sizeof(sockAddr);
#else
#	error "Unsupported OS"
#endif

#if M_OS == M_OS_WINDOWS
	int len;
#else
	ssize_t len;
#endif

	while(true){
		len = ::recvfrom(
				this->sock,
				reinterpret_cast<char*>(buf.data()),
				int(buf.size()),
				0,
				reinterpret_cast<sockaddr*>(&sockAddr),
				&sockLen
			);

		if(len == socket_error){
#if M_OS == M_OS_WINDOWS
			int errorCode = WSAGetLastError();
#else
			int errorCode = errno;
#endif
			if(errorCode == error_interrupted){
				continue;
			}else if(errorCode == error_again){
				return 0; // no data available, return 0 bytes received
			}else{
				std::stringstream ss;
				ss << "udp_socket::Recv(): recvfrom() failed, error code = " << errorCode << ": ";
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
				// TODO: use std::system_error?
				throw std::runtime_error(ss.str());
			}
		}
		break;
	}

	ASSERT(buf.size() <= size_t(std::numeric_limits<int>::max()))
	ASSERT_INFO(len <= int(buf.size()), "len = " << len)

	if(sockAddr.ss_family == AF_INET){
		sockaddr_in& a = reinterpret_cast<sockaddr_in&>(sockAddr);
		out_sender_address = ip_address(
				ntohl(a.sin_addr.s_addr),
				uint16_t(ntohs(a.sin_port))
			);
	}else{
		ASSERT_INFO(sockAddr.ss_family == AF_INET6, "sockAddr.ss_family = " << unsigned(sockAddr.ss_family) << " AF_INET = " << AF_INET << " AF_INET6 = " << AF_INET6)
		sockaddr_in6& a = reinterpret_cast<sockaddr_in6&>(sockAddr);
		out_sender_address = ip_address(
				ip_address::ip(
#if M_OS == M_OS_MACOSX || M_OS == M_OS_WINDOWS || (M_OS == M_OS_LINUX && M_OS_NAME == M_OS_NAME_ANDROID)
						(uint32_t(a.sin6_addr.s6_addr[0]) << 24) | (uint32_t(a.sin6_addr.s6_addr[1]) << 16) | (uint32_t(a.sin6_addr.s6_addr[2]) << 8) | uint32_t(a.sin6_addr.s6_addr[3]),
						(uint32_t(a.sin6_addr.s6_addr[4]) << 24) | (uint32_t(a.sin6_addr.s6_addr[5]) << 16) | (uint32_t(a.sin6_addr.s6_addr[6]) << 8) | uint32_t(a.sin6_addr.s6_addr[7]),
						(uint32_t(a.sin6_addr.s6_addr[8]) << 24) | (uint32_t(a.sin6_addr.s6_addr[9]) << 16) | (uint32_t(a.sin6_addr.s6_addr[10]) << 8) | uint32_t(a.sin6_addr.s6_addr[11]),
						(uint32_t(a.sin6_addr.s6_addr[12]) << 24) | (uint32_t(a.sin6_addr.s6_addr[13]) << 16) | (uint32_t(a.sin6_addr.s6_addr[14]) << 8) | uint32_t(a.sin6_addr.s6_addr[15])
#else
						uint32_t(ntohl(a.sin6_addr.__in6_u.__u6_addr32[0])),
						uint32_t(ntohl(a.sin6_addr.__in6_u.__u6_addr32[1])),
						uint32_t(ntohl(a.sin6_addr.__in6_u.__u6_addr32[2])),
						uint32_t(ntohl(a.sin6_addr.__in6_u.__u6_addr32[3]))
#endif
					),
				uint16_t(ntohs(a.sin6_port))
			);
	}
	
	ASSERT(len >= 0)
	return size_t(len);
}

#if M_OS == M_OS_WINDOWS
void udp_socket::set_waiting_events(uint32_t flagsToWaitFor){
	long flags = FD_CLOSE;
	if((flagsToWaitFor & Waitable::READ) != 0){
		flags |= FD_READ;
	}
	if((flagsToWaitFor & Waitable::WRITE) != 0){
		flags |= FD_WRITE;
	}
	this->set_waiting_events_for_windows(flags);
}
#endif
