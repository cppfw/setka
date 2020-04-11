#pragma once

#include <utki/config.hpp>

#include "socket.hpp"
#include "tcp_socket.hpp"

/**
 * @brief the main namespace of ting library.
 * All the declarations of ting library are made inside this namespace.
 */
namespace setka{

/**
 * @brief a class which represents a TCP server socket.
 * TCP server socket is the socket which can listen for new connections
 * and accept them creating an ordinary TCP socket for it.
 */
class tcp_server_socket : public socket{
	bool disable_naggle = false; // this flag indicates if accepted sockets should be created with disabled Naggle
public:
	/**
	 * @brief Creates an invalid (unopened) TCP server socket.
	 */
	tcp_server_socket(){}

	tcp_server_socket(const tcp_server_socket&) = delete;
	tcp_server_socket& operator=(const tcp_server_socket&) = delete;
	
	tcp_server_socket(tcp_server_socket&& s) :
			socket(std::move(s)),
			disable_naggle(s.disable_naggle)
	{}
	
	tcp_server_socket& operator=(tcp_server_socket&& s){
		this->disable_naggle = s.disable_naggle;
		this->socket::operator=(std::move(s));
		return *this;
	}

	/**
	 * @brief Connects the socket or starts listening on it.
	 * This method starts listening on the socket for incoming connections.
	 * @param port - IP port number to listen on.
	 * @param disable_naggle - enable/disable Naggle algorithm for all accepted connections.
	 * @param queue_size - the maximum number of pending connections.
	 */
	void open(uint16_t port, bool disable_naggle = false, uint16_t queue_size = 50);
	
	/**
	 * @brief Accepts one of the pending connections, non-blocking.
	 * Accepts one of the pending connections and returns a TCP socket object which represents
	 * either a valid connected socket or an invalid socket object.
	 * This function does not block if there is no any pending connections, it just returns invalid
	 * socket object in this case. One can periodically check for incoming connections by calling this method.
	 * @return tcp_socket object. One can later check if the returned socket object
	 *         is valid or not by calling socket::is_valid() method on that object.
	 *         - if the socket is valid then it is a newly connected socket, further it can be used to send or receive data.
	 *         - if the socket is invalid then there was no any connections pending, so no connection was accepted.
	 */
	tcp_socket accept();

#if M_OS == M_OS_WINDOWS
private:
	void set_waiting_flags(utki::flags<opros::ready> waiting_flags)override;
#endif
};
}
