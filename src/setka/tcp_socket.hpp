#pragma once

#include <utki/config.hpp>
#include <utki/span.hpp>

#include "socket.hpp"
#include "address.hpp"

namespace setka{

class tcp_server_socket;

/**
 * @brief a class which represents a TCP socket.
 */
class tcp_socket : public socket{
	friend class setka::tcp_server_socket;
public:
	
	/**
	 * @brief Constructs an invalid TCP socket object.
	 */
	tcp_socket(){
//		TRACE(<< "tcp_socket::tcp_socket(): invoked " << this << std::endl)
	}
	
	tcp_socket(const tcp_socket&) = delete;
	tcp_socket& operator=(const tcp_socket&) = delete;
	
	tcp_socket(tcp_socket&& s) :
			socket(std::move(s))
	{}

	tcp_socket& operator=(tcp_socket&& s){
		this->socket::operator=(std::move(s));
		return *this;
	}
	
	/**
	 * @brief Connects the socket.
	 * This method connects the socket to remote TCP server socket.
	 * @param address - IP address.
	 * @param disable_naggle - enable/disable Naggle algorithm.
	 */
	void open(const address& address, bool disable_naggle = false);

	/**
	 * @brief Send data to connected socket.
	 * Sends data on connected socket. This method does not guarantee that the whole
	 * buffer will be sent completely, it will return the number of bytes actually sent.
	 * @param buf - pointer to the buffer with data to send.
	 * @return the number of bytes actually sent.
	 */
	size_t send(const utki::span<uint8_t> buf);

	/**
	 * @brief Receive data from connected socket.
	 * Receives data available on the socket.
	 * If there is no data available this function does not block, instead it returns 0,
	 * indicating that 0 bytes were received.
	 * If previous WaitSet::Wait() indicated that socket is ready for reading
	 * and tcp_socket::Recv() returns 0, then connection was closed by peer.
	 * @param buf - pointer to the buffer where to put received data.
	 * @return the number of bytes written to the buffer.
	 */
	size_t recieve(utki::span<uint8_t> buf);

	/**
	 * @brief Get local IP address and port.
	 * @return IP address and port of the local socket.
	 */
	address get_local_address();

	/**
	 * @brief Get remote IP address and port.
	 * @return IP address and port of the peer socket.
	 */
	address get_remote_address();

#if M_OS == M_OS_WINDOWS
private:
	void set_waiting_flags(utki::flags<opros::ready> waiting_flags)override;
#endif
};
}
