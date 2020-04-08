#pragma once

#include <utki/Buf.hpp>
#include <utki/config.hpp>

#include "socket.hpp"
#include "ip_address.hpp"

namespace setka{

class TCPServerSocket;

/**
 * @brief a class which represents a TCP socket.
 */
class TCPSocket : public socket{
	friend class setka::TCPServerSocket;
public:
	
	/**
	 * @brief Constructs an invalid TCP socket object.
	 */
	TCPSocket(){
//		TRACE(<< "TCPSocket::TCPSocket(): invoked " << this << std::endl)
	}

	
	
	TCPSocket(const TCPSocket&) = delete;
	
	TCPSocket(TCPSocket&& s) :
			socket(std::move(s))
	{}

	
	
	TCPSocket& operator=(const TCPSocket&) = delete;
	
	
	TCPSocket& operator=(TCPSocket&& s){
		this->socket::operator=(std::move(s));
		return *this;
	}

	
	
	/**
	 * @brief Connects the socket.
	 * This method connects the socket to remote TCP server socket.
	 * @param ip - IP address.
	 * @param disableNaggle - enable/disable Naggle algorithm.
	 */
	void open(const ip_address& ip, bool disableNaggle = false);



	/**
	 * @brief Send data to connected socket.
	 * Sends data on connected socket. This method does not guarantee that the whole
	 * buffer will be sent completely, it will return the number of bytes actually sent.
	 * @param buf - pointer to the buffer with data to send.
	 * @return the number of bytes actually sent.
	 */
	size_t send(const utki::Buf<uint8_t> buf);



	/**
	 * @brief Receive data from connected socket.
	 * Receives data available on the socket.
	 * If there is no data available this function does not block, instead it returns 0,
	 * indicating that 0 bytes were received.
	 * If previous WaitSet::Wait() indicated that socket is ready for reading
	 * and TCPSocket::Recv() returns 0, then connection was closed by peer.
	 * @param buf - pointer to the buffer where to put received data.
	 * @return the number of bytes written to the buffer.
	 */
	size_t recieve(utki::Buf<uint8_t> buf);

	
	
	/**
	 * @brief Get local IP address and port.
	 * @return IP address and port of the local socket.
	 */
	ip_address getLocalAddress();
	
	
	
	/**
	 * @brief Get remote IP address and port.
	 * @return IP address and port of the peer socket.
	 */
	ip_address getRemoteAddress();



#if M_OS == M_OS_WINDOWS
private:
	void setWaitingEvents(uint32_t flagsToWaitFor)override;
#endif

};//~class TCPSocket



}//~namespace
