/* The MIT License:

Copyright (c) 2009-2014 Ivan Gagis <igagis@gmail.com>

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
namespace ting{
namespace net{



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
	void Open(std::uint16_t port, bool disableNaggle = false, std::uint16_t queueLength = 50);
	
	
	
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
	TCPSocket Accept();



#if M_OS == M_OS_WINDOWS
private:
	void SetWaitingEvents(std::uint32_t flagsToWaitFor)override;
#endif
};//~class TCPServerSocket



}//~namespace
}//~namespace
