/**
 * @author Ivan Gagis <igagis@gmail.com>
 */

#pragma once


#include "Socket.hpp"
#include "TCPSocket.hpp"



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
class TCPServerSocket : public Socket{
	bool disableNaggle = false;//this flag indicates if accepted sockets should be created with disabled Naggle
public:
	/**
	 * @brief Creates an invalid (unopened) TCP server socket.
	 */
	TCPServerSocket(){}

	
	

	TCPServerSocket(const TCPServerSocket&) = delete;
	
	TCPServerSocket(TCPServerSocket&& s) :
			Socket(std::move(s)),
			disableNaggle(s.disableNaggle)
	{}

	
	
	TCPServerSocket& operator=(const TCPServerSocket&) = delete;
	
	TCPServerSocket& operator=(TCPServerSocket&& s){
		this->disableNaggle = s.disableNaggle;
		this->Socket::operator=(std::move(s));
		return *this;
	}

	
	
	/**
	 * @brief Connects the socket or starts listening on it.
	 * This method starts listening on the socket for incoming connections.
	 * @param port - IP port number to listen on.
	 * @param disableNaggle - enable/disable Naggle algorithm for all accepted connections.
	 * @param queueLength - the maximum length of the queue of pending connections.
	 */
	void open(std::uint16_t port, bool disableNaggle = false, std::uint16_t queueLength = 50);
	
	
	
	/**
	 * @brief Accepts one of the pending connections, non-blocking.
	 * Accepts one of the pending connections and returns a TCP socket object which represents
	 * either a valid connected socket or an invalid socket object.
	 * This function does not block if there is no any pending connections, it just returns invalid
	 * socket object in this case. One can periodically check for incoming connections by calling this method.
	 * @return TCPSocket object. One can later check if the returned socket object
	 *         is valid or not by calling Socket::IsValid() method on that object.
	 *         - if the socket is valid then it is a newly connected socket, further it can be used to send or receive data.
	 *         - if the socket is invalid then there was no any connections pending, so no connection was accepted.
	 */
	TCPSocket accept();



#if M_OS == M_OS_WINDOWS
private:
	void setWaitingEvents(std::uint32_t flagsToWaitFor)override;
#endif
};//~class TCPServerSocket



}//~namespace
