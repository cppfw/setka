#include "../../src/ting/timer.hpp"
#include "../../src/ting/mt/Thread.hpp"
#include "../../src/ting/mt/MsgThread.hpp"
#include "../../src/ting/net/TCPSocket.hpp"
#include "../../src/ting/net/TCPServerSocket.hpp"
#include "../../src/ting/net/UDPSocket.hpp"
#include "../../src/ting/WaitSet.hpp"
#include "../../src/ting/Buffer.hpp"
#include "../../src/ting/config.hpp"
#include "../../src/ting/util.hpp"

#include "socket.hpp"



namespace{

bool IsIPv6SupportedByOS(){
#if M_OS == M_OS_WINDOWS
	{
		OSVERSIONINFO osvi;
		memset(&osvi, 0, sizeof(osvi));
		osvi.dwOSVersionInfoSize = sizeof(osvi);

		GetVersionEx(&osvi);

		if(osvi.dwMajorVersion > 5){
			return true;
		}else{
			return false;
		}
	}
#else
	return true;
#endif
}

}


namespace BasicClientServerTest{

void SendAll(ting::net::TCPSocket& s, ting::Buffer<std::uint8_t> buf){
	if(!s){
		throw ting::net::Exc("TCPSocket::Send(): socket is not opened");
	}

	DEBUG_CODE(int left = int(buf.size());)
	ASSERT(left >= 0)

	size_t offset = 0;

	while(true){
		int res = s.Send(decltype(buf)(&*buf.begin() + offset, buf.size() - offset));
		DEBUG_CODE(left -= res;)
		ASSERT(left >= 0)
		offset += res;
		if(offset == buf.size()){
			break;
		}
		//give 30ms to allow data from send buffer to be sent
		ting::mt::Thread::Sleep(30);
	}

	ASSERT(left == 0)
}


class ServerThread : public ting::mt::MsgThread{
public:
	//override
	void Run(){
		try{
			ting::net::TCPServerSocket listenSock;

			listenSock.Open(13666);//start listening

			ASSERT_ALWAYS(listenSock.GetLocalPort() == 13666)

			//Accept some connection
			ting::net::TCPSocket sock;
			while(!sock && !this->quitFlag){
				sock = listenSock.Accept();
				ting::mt::Thread::Sleep(100);
				if(auto m = this->queue.PeekMsg()){
					m();
				}
			}

			ASSERT_ALWAYS(sock)

			ASSERT_ALWAYS(sock.GetLocalAddress().host.IPv4Host() == 0x7f000001)
			ASSERT_ALWAYS(sock.GetRemoteAddress().host.IPv4Host() == 0x7f000001)

			std::array<std::uint8_t, 4> data;
			data[0] = '0';
			data[1] = '1';
			data[2] = '2';
			data[3] = '4';
			SendAll(sock, data);
		}catch(ting::net::Exc &e){
			ASSERT_INFO_ALWAYS(false, "Network error: " << e.What())
		}
	}
};



void Run(){
	ServerThread serverThread;
	
	serverThread.Start();
	
	ting::mt::Thread::Sleep(1000);
	
	try{
		ting::net::IPAddress ip("127.0.0.1", 13666);

		ting::net::TCPSocket sock;

		sock.Open(ip);

		ASSERT_ALWAYS(sock)

		ting::mt::Thread::Sleep(1000);//give some time for socket to connect
		
		ASSERT_ALWAYS(sock.GetRemoteAddress().host.IPv4Host() == 0x7f000001)

		std::array<std::uint8_t, 4> data;
		unsigned bytesReceived = 0;
		for(unsigned i = 0; i < 30; ++i){
			ASSERT_ALWAYS(bytesReceived < 4)
			bytesReceived += sock.Recv(ting::Buffer<std::uint8_t>(&*data.begin() + bytesReceived, data.size() - bytesReceived));
			ASSERT_ALWAYS(bytesReceived <= 4)
			if(bytesReceived == 4){
				break;
			}

			ting::mt::Thread::Sleep(100);
		}
		ASSERT_ALWAYS(bytesReceived == 4)
		
		ASSERT_ALWAYS(data[0] == '0')
		ASSERT_ALWAYS(data[1] == '1')
		ASSERT_ALWAYS(data[2] == '2')
		ASSERT_ALWAYS(data[3] == '4')
	}catch(ting::net::Exc &e){
		ASSERT_INFO_ALWAYS(false, "Network error: " << e.What())
	}
	
	serverThread.Join();
}

}//~namespace



namespace SendDataContinuouslyWithWaitSet{

void Run(){
	ting::net::TCPServerSocket serverSock;

	serverSock.Open(13666);


	ting::net::TCPSocket sockS;
	{
		ting::net::IPAddress ip("127.0.0.1", 13666);
		sockS.Open(ip);
	}

	//Accept connection
//	TRACE(<< "SendDataContinuously::Run(): accepting connection" << std::endl)
	ting::net::TCPSocket sockR;
	for(unsigned i = 0; i < 20 && !sockR; ++i){
		ting::mt::Thread::Sleep(100);
		sockR = serverSock.Accept();
	}

	ASSERT_ALWAYS(sockS)
	ASSERT_ALWAYS(sockR)

	//Here we have 2 sockets sockS and sockR

	{
		ting::net::IPAddress addrS = sockS.GetRemoteAddress();
		ting::net::IPAddress addrR = sockR.GetRemoteAddress();
//		TRACE(<< "SendDataContinuously::Run(): addrS = " << std::hex << addrS.host << ":" << addrS.port << std::dec << std::endl)
//		TRACE(<< "SendDataContinuously::Run(): addrR = " << std::hex << addrR.host << ":" << addrR.port << std::dec << std::endl)
		ASSERT_ALWAYS(addrS.host.IPv4Host() == 0x7f000001) //check that IP is 127.0.0.1
		ASSERT_ALWAYS(addrR.host.IPv4Host() == 0x7f000001) //check that IP is 127.0.0.1
	}

	ting::WaitSet ws(2);
	ws.Add(sockR, ting::Waitable::READ);
	ws.Add(sockS, ting::Waitable::WRITE);


	std::uint32_t scnt = 0;
	std::vector<std::uint8_t> sendBuffer;
	unsigned bytesSent = 0;

	std::uint32_t rcnt = 0;
	std::array<std::uint8_t, sizeof(std::uint32_t)> recvBuffer;
	unsigned recvBufBytes = 0;


	std::uint32_t startTime = ting::timer::GetTicks();
	
	while(ting::timer::GetTicks() - startTime < 5000){ //5 seconds
		std::array<ting::Waitable*, 2> triggered;

		unsigned numTriggered = ws.WaitWithTimeout(1000, triggered);
//		unsigned numTriggered = ws.Wait(triggered);

		ASSERT_ALWAYS(numTriggered <= 2)

		if(numTriggered == 0){
//			TRACE(<< "SendDataContinuously::Run(): 0 triggered" << std::endl)
			continue;
		}

		//If 2 waitables have triggered they should be 2 different waitables.
		if(numTriggered == 2){
//			TRACE(<< "SendDataContinuously::Run(): 2 triggered" << std::endl)
			ASSERT_ALWAYS(triggered[0] != triggered[1])
		}else{
			ASSERT_ALWAYS(numTriggered == 1)
//			TRACE(<< "SendDataContinuously::Run(): 1 triggered" << std::endl)
		}

		for(unsigned i = 0; i < numTriggered; ++i){
			if(triggered[i] == &sockS){
				ASSERT_ALWAYS(triggered[i] != &sockR)

//				TRACE(<< "SendDataContinuously::Run(): sockS triggered" << std::endl)
				ASSERT_ALWAYS(!sockS.CanRead())
				ASSERT_ALWAYS(!sockS.ErrorCondition())
				ASSERT_ALWAYS(sockS.CanWrite())

				ASSERT_ALWAYS(bytesSent <= sendBuffer.size())

				if(sendBuffer.size() == bytesSent){
					sendBuffer.resize(0xffff + 1);
					bytesSent = 0;
					
					ASSERT_INFO_ALWAYS((sendBuffer.size() % sizeof(std::uint32_t)) == 0,
							"sendBuffer.Size() = " << sendBuffer.size()
							<< " (sendBuffer.Size() % sizeof(std::uint32_t)) = "
							<< (sendBuffer.size() % sizeof(std::uint32_t))
						)

					std::uint8_t* p = &*sendBuffer.begin();
					for(; p != &*sendBuffer.end(); p += sizeof(std::uint32_t)){
						ASSERT_INFO_ALWAYS(p < (&*sendBuffer.end() - (sizeof(std::uint32_t) - 1)), "p = " << p << " sendBuffer.End() = " << &*sendBuffer.end())
						ting::util::Serialize32LE(scnt, p);
						++scnt;
					}
					ASSERT_ALWAYS(p == &*sendBuffer.end())
				}

				ASSERT_ALWAYS(sendBuffer.size() > 0)

				try{
					unsigned res = sockS.Send(ting::Buffer<std::uint8_t>(&*sendBuffer.begin() + bytesSent, sendBuffer.size() - bytesSent));
					bytesSent += res;
					if(res == 0){
						ASSERT_ALWAYS(res > 0) //since it was CanWrite() we should be able to write at least something
					}else{
//						TRACE(<< "SendDataContinuously::Run(): " << res << " bytes sent" << std::endl)
					}
					ASSERT_ALWAYS(!sockS.CanWrite())
				}catch(ting::net::Exc& e){
					ASSERT_INFO_ALWAYS(false, "sockS.Send() failed: " << e.What())
				}
				ASSERT_ALWAYS(bytesSent <= sendBuffer.size())
			}else if(triggered[i] == &sockR){
				ASSERT_ALWAYS(triggered[i] != &sockS)

//				TRACE(<< "SendDataContinuously::Run(): sockR triggered" << std::endl)
				ASSERT_ALWAYS(sockR.CanRead())
				ASSERT_ALWAYS(!sockR.ErrorCondition())
				ASSERT_ALWAYS(!sockR.CanWrite())

				while(true){
					std::array<std::uint8_t, 0x2000> buf; //8kb buffer
					unsigned numBytesReceived;
					try{
						numBytesReceived = sockR.Recv(buf);
					}catch(ting::net::Exc& e){
						ASSERT_INFO_ALWAYS(false, "sockR.Recv() failed: " << e.What())
					}
					ASSERT_ALWAYS(numBytesReceived <= buf.size())
//					TRACE(<< "SendDataContinuously::Run(): " << numBytesReceived << " bytes received" << std::endl)

					if(numBytesReceived == 0){
						break;//~while(true)
					}

					const std::uint8_t* p = buf.begin();
					for(unsigned i = 0; i < numBytesReceived && p != buf.end(); ++p, ++i){
						recvBuffer[recvBufBytes] = *p;
						++recvBufBytes;

						ASSERT_ALWAYS(recvBufBytes <= recvBuffer.size())

						if(recvBufBytes == recvBuffer.size()){
							recvBufBytes = 0;
							std::uint32_t num = ting::util::Deserialize32LE(recvBuffer.begin());
							ASSERT_INFO_ALWAYS(
									rcnt == num,
									"num = " << num << " rcnt = " << rcnt
											<< " rcnt - num = " << (rcnt - num)
											<< " recvBuffer = "
											<< unsigned(recvBuffer[0]) << ", "
											<< unsigned(recvBuffer[1]) << ", "
											<< unsigned(recvBuffer[2]) << ", "
											<< unsigned(recvBuffer[3])
								)
							++rcnt;
						}
					}//~for
				}//~while(true)
			}else{
				ASSERT_ALWAYS(false)
			}
		}//~for(triggered)
	}//~while

	ws.Remove(sockS);
	ws.Remove(sockR);
}

}//~namespace



namespace SendDataContinuously{

void Run(){
	ting::net::TCPServerSocket serverSock;

	serverSock.Open(13666);


	ting::net::TCPSocket sockS;
	{
		ting::net::IPAddress ip("127.0.0.1", 13666);
		sockS.Open(ip);
	}

	//Accept connection
//	TRACE(<< "SendDataContinuously::Run(): accepting connection" << std::endl)
	ting::net::TCPSocket sockR;
	for(unsigned i = 0; i < 20 && !sockR; ++i){
		ting::mt::Thread::Sleep(100);
		sockR = serverSock.Accept();
	}

	ASSERT_ALWAYS(sockS)
	ASSERT_ALWAYS(sockR)

	//Here we have 2 sockets sockS and sockR

	std::uint8_t scnt = 0;

	std::uint8_t rcnt = 0;


	std::uint32_t startTime = ting::timer::GetTicks();

	while(ting::timer::GetTicks() - startTime < 5000){ //5 seconds

		//SEND

		try{
			ting::Buffer<std::uint8_t> buf(&scnt, 1);
			unsigned res = sockS.Send(buf);
			ASSERT_ALWAYS(res <= 1)
			if(res == 1){
				++scnt;
			}else{
				ASSERT_ALWAYS(false)
			}
		}catch(ting::net::Exc& e){
			ASSERT_INFO_ALWAYS(false, "sockS.Send() failed: " << e.What())
		}


		//READ

		while(true){
			std::array<std::uint8_t, 0x2000> buf; //8kb buffer
			unsigned numBytesReceived;
			try{
				numBytesReceived = sockR.Recv(buf);
			}catch(ting::net::Exc& e){
				ASSERT_INFO_ALWAYS(false, "sockR.Recv() failed: " << e.What())
			}
			ASSERT_ALWAYS(numBytesReceived <= buf.size())
//			TRACE(<< "SendDataContinuously::Run(): " << numBytesReceived << " bytes received" << std::endl)

			if(numBytesReceived == 0){
				break;//~while(true)
			}

			const std::uint8_t* p = buf.begin();
			for(unsigned i = 0; i < numBytesReceived && p != buf.end(); ++p, ++i){
				ASSERT_INFO_ALWAYS(rcnt == *p, "rcnt = " << unsigned(rcnt) << " *p = " << unsigned(*p) << " diff = " << unsigned(rcnt - *p))
				++rcnt;
			}//~for
		}//~while(true)
		
	}//~while
}

}//~namespace



namespace BasicIPAddressTest{

void Run(){
	{
		try{
			ting::net::IPAddress a("123.124.125.126", 5);
			ASSERT_ALWAYS(a.host.IPv4Host() == (123 << 24) + (124 << 16) + (125 << 8) + 126)
			ASSERT_ALWAYS(a.port == 5)
		}catch(std::exception& e){
			ASSERT_INFO_ALWAYS(false, e.what())
		}
	}

	{
		ting::net::IPAddress a(123, 124, 125, 126, 5);
		ASSERT_ALWAYS(a.host.IPv4Host() == (123 << 24) + (124 << 16) + (125 << 8) + 126)
		ASSERT_ALWAYS(a.port == 5)
	}

	//test copy constructor and operator=()
	{
		ting::net::IPAddress a(123, 124, 125, 126, 5);
		ASSERT_ALWAYS(a.host.IPv4Host() == (123 << 24) + (124 << 16) + (125 << 8) + 126)
		ASSERT_ALWAYS(a.port == 5)

		ting::net::IPAddress a1(a);
		ASSERT_ALWAYS(a1.host.IPv4Host() == (123 << 24) + (124 << 16) + (125 << 8) + 126)
		ASSERT_ALWAYS(a1.port == 5)

		ting::net::IPAddress a2;
		a2 = a1;
		ASSERT_ALWAYS(a2.host.IPv4Host() == (123 << 24) + (124 << 16) + (125 << 8) + 126)
		ASSERT_ALWAYS(a2.port == 5)

		ASSERT_ALWAYS(a == a1)
		ASSERT_ALWAYS(a == a2)
	}
}

}//~namespace



namespace BasicUDPSocketsTest{

void Run(){

	ting::net::UDPSocket recvSock;

	try{
		recvSock.Open(13666);
	}catch(ting::net::Exc &e){
		ASSERT_INFO_ALWAYS(false, e.What())
	}

	ASSERT_ALWAYS(recvSock.GetLocalPort() == 13666)

	ting::net::UDPSocket sendSock;

	try{
		sendSock.Open();

		std::array<std::uint8_t, 4> data;
		data[0] = '0';
		data[1] = '1';
		data[2] = '2';
		data[3] = '4';
		unsigned bytesSent = 0;

		ting::net::IPAddress addr(
				IsIPv6SupportedByOS() ? "::1" : "127.0.0.1",
				13666
			);

		for(unsigned i = 0; i < 10; ++i){
			bytesSent = sendSock.Send(data, addr);
			ASSERT_ALWAYS(bytesSent == 4 || bytesSent == 0)
			if(bytesSent == 4){
				break;
			}
			
			ting::mt::Thread::Sleep(100);
		}
		ASSERT_ALWAYS(bytesSent == 4)
	}catch(ting::net::Exc &e){
		ASSERT_INFO_ALWAYS(false, e.What())
	}

	try{
		std::array<std::uint8_t, 1024> buf;
		
		unsigned bytesReceived = 0;
		for(unsigned i = 0; i < 10; ++i){
			ting::net::IPAddress ip;
			bytesReceived = recvSock.Recv(buf, ip);
			ASSERT_ALWAYS(bytesReceived == 0 || bytesReceived == 4)//all or nothing
			if(bytesReceived == 4){
				if(IsIPv6SupportedByOS()){
					ASSERT_INFO_ALWAYS(ip.host.Quad3() == 1, "ip.host.Quad3() = " << std::hex << ip.host.Quad3() << std::dec)
				}else{
					ASSERT_INFO_ALWAYS(ip.host.IPv4Host() == 0x7f000001, "ip.host.IPv4Host() = " << std::hex << ip.host.IPv4Host() << std::dec)
				}
				break;
			}
			
			ting::mt::Thread::Sleep(100);
		}
		ASSERT_ALWAYS(bytesReceived == 4)
		ASSERT_ALWAYS(buf[0] == '0')
		ASSERT_ALWAYS(buf[1] == '1')
		ASSERT_ALWAYS(buf[2] == '2')
		ASSERT_ALWAYS(buf[3] == '4')
	}catch(ting::net::Exc& e){
		ASSERT_INFO_ALWAYS(false, e.What())
	}
}

}//~namespace



namespace TestUDPSocketWaitForWriting{

void Run(){

	try{
		ting::net::UDPSocket sendSock;

		try{
			sendSock.Open();

			ting::WaitSet ws(1);

			ws.Add(sendSock, ting::Waitable::READ_AND_WRITE);

			if(ws.WaitWithTimeout(3000) == 0){
				//if timeout was hit
//NOTE: for some reason waiting for writing to UDP socket does not work on Win32 (aaarrrggghh).
#if M_OS == M_OS_WINDOWS
#	if M_COMPILER == M_COMPILER_MSVC
#	else
#		warning "Waiting for writing to UDP socket does not work on Win32"
#	endif
#endif
			}else{
				ASSERT_ALWAYS(sendSock.CanWrite())
				ASSERT_ALWAYS(!sendSock.CanRead())
			}

			ws.Remove(sendSock);
		}catch(ting::net::Exc &e){
			ASSERT_INFO_ALWAYS(false, e.What())
		}
	}catch(std::exception& e){
		ASSERT_INFO_ALWAYS(false, e.what())
	}

}

}//~namespace



namespace TestIPAddress{

void Run(){
	try{//test IP-address without port string parsing
		try{//test correct string
			ting::net::IPAddress ip("127.0.0.1", 80);
			ASSERT_ALWAYS(ip.host.IPv4Host() == 0x7f000001)
			ASSERT_ALWAYS(ip.port == 80)
		}catch(ting::net::IPAddress::BadIPAddressFormatExc& e){
			ASSERT_ALWAYS(false)
		}
		
		try{//test correct string
			ting::net::IPAddress ip("127.0.0.1:23ddqwd", 80);
			ASSERT_ALWAYS(ip.host.IPv4Host() == 0x7f000001)
			ASSERT_ALWAYS(ip.port == 80)
		}catch(ting::net::IPAddress::BadIPAddressFormatExc& e){
			ASSERT_ALWAYS(true)
		}
		try{//test correct string
			ting::net::IPAddress ip("127.0.0.2555:23ddqwd", 80);
			ASSERT_ALWAYS(ip.host.IPv4Host() == 0x7f0000ff)
			ASSERT_ALWAYS(ip.port == 80)
		}catch(ting::net::IPAddress::BadIPAddressFormatExc& e){
			ASSERT_ALWAYS(true)
		}
		
		try{//test incorrect string
			ting::net::IPAddress ip("127.0.1803:65536");
			ASSERT_ALWAYS(false)
		}catch(ting::net::IPAddress::BadIPAddressFormatExc& e){
			//should get here
		}catch(...){
			ASSERT_ALWAYS(false)
		}
		
		try{//test incorrect string
			ting::net::IPAddress ip("127.0.270.1:65536");
			ASSERT_ALWAYS(false)
		}catch(ting::net::IPAddress::BadIPAddressFormatExc& e){
			//should get here
		}catch(...){
			ASSERT_ALWAYS(false)
		}
	}catch(...){
		ASSERT_ALWAYS(false)
	}
	
	try{//test IP-address with port string parsing
		try{//test correct string
			ting::net::IPAddress ip("127.0.0.1:80");
			ASSERT_ALWAYS(ip.host.IPv4Host() == 0x7f000001)
			ASSERT_ALWAYS(ip.port == 80)
		}catch(ting::net::IPAddress::BadIPAddressFormatExc& e){
			ASSERT_ALWAYS(false)
		}
		
		try{//test incorrect string
			ting::net::IPAddress ip("127.0.0.1803:43");
			ASSERT_ALWAYS(false)
		}catch(ting::net::IPAddress::BadIPAddressFormatExc& e){
			//should get here
		}catch(...){
			ASSERT_ALWAYS(false)
		}
		
		try{//test incorrect string
			ting::net::IPAddress ip("127.0.0.180p43");
			ASSERT_ALWAYS(false)
		}catch(ting::net::IPAddress::BadIPAddressFormatExc& e){
			//should get here
		}catch(...){
			ASSERT_ALWAYS(false)
		}
		
		try{//test incorrect string
			ting::net::IPAddress ip("127.0.0.180:123456");
			ASSERT_ALWAYS(false)
		}catch(ting::net::IPAddress::BadIPAddressFormatExc& e){
			//should get here
		}catch(...){
			ASSERT_ALWAYS(false)
		}
		
		try{//test incorrect string
			ting::net::IPAddress ip("127.0.0.180:72345");
			ASSERT_ALWAYS(false)
		}catch(ting::net::IPAddress::BadIPAddressFormatExc& e){
			//should get here
		}catch(...){
			ASSERT_ALWAYS(false)
		}
		
		try{//test incorrect string
			ting::net::IPAddress ip("127.0.0.1803:65536");
			ASSERT_ALWAYS(false)
		}catch(ting::net::IPAddress::BadIPAddressFormatExc& e){
			//should get here
		}catch(...){
			ASSERT_ALWAYS(false)
		}
		
		try{//test correct string
			ting::net::IPAddress ip("127.0.0.1:65535");
			ASSERT_ALWAYS(ip.host.IPv4Host() == 0x7f000001)
			ASSERT_ALWAYS(ip.port == 0xffff)
		}catch(ting::net::IPAddress::BadIPAddressFormatExc& e){
			ASSERT_ALWAYS(false)
		}
		
		try{//test correct string
			ting::net::IPAddress ip("127.0.0.1:0");
			ASSERT_ALWAYS(ip.host.IPv4Host() == 0x7f000001)
			ASSERT_ALWAYS(ip.port == 0)
		}catch(ting::net::IPAddress::BadIPAddressFormatExc& e){
			ASSERT_ALWAYS(false)
		}
		
		try{//test correct string
			ting::net::IPAddress ip("127.0.0.1:6535 ");
			ASSERT_ALWAYS(ip.host.IPv4Host() == 0x7f000001)
			ASSERT_ALWAYS(ip.port == 6535)
		}catch(ting::net::IPAddress::BadIPAddressFormatExc& e){
			ASSERT_ALWAYS(false)
		}
		
		try{//test correct string
			ting::net::IPAddress ip("127.0.0.1:6535dwqd 345");
			ASSERT_ALWAYS(ip.host.IPv4Host() == 0x7f000001)
			ASSERT_ALWAYS(ip.port == 6535)
		}catch(ting::net::IPAddress::BadIPAddressFormatExc& e){
			ASSERT_ALWAYS(false)
		}
	}catch(...){
		ASSERT_ALWAYS(false)
	}
	
	//Test IPv6
	if(IsIPv6SupportedByOS()){
		try{
			ting::net::IPAddress ip("1002:3004:5006::7008:900a");
			ASSERT_ALWAYS(ip.port == 0)
			ASSERT_ALWAYS(ip.host.Quad0() == 0x10023004)
			ASSERT_ALWAYS(ip.host.Quad1() == 0x50060000)
			ASSERT_ALWAYS(ip.host.Quad2() == 0x00000000)
			ASSERT_ALWAYS(ip.host.Quad3() == 0x7008900a)
		}catch(ting::net::IPAddress::BadIPAddressFormatExc& e){
			ASSERT_ALWAYS(false)
		}catch(ting::Exc& e){
			ASSERT_INFO_ALWAYS(false, "exception caught: " << e.What())
		}catch(...){
			ASSERT_ALWAYS(false)
		}

		try{
			ting::net::IPAddress ip("[1002:3004:5006::7008:900a]:134");
			ASSERT_ALWAYS(ip.port == 134)
			ASSERT_ALWAYS(ip.host.Quad0() == 0x10023004)
			ASSERT_ALWAYS(ip.host.Quad1() == 0x50060000)
			ASSERT_ALWAYS(ip.host.Quad2() == 0x00000000)
			ASSERT_ALWAYS(ip.host.Quad3() == 0x7008900a)
		}catch(...){
			ASSERT_ALWAYS(false)
		}

		try{
			ting::net::IPAddress ip("[::ffff:127.0.0.1]:45");
			ASSERT_ALWAYS(ip.port == 45)
			ASSERT_ALWAYS(ip.host.IsIPv4())
			ASSERT_ALWAYS(ip.host.IPv4Host() == 0x7f000001)
		}catch(...){
			ASSERT_ALWAYS(false)
		}
	}
}

}//~namespace
