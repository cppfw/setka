#include "../../src/setka/tcp_socket.hpp"
#include "../../src/setka/tcp_server_socket.hpp"
#include "../../src/setka/udp_socket.hpp"

#include <opros/wait_set.hpp>
#include <nitki/thread.hpp>
#include <nitki/queue.hpp>

#include <utki/config.hpp>
#include <utki/time.hpp>

#include "socket.hpp"

namespace{
bool IsIPv6SupportedByOS(){
#if M_OS == M_OS_WINDOWS
	{
		OSVERSIONINFOEX osvi;
		memset(&osvi, 0, sizeof(osvi));
		osvi.dwOSVersionInfoSize = sizeof(osvi);

		osvi.dwMajorVersion = 5; // version 5 is WinXP
		osvi.dwMinorVersion = 0;
		osvi.wServicePackMajor = 0;
		osvi.wServicePackMinor = 0;

		DWORD mask = VER_MAJORVERSION | VER_MINORVERSION | VER_SERVICEPACKMAJOR | VER_SERVICEPACKMINOR;

		if(VerifyVersionInfo(
				&osvi,
				mask,
				VerSetConditionMask(0, mask, VER_GREATER) // we check if current Windows version is greater than WinXP
			) == 0)
		{
			// Windows version is WinXP or before

			return false;
		}else{
			return true;
		}
	}
#else
	return true;
#endif
}
}

namespace BasicClientServerTest{
void SendAll(setka::tcp_socket& s, utki::span<uint8_t> buf){
	if(!s){
		throw std::logic_error("tcp_socket::Send(): socket is not opened");
	}

	size_t left = buf.size();
	ASSERT_ALWAYS(left >= 0)

	size_t offset = 0;

	while(true){
		size_t res = s.send(decltype(buf)(&*buf.begin() + offset, buf.size() - offset));
		left -= res;
		ASSERT_ALWAYS(left >= 0)
		offset += res;
		if(offset == buf.size()){
			break;
		}
		// give 30ms to allow data from send buffer to be sent
		std::this_thread::sleep_for(std::chrono::milliseconds(30));
	}

	ASSERT_ALWAYS(left == 0)
}

class ServerThread : public nitki::thread{
public:
	volatile bool quitFlag = false;
	nitki::queue queue;

	void run()override{
		try{
			setka::tcp_server_socket listenSock;

			listenSock.open(13666); // start listening

			ASSERT_ALWAYS(listenSock.get_local_port() == 13666)

			// accept some connection
			setka::tcp_socket sock;
			while(!sock && !this->quitFlag){
				sock = listenSock.accept();
				std::this_thread::sleep_for(std::chrono::milliseconds(100));
				if(auto m = this->queue.pop_front()){
					m();
				}
			}

			ASSERT_ALWAYS(sock)

			ASSERT_ALWAYS(sock.get_local_address().host.get_v4() == 0x7f000001)
			ASSERT_ALWAYS(sock.get_remote_address().host.get_v4() == 0x7f000001)

			std::array<uint8_t, 4> data;
			data[0] = '0';
			data[1] = '1';
			data[2] = '2';
			data[3] = '4';
			SendAll(sock, utki::make_span(data));
		}catch(std::exception &e){
			ASSERT_INFO_ALWAYS(false, "Network error: " << e.what())
		}
	}
};



void Run(){
	ServerThread serverThread;
	
	serverThread.start();
	
	std::this_thread::sleep_for(std::chrono::milliseconds(1000));
	
	try{
		setka::ip_address ip("127.0.0.1", 13666);

		setka::tcp_socket sock;

		sock.open(ip);

		ASSERT_ALWAYS(sock)

		// give some time for socket to connect
		std::this_thread::sleep_for(std::chrono::milliseconds(1000));
		
		ASSERT_ALWAYS(sock.get_remote_address().host.get_v4() == 0x7f000001)

		std::array<uint8_t, 4> data;
		size_t bytesReceived = 0;
		for(unsigned i = 0; i < 30; ++i){
			ASSERT_ALWAYS(bytesReceived < 4)
			bytesReceived += sock.recieve(utki::span<uint8_t>(&*data.begin() + bytesReceived, data.size() - bytesReceived));
			ASSERT_ALWAYS(bytesReceived <= 4)
			if(bytesReceived == 4){
				break;
			}

			std::this_thread::sleep_for(std::chrono::milliseconds(100));
		}
		ASSERT_ALWAYS(bytesReceived == 4)
		
		ASSERT_ALWAYS(data[0] == '0')
		ASSERT_ALWAYS(data[1] == '1')
		ASSERT_ALWAYS(data[2] == '2')
		ASSERT_ALWAYS(data[3] == '4')
	}catch(std::exception &e){
		ASSERT_INFO_ALWAYS(false, "Network error: " << e.what())
	}
	
	serverThread.join();
}

}



namespace SendDataContinuouslyWithWaitSet{

void Run(){
	setka::tcp_server_socket serverSock;

	serverSock.open(13666);


	setka::tcp_socket sockS;
	{
		setka::ip_address ip("127.0.0.1", 13666);
		sockS.open(ip);
	}

	//Accept connection
//	TRACE(<< "SendDataContinuously::Run(): accepting connection" << std::endl)
	setka::tcp_socket sockR;
	for(unsigned i = 0; i < 20 && !sockR; ++i){
		std::this_thread::sleep_for(std::chrono::milliseconds(100));
		sockR = serverSock.accept();
	}

	ASSERT_ALWAYS(sockS)
	ASSERT_ALWAYS(sockR)

	//Here we have 2 sockets sockS and sockR

	{
		setka::ip_address addrS = sockS.get_remote_address();
		setka::ip_address addrR = sockR.get_remote_address();
//		TRACE(<< "SendDataContinuously::Run(): addrS = " << std::hex << addrS.host << ":" << addrS.port << std::dec << std::endl)
//		TRACE(<< "SendDataContinuously::Run(): addrR = " << std::hex << addrR.host << ":" << addrR.port << std::dec << std::endl)
		ASSERT_ALWAYS(addrS.host.get_v4() == 0x7f000001) //check that IP is 127.0.0.1
		ASSERT_ALWAYS(addrR.host.get_v4() == 0x7f000001) //check that IP is 127.0.0.1
	}

	opros::wait_set ws(2);
	ws.add(sockR, utki::make_flags({opros::ready::read}));
	ws.add(sockS, utki::make_flags({opros::ready::write}));


	uint32_t scnt = 0;
	std::vector<uint8_t> sendBuffer;
	size_t bytesSent = 0;

	uint32_t rcnt = 0;
	std::array<uint8_t, sizeof(uint32_t)> recvBuffer;
	unsigned recvBufBytes = 0;


	uint32_t startTime = utki::get_ticks_ms();
	
	while(utki::get_ticks_ms() - startTime < 5000){ // 5 seconds
		std::array<opros::waitable*, 2> triggered;

		unsigned numTriggered = ws.wait(1000, utki::make_span(triggered));

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
				ASSERT_ALWAYS(!sockS.flags().get(opros::ready::read))
				ASSERT_ALWAYS(!sockS.flags().get(opros::ready::error))
				ASSERT_ALWAYS(sockS.flags().get(opros::ready::write))

				ASSERT_ALWAYS(bytesSent <= sendBuffer.size())

				if(sendBuffer.size() == bytesSent){
					sendBuffer.resize(0xffff + 1);
					bytesSent = 0;
					
					ASSERT_INFO_ALWAYS((sendBuffer.size() % sizeof(uint32_t)) == 0,
							"sendBuffer.Size() = " << sendBuffer.size()
							<< " (sendBuffer.Size() % sizeof(uint32_t)) = "
							<< (sendBuffer.size() % sizeof(uint32_t))
						)

					uint8_t* p = &sendBuffer[0];
					for(; p != (&sendBuffer[0]) + sendBuffer.size(); p += sizeof(uint32_t)){
						ASSERT_INFO_ALWAYS(p < (((&sendBuffer[0]) + sendBuffer.size()) - (sizeof(uint32_t) - 1)), "p = " << p << " sendBuffer.End() = " << &*sendBuffer.end())
						utki::serialize32le(scnt, p);
						++scnt;
					}
					ASSERT_ALWAYS(p == (&sendBuffer[0]) + sendBuffer.size())
				}

				ASSERT_ALWAYS(sendBuffer.size() > 0)

				try{
					auto res = sockS.send(utki::span<uint8_t>(&*sendBuffer.begin() + bytesSent, sendBuffer.size() - bytesSent));
					bytesSent += res;
					if(res == 0){
						ASSERT_ALWAYS(res > 0) // since it was CanWrite() we should be able to write at least something
					}else{
//						TRACE(<< "SendDataContinuously::Run(): " << res << " bytes sent" << std::endl)
					}
					ASSERT_ALWAYS(!sockS.flags().get(opros::ready::write))
				}catch(std::exception& e){
					ASSERT_INFO_ALWAYS(false, "sockS.Send() failed: " << e.what())
				}
				ASSERT_ALWAYS(bytesSent <= sendBuffer.size())
			}else if(triggered[i] == &sockR){
				ASSERT_ALWAYS(triggered[i] != &sockS)

//				TRACE(<< "SendDataContinuously::Run(): sockR triggered" << std::endl)
				ASSERT_ALWAYS(sockR.flags().get(opros::ready::read))
				ASSERT_ALWAYS(!sockR.flags().get(opros::ready::error))
				ASSERT_ALWAYS(!sockR.flags().get(opros::ready::write))

				while(true){
					std::array<uint8_t, 0x2000> buf; // 8kb buffer
					size_t numBytesReceived;
					try{
						numBytesReceived = sockR.recieve(utki::make_span(buf));
					}catch(std::exception& e){
						ASSERT_INFO_ALWAYS(false, "sockR.Recv() failed: " << e.what())
					}
					ASSERT_ALWAYS(numBytesReceived <= buf.size())
//					TRACE(<< "SendDataContinuously::Run(): " << numBytesReceived << " bytes received" << std::endl)

					if(numBytesReceived == 0){
						break;//~while(true)
					}

					auto p = buf.cbegin();
					for(unsigned i = 0; i < numBytesReceived && p != buf.end(); ++p, ++i){
						recvBuffer[recvBufBytes] = *p;
						++recvBufBytes;

						ASSERT_ALWAYS(recvBufBytes <= recvBuffer.size())

						if(recvBufBytes == recvBuffer.size()){
							recvBufBytes = 0;
							uint32_t num = utki::deserialize32le(&*recvBuffer.begin());
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

	ws.remove(sockS);
	ws.remove(sockR);
}

}//~namespace



namespace SendDataContinuously{

void Run(){
	setka::tcp_server_socket serverSock;

	serverSock.open(13666);

	setka::tcp_socket sockS;
	{
		setka::ip_address ip("127.0.0.1", 13666);
		sockS.open(ip);
	}

	// accept connection
//	TRACE(<< "SendDataContinuously::Run(): accepting connection" << std::endl)
	setka::tcp_socket sockR;
	for(unsigned i = 0; i < 20 && !sockR; ++i){
		std::this_thread::sleep_for(std::chrono::milliseconds(100));
		sockR = serverSock.accept();
	}

	ASSERT_ALWAYS(sockS)
	ASSERT_ALWAYS(sockR)

	// here we have 2 sockets sockS and sockR

	uint8_t scnt = 0;

	uint8_t rcnt = 0;


	uint32_t startTime = utki::get_ticks_ms();

	while(utki::get_ticks_ms() - startTime < 5000){ // 5 seconds

		// send

		try{
			utki::span<uint8_t> buf(&scnt, 1);
			auto res = sockS.send(buf);
			ASSERT_ALWAYS(res <= 1)
			if(res == 1){
				++scnt;
			}else{
				ASSERT_ALWAYS(false)
			}
		}catch(std::exception& e){
			ASSERT_INFO_ALWAYS(false, "sockS.Send() failed: " << e.what())
		}


		// read

		while(true){
			std::array<uint8_t, 0x2000> buf; // 8kb buffer
			size_t numBytesReceived;
			try{
				numBytesReceived = sockR.recieve(utki::make_span(buf));
			}catch(std::exception& e){
				ASSERT_INFO_ALWAYS(false, "sockR.Recv() failed: " << e.what())
			}
			ASSERT_ALWAYS(numBytesReceived <= buf.size())
//			TRACE(<< "SendDataContinuously::Run(): " << numBytesReceived << " bytes received" << std::endl)

			if(numBytesReceived == 0){
				break;//~while(true)
			}

			auto p = buf.cbegin();
			for(unsigned i = 0; i < numBytesReceived && p != buf.end(); ++p, ++i){
				ASSERT_INFO_ALWAYS(rcnt == *p, "rcnt = " << unsigned(rcnt) << " *p = " << unsigned(*p) << " diff = " << unsigned(rcnt - *p))
				++rcnt;
			}//~for
		}//~while(true)
		
	}//~while
}

}//~namespace



namespace Basicip_addressTest{

void Run(){
	{
		try{
			setka::ip_address a("123.124.125.126", 5);
			ASSERT_ALWAYS(a.host.get_v4() == (123 << 24) + (124 << 16) + (125 << 8) + 126)
			ASSERT_ALWAYS(a.port == 5)
		}catch(std::exception& e){
			ASSERT_INFO_ALWAYS(false, e.what())
		}
	}

	{
		setka::ip_address a(123, 124, 125, 126, 5);
		ASSERT_ALWAYS(a.host.get_v4() == (123 << 24) + (124 << 16) + (125 << 8) + 126)
		ASSERT_ALWAYS(a.port == 5)
	}

	//test copy constructor and operator=()
	{
		setka::ip_address a(123, 124, 125, 126, 5);
		ASSERT_ALWAYS(a.host.get_v4() == (123 << 24) + (124 << 16) + (125 << 8) + 126)
		ASSERT_ALWAYS(a.port == 5)

		setka::ip_address a1(a);
		ASSERT_ALWAYS(a1.host.get_v4() == (123 << 24) + (124 << 16) + (125 << 8) + 126)
		ASSERT_ALWAYS(a1.port == 5)

		setka::ip_address a2;
		a2 = a1;
		ASSERT_ALWAYS(a2.host.get_v4() == (123 << 24) + (124 << 16) + (125 << 8) + 126)
		ASSERT_ALWAYS(a2.port == 5)

		ASSERT_ALWAYS(a == a1)
		ASSERT_ALWAYS(a == a2)
	}
}

}//~namespace



namespace BasicUDPSocketsTest{

void Run(){

	setka::udp_socket recvSock;

	try{
		recvSock.open(13666);
	}catch(std::exception &e){
		ASSERT_INFO_ALWAYS(false, e.what())
	}

	ASSERT_ALWAYS(recvSock.get_local_port() == 13666)

	setka::udp_socket sendSock;

	try{
		sendSock.open();

		std::array<uint8_t, 4> data;
		data[0] = '0';
		data[1] = '1';
		data[2] = '2';
		data[3] = '4';
		size_t bytesSent = 0;

		setka::ip_address addr(
				IsIPv6SupportedByOS() ? "::1" : "127.0.0.1",
				13666
			);

		for(unsigned i = 0; i < 10; ++i){
			bytesSent = sendSock.send(utki::make_span(data), addr);
			ASSERT_ALWAYS(bytesSent == 4 || bytesSent == 0)
			if(bytesSent == 4){
				break;
			}
			
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
		}
		ASSERT_ALWAYS(bytesSent == 4)
	}catch(std::exception &e){
		ASSERT_INFO_ALWAYS(false, e.what())
	}

	try{
		std::array<uint8_t, 1024> buf;
		
		size_t bytesReceived = 0;
		for(unsigned i = 0; i < 10; ++i){
			setka::ip_address ip;
			bytesReceived = recvSock.recieve(utki::make_span(buf), ip);
			ASSERT_ALWAYS(bytesReceived == 0 || bytesReceived == 4)//all or nothing
			if(bytesReceived == 4){
				if(IsIPv6SupportedByOS()){
					ASSERT_INFO_ALWAYS(ip.host.quad[3] == 1, "ip.host.Quad3() = " << std::hex << ip.host.quad[3] << std::dec)
				}else{
					ASSERT_INFO_ALWAYS(ip.host.get_v4() == 0x7f000001, "ip.host.IPv4Host() = " << std::hex << ip.host.get_v4() << std::dec)
				}
				break;
			}
			
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
		}
		ASSERT_ALWAYS(bytesReceived == 4)
		ASSERT_ALWAYS(buf[0] == '0')
		ASSERT_ALWAYS(buf[1] == '1')
		ASSERT_ALWAYS(buf[2] == '2')
		ASSERT_ALWAYS(buf[3] == '4')
	}catch(std::exception& e){
		ASSERT_INFO_ALWAYS(false, e.what())
	}
}
}

namespace TestUDPSocketWaitForWriting{
void Run(){
	try{
		setka::udp_socket sendSock;

		sendSock.open();

		opros::wait_set ws(1);

		ws.add(sendSock, utki::make_flags({opros::ready::read, opros::ready::write}));

		if(ws.wait(3000) == 0){
			// if timeout was hit
// NOTE: for some reason waiting for writing to UDP socket does not work on Win32 (aaarrrggghh).
#if M_OS == M_OS_WINDOWS
#	if M_COMPILER == M_COMPILER_MSVC
#	else
#		warning "Waiting for writing to UDP socket does not work on Win32"
#	endif
#endif
		}else{
			ASSERT_ALWAYS(sendSock.flags().get(opros::ready::write))
			ASSERT_ALWAYS(!sendSock.flags().get(opros::ready::read))
		}

		ws.remove(sendSock);
	}catch(std::exception& e){
		ASSERT_INFO_ALWAYS(false, e.what())
	}
}
}

namespace Testip_address{
void Run(){
	try{//test IP-address without port string parsing
		try{//test correct string
			setka::ip_address ip("127.0.0.1", 80);
			ASSERT_ALWAYS(ip.host.get_v4() == 0x7f000001)
			ASSERT_ALWAYS(ip.port == 80)
		}catch(std::exception&){
			ASSERT_ALWAYS(false)
		}
		
		try{//test correct string
			setka::ip_address ip("127.0.0.1:23ddqwd", 80);
			ASSERT_ALWAYS(ip.host.get_v4() == 0x7f000001)
			ASSERT_ALWAYS(ip.port == 80)
		}catch(std::exception&){
			ASSERT_ALWAYS(true)
		}
		try{//test correct string
			setka::ip_address ip("127.0.0.2555:23ddqwd", 80);
			ASSERT_ALWAYS(ip.host.get_v4() == 0x7f0000ff)
			ASSERT_ALWAYS(ip.port == 80)
		}catch(std::exception&){
			ASSERT_ALWAYS(true)
		}
		
		try{//test incorrect string
			setka::ip_address ip("127.0.1803:65536");
			ASSERT_ALWAYS(false)
		}catch(std::exception&){
			//should get here
		}catch(...){
			ASSERT_ALWAYS(false)
		}
		
		try{//test incorrect string
			setka::ip_address ip("127.0.270.1:65536");
			ASSERT_ALWAYS(false)
		}catch(std::exception&){
			//should get here
		}catch(...){
			ASSERT_ALWAYS(false)
		}
	}catch(...){
		ASSERT_ALWAYS(false)
	}
	
	try{//test IP-address with port string parsing
		try{//test correct string
			setka::ip_address ip("127.0.0.1:80");
			ASSERT_ALWAYS(ip.host.get_v4() == 0x7f000001)
			ASSERT_ALWAYS(ip.port == 80)
		}catch(std::exception&){
			ASSERT_ALWAYS(false)
		}
		
		try{//test incorrect string
			setka::ip_address ip("127.0.0.1803:43");
			ASSERT_ALWAYS(false)
		}catch(std::exception&){
			//should get here
		}catch(...){
			ASSERT_ALWAYS(false)
		}
		
		try{//test incorrect string
			setka::ip_address ip("127.0.0.180p43");
			ASSERT_ALWAYS(false)
		}catch(std::exception&){
			//should get here
		}catch(...){
			ASSERT_ALWAYS(false)
		}
		
		try{//test incorrect string
			setka::ip_address ip("127.0.0.180:123456");
			ASSERT_ALWAYS(false)
		}catch(std::exception&){
			//should get here
		}catch(...){
			ASSERT_ALWAYS(false)
		}
		
		try{//test incorrect string
			setka::ip_address ip("127.0.0.180:72345");
			ASSERT_ALWAYS(false)
		}catch(std::exception&){
			//should get here
		}catch(...){
			ASSERT_ALWAYS(false)
		}
		
		try{//test incorrect string
			setka::ip_address ip("127.0.0.1803:65536");
			ASSERT_ALWAYS(false)
		}catch(std::exception&){
			//should get here
		}catch(...){
			ASSERT_ALWAYS(false)
		}
		
		try{//test correct string
			setka::ip_address ip("127.0.0.1:65535");
			ASSERT_ALWAYS(ip.host.get_v4() == 0x7f000001)
			ASSERT_ALWAYS(ip.port == 0xffff)
		}catch(std::exception&){
			ASSERT_ALWAYS(false)
		}
		
		try{//test correct string
			setka::ip_address ip("127.0.0.1:0");
			ASSERT_ALWAYS(ip.host.get_v4() == 0x7f000001)
			ASSERT_ALWAYS(ip.port == 0)
		}catch(std::exception&){
			ASSERT_ALWAYS(false)
		}
		
		try{//test correct string
			setka::ip_address ip("127.0.0.1:6535 ");
			ASSERT_ALWAYS(ip.host.get_v4() == 0x7f000001)
			ASSERT_ALWAYS(ip.port == 6535)
		}catch(std::exception&){
			ASSERT_ALWAYS(false)
		}
		
		try{//test correct string
			setka::ip_address ip("127.0.0.1:6535dwqd 345");
			ASSERT_ALWAYS(ip.host.get_v4() == 0x7f000001)
			ASSERT_ALWAYS(ip.port == 6535)
		}catch(std::exception&){
			ASSERT_ALWAYS(false)
		}
	}catch(...){
		ASSERT_ALWAYS(false)
	}
	
	// Test IPv6
	if(IsIPv6SupportedByOS()){
		try{
			setka::ip_address ip("1002:3004:5006::7008:900a");
			ASSERT_ALWAYS(ip.port == 0)
			ASSERT_ALWAYS(ip.host.quad[0] == 0x10023004)
			ASSERT_ALWAYS(ip.host.quad[1] == 0x50060000)
			ASSERT_ALWAYS(ip.host.quad[2] == 0x00000000)
			ASSERT_ALWAYS(ip.host.quad[3] == 0x7008900a)
		}catch(std::exception& e){
			ASSERT_INFO_ALWAYS(false, "exception caught: " << e.what())
		}catch(...){
			ASSERT_ALWAYS(false)
		}

		try{
			setka::ip_address ip("[1002:3004:5006::7008:900a]:134");
			ASSERT_ALWAYS(ip.port == 134)
			ASSERT_ALWAYS(ip.host.quad[0] == 0x10023004)
			ASSERT_ALWAYS(ip.host.quad[1] == 0x50060000)
			ASSERT_ALWAYS(ip.host.quad[2] == 0x00000000)
			ASSERT_ALWAYS(ip.host.quad[3] == 0x7008900a)
		}catch(...){
			ASSERT_ALWAYS(false)
		}

		try{
			setka::ip_address ip("[::ffff:127.0.0.1]:45");
			ASSERT_ALWAYS(ip.port == 45)
			ASSERT_ALWAYS(ip.host.is_v4())
			ASSERT_ALWAYS(ip.host.get_v4() == 0x7f000001)
		}catch(...){
			ASSERT_ALWAYS(false)
		}
	}
}
}
