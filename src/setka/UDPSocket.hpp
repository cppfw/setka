/**
 * @author Ivan Gagis <igagis@gmail.com>
 */

#pragma once


#include <string>

#include <utki/config.hpp>
#include <utki/Buf.hpp>

#include "Socket.hpp"
#include "IPAddress.hpp"



namespace setka{



/**
 * @brief UDP socket class.
 * Socket for User Datagram Protocol.
 * NOTE: Win32 specific: when using UDP socket with WaitSet be aware that waiting on UDP socket for writing does not work on Win32 OS.
 *       On other operating systems it works OK.
 */
class DLLEXPORT UDPSocket : public Socket{
	bool ipv4;
public:
	UDPSocket(){}

	UDPSocket(const UDPSocket&) = delete;

	UDPSocket(UDPSocket&& s) :
			Socket(std::move(s))
	{}


	UDPSocket& operator=(UDPSocket&& s){
		this->Socket::operator=(std::move(s));
		this->ipv4 = s.ipv4;
		return *this;
	}


	
	/**
	 * @brief Open the socket.
	 * This method opens the socket, this socket can further be used to send or receive data.
	 * After the socket is opened it becomes a valid socket and Socket::IsValid() will return true for such socket.
	 * After the socket is closed it becomes invalid.
	 * In other words, a valid socket is an opened socket.
	 * In case of errors this method throws net::Exc.
	 * @param port - IP port number on which the socket will listen for incoming datagrams.
	 *               If 0 is passed then system will assign some free port if any. If there
	 *               are no free ports, then it is an error and an exception will be thrown.
	 *               This is useful for server-side sockets, for client-side sockets use UDPSocket::Open().
	 */
	void open(std::uint16_t port = 0);



	/**
	 * @brief Send datagram over UDP socket.
	 * The datagram is sent to UDP socket all at once. If the datagram cannot be
	 * sent at once at the current moment, 0 will be returned.
	 * Note, that underlying protocol limits the maximum size of the datagram,
	 * trying to send the bigger datagram will result in an exception to be thrown.
	 * @param buf - buffer containing the datagram to send.
	 * @param destinationIP - the destination IP address to send the datagram to.
	 * @return number of bytes actually sent. Actually it is either 0 or the size of the
	 *         datagram passed in as argument.
	 */
	size_t send(const utki::Buf<std::uint8_t> buf, const IPAddress& destinationIP);



	/**
	 * @brief Receive datagram.
	 * Writes a datagram to the given buffer at once if it is available.
	 * If there is no received datagram available a 0 will be returned.
	 * Note, that it will always write out the whole datagram at once. I.e. it is either all or nothing.
	 * Except for the case when the given buffer is not large enough to store the datagram,
	 * in which case the datagram is truncated to the size of the buffer and the rest of the data is lost.
	 * @param buf - reference to the buffer the received datagram will be stored to. The buffer
	 *              should be large enough to store the whole datagram. If datagram
	 *              does not fit the passed buffer, then the datagram tail will be truncated
	 *              and this tail data will be lost.
	 * @param out_SenderIP - reference to the IP-address structure where the IP-address
	 *                       of the sender will be stored.
	 * @return number of bytes stored in the output buffer.
	 */
	size_t recieve(utki::Buf<std::uint8_t> buf, IPAddress &out_SenderIP);



#if M_OS == M_OS_WINDOWS
private:
	void setWaitingEvents(std::uint32_t flagsToWaitFor)override;
#endif
};



}//~namespace
