/**
 * @author Ivan Gagis <igagis@gmail.com>
 */

#pragma once


#include <string>
#include <sstream>

#include <utki/config.hpp>
#include <utki//debug.hpp>



#if M_OS == M_OS_WINDOWS
#	include <winsock2.h>
#	include <ws2tcpip.h>
#	include <utki/windows.hpp>

//On older windows SDKs IPV6_V6ONLY is not defined, let's define it here if necessary.
#	ifndef IPV6_V6ONLY
#		define IPV6_V6ONLY 27
#	endif

#elif M_OS == M_OS_LINUX || M_OS == M_OS_MACOSX || M_OS == M_OS_UNIX
#	include <sys/socket.h>

#else
#	error "Unsupported OS"
#endif



#include "Exc.hpp"

#include <opros/waitable.hpp>



/**
 * @brief the main namespace of ting library.
 * All the declarations of ting library are made inside this namespace.
 */
namespace setka{




/**
 * @brief Basic socket class.
 * This is a base class for all socket types such as TCP sockets or UDP sockets.
 */
class Socket : public opros::waitable{
protected:
#if M_OS == M_OS_WINDOWS
	typedef SOCKET T_Socket;

	static T_Socket DInvalidSocket(){
		return INVALID_SOCKET;
	}

	static int DSocketError(){
		return SOCKET_ERROR;
	}

	static int DEIntr(){
		return WSAEINTR;
	}
	
	static int DEAgain(){
		return WSAEWOULDBLOCK;
	}

	static int DEInProgress(){
		return WSAEWOULDBLOCK;
	}

#elif M_OS == M_OS_LINUX || M_OS == M_OS_MACOSX || M_OS == M_OS_UNIX
	typedef int T_Socket;

	static T_Socket DInvalidSocket(){
		return -1;
	}

	static T_Socket DSocketError(){
		return -1;
	}

	static int DEIntr(){
		return EINTR;
	}

	static int DEAgain(){
		return EAGAIN;
	}

	static int DEInProgress(){
		return EINPROGRESS;
	}
#else
#	error "Unsupported OS"
#endif

#if M_OS == M_OS_WINDOWS
	WSAEVENT eventForWaitable;
#endif

	T_Socket socket;

	Socket() :
#if M_OS == M_OS_WINDOWS
			eventForWaitable(WSA_INVALID_EVENT),
#endif
			socket(DInvalidSocket())
	{
//		TRACE(<< "Socket::Socket(): invoked " << this << std::endl)
	}



	Socket& operator=(Socket&& s);



	void disableNaggle();



	void setNonBlockingMode();



public:
	Socket(Socket&& s) :
			//NOTE: operator=() will call Close, so the socket should be in invalid state!!!
			//Therefore, initialize variables to invalid values.
			Socket()
	{
		this->operator=(std::move(s));
	}

	
	
	virtual ~Socket()noexcept;



	/**
	 * @brief Tells whether the socket is opened or not.
	 * @return Returns true if the socket is opened or false otherwise.
	 */
	explicit operator bool()const noexcept{
		return this->socket != DInvalidSocket();
	}




	/**
	 * @brief Closes the socket disconnecting it if necessary.
	 */
	void close()noexcept;



	/**
	 * @brief Returns local port this socket is bound to.
	 * @return local port number to which this socket is bound,
	 *         0 means that the socket is not bound to a port.
	 */
	std::uint16_t getLocalPort();



#if M_OS == M_OS_WINDOWS
private:
	HANDLE getHandle()override;

	bool checkSignaled()override;

protected:
	void createEventForWaitable();

	void closeEventForWaitable();

	void setWaitingEventsForWindows(long flags);



#elif M_OS == M_OS_LINUX || M_OS == M_OS_MACOSX || M_OS == M_OS_UNIX
private:
	int get_handle()override;
#else
#	error "Unsupported OS"
#endif
};




}//~namespace
