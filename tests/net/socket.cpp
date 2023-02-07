#include "../../src/setka/tcp_socket.hpp"
#include "../../src/setka/tcp_server_socket.hpp"
#include "../../src/setka/udp_socket.hpp"

#include <opros/wait_set.hpp>
#include <nitki/thread.hpp>
#include <nitki/queue.hpp>

#include <utki/config.hpp>
#include <utki/time.hpp>
#include <utki/debug.hpp>

#include "socket.hpp"

#ifdef assert
#	undef assert
#endif

namespace{
bool is_ipv6_supported_by_os(){
#if CFG_OS == CFG_OS_WINDOWS
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

namespace basic_client_server_test{
void SendAll(setka::tcp_socket& s, utki::span<uint8_t> buf){
	if(s.is_empty()){
		throw std::logic_error("SendAll(): socket is not opened");
	}

	size_t left = buf.size();
	utki::assert(left >= 0, SL);

	size_t offset = 0;

	while(true){
		size_t res = s.send(decltype(buf)(&*buf.begin() + offset, buf.size() - offset));
		left -= res;
		utki::assert(left >= 0, SL);
		offset += res;
		if(offset == buf.size()){
			break;
		}
		// give 30ms to allow data from send buffer to be sent
		std::this_thread::sleep_for(std::chrono::milliseconds(30));
	}

	utki::assert(left == 0, SL);
}

class ServerThread : public nitki::thread{
public:
	volatile bool quitFlag = false;
	nitki::queue queue;

	void run()override{
		try{
			setka::tcp_server_socket listenSock(13666); // start listening

			utki::assert(listenSock.get_local_port() == 13666, SL);

			// accept some connection
			setka::tcp_socket sock;
			while(sock.is_empty() && !this->quitFlag){
				sock = listenSock.accept();
				std::this_thread::sleep_for(std::chrono::milliseconds(100));
				if(auto m = this->queue.pop_front()){
					m();
				}
			}

			utki::assert(!sock.is_empty(), SL);

			utki::assert(sock.get_local_address().host.get_v4() == 0x7f000001, SL);
			utki::assert(sock.get_remote_address().host.get_v4() == 0x7f000001, SL);

			std::array<uint8_t, 4> data;
			data[0] = '0';
			data[1] = '1';
			data[2] = '2';
			data[3] = '4';
			SendAll(sock, utki::make_span(data));
		}catch(std::exception &e){
			utki::assert(false, [&](auto&o){o << "Network error: " << e.what();}, SL);
		}
	}
};



void run(){
	ServerThread serverThread;
	
	serverThread.start();
	
	std::this_thread::sleep_for(std::chrono::milliseconds(1000));
	
	try{
		setka::address ip("127.0.0.1", 13666);

		setka::tcp_socket sock(ip);

		utki::assert(!sock.is_empty(), SL);

		// give some time for socket to connect
		std::this_thread::sleep_for(std::chrono::milliseconds(1000));
		
		utki::assert(sock.get_remote_address().host.get_v4() == 0x7f000001, SL);

		std::array<uint8_t, 4> data;
		size_t num_bytes_received = 0;
		for(unsigned i = 0; i < 30; ++i){
			utki::assert(num_bytes_received < 4, SL);
			num_bytes_received += sock.receive(utki::span<uint8_t>(&*data.begin() + num_bytes_received, data.size() - num_bytes_received));
			utki::assert(num_bytes_received <= 4, SL);
			if(num_bytes_received == 4){
				break;
			}

			std::this_thread::sleep_for(std::chrono::milliseconds(100));
		}
		utki::assert(num_bytes_received == 4, SL);
		
		utki::assert(data[0] == '0', SL);
		utki::assert(data[1] == '1', SL);
		utki::assert(data[2] == '2', SL);
		utki::assert(data[3] == '4', SL);
	}catch(std::exception &e){
		utki::assert(
			false,
			[&](auto&o){o << "Network error: " << e.what(); },
			SL
		);
	}
	
	serverThread.join();
}

}



namespace send_data_continuously_with_wait_set{

void run(){
	setka::tcp_server_socket serverSock(13666);

	setka::tcp_socket sockS(setka::address("127.0.0.1", 13666));

	//Accept connection
//	TRACE(<< "send_data_continuously::run(): accepting connection" << std::endl)
	setka::tcp_socket sockR;
	for(unsigned i = 0; i < 20 && sockR.is_empty(); ++i){
		std::this_thread::sleep_for(std::chrono::milliseconds(100));
		sockR = serverSock.accept();
	}

	utki::assert(!sockS.is_empty(), SL);
	utki::assert(!sockR.is_empty(), SL);

	//Here we have 2 sockets sockS and sockR

	{
		setka::address addrS = sockS.get_remote_address();
		setka::address addrR = sockR.get_remote_address();
//		TRACE(<< "send_data_continuously::run(): addrS = " << std::hex << addrS.host << ":" << addrS.port << std::dec << std::endl)
//		TRACE(<< "send_data_continuously::run(): addrR = " << std::hex << addrR.host << ":" << addrR.port << std::dec << std::endl)
		utki::assert(addrS.host.get_v4() == 0x7f000001, SL); //check that IP is 127.0.0.1
		utki::assert(addrR.host.get_v4() == 0x7f000001, SL); //check that IP is 127.0.0.1
	}

	opros::wait_set ws(2);
	ws.add(sockR, utki::make_flags({opros::ready::read}));
	ws.add(sockS, utki::make_flags({opros::ready::write}));


	uint32_t scnt = 0;
	std::vector<uint8_t> sendBuffer;
	size_t num_bytes_send = 0;

	uint32_t rcnt = 0;
	std::array<uint8_t, sizeof(uint32_t)> recvBuffer;
	unsigned recvBufBytes = 0;


	uint32_t startTime = utki::get_ticks_ms();
	
	while(utki::get_ticks_ms() - startTime < 5000){ // 5 seconds
		std::array<opros::event_info, 2> triggered;

		unsigned numTriggered = ws.wait(1000, utki::make_span(triggered));

		utki::assert(numTriggered <= 2, SL);

		if(numTriggered == 0){
//			TRACE(<< "send_data_continuously::run(): 0 triggered" << std::endl)
			continue;
		}

		//If 2 waitables have triggered they should be 2 different waitables.
		if(numTriggered == 2){
//			TRACE(<< "send_data_continuously::run(): 2 triggered" << std::endl)
			utki::assert(triggered[0].w != triggered[1].w, SL);
		}else{
			utki::assert(numTriggered == 1, SL);
//			TRACE(<< "send_data_continuously::run(): 1 triggered" << std::endl)
		}

		for(unsigned i = 0; i < numTriggered; ++i){
			if(triggered[i].w == &sockS){
				utki::assert(triggered[i].w != &sockR, SL);

//				TRACE(<< "send_data_continuously::run(): sockS triggered" << std::endl)
				utki::assert(!triggered[i].flags.get(opros::ready::read), SL);
				utki::assert(!triggered[i].flags.get(opros::ready::error), SL);
				utki::assert(triggered[i].flags.get(opros::ready::write), SL);

				utki::assert(num_bytes_send <= sendBuffer.size(), SL);

				if(sendBuffer.size() == num_bytes_send){
					sendBuffer.resize(0xffff + 1);
					num_bytes_send = 0;
					
					utki::assert(
						(sendBuffer.size() % sizeof(uint32_t)) == 0,
						[&](auto&o){
							o << "sendBuffer.Size() = " << sendBuffer.size()
							<< " (sendBuffer.Size() % sizeof(uint32_t)) = "
							<< (sendBuffer.size() % sizeof(uint32_t));
						},
						SL
					);

					uint8_t* p = &sendBuffer[0];
					for(; p != (&sendBuffer[0]) + sendBuffer.size(); p += sizeof(uint32_t)){
						utki::assert(
							p < (((&sendBuffer[0]) + sendBuffer.size()) - (sizeof(uint32_t) - 1)),
							[&](auto&o){o << "p = " << p << " sendBuffer.End() = " << &*sendBuffer.end();},
							SL
						);
						utki::serialize32le(scnt, p);
						++scnt;
					}
					utki::assert(p == (&sendBuffer[0]) + sendBuffer.size(), SL);
				}

				utki::assert(sendBuffer.size() > 0, SL);

				try{
					auto res = sockS.send(utki::span<uint8_t>(&*sendBuffer.begin() + num_bytes_send, sendBuffer.size() - num_bytes_send));
					num_bytes_send += res;
					if(res == 0){
						utki::assert(res > 0, SL); // since it was CanWrite() we should be able to write at least something
					}else{
//						TRACE(<< "send_data_continuously::run(): " << res << " bytes sent" << std::endl)
					}
				}catch(std::exception& e){
					utki::assert(
						false,
						[&](auto&o){o << "sockS.Send() failed: " << e.what();},
						SL
					);
				}
				utki::assert(num_bytes_send <= sendBuffer.size(), SL);
			}else if(triggered[i].w == &sockR){
				utki::assert(triggered[i].w != &sockS, SL);

//				TRACE(<< "send_data_continuously::run(): sockR triggered" << std::endl)
				utki::assert(triggered[i].flags.get(opros::ready::read), SL);
				utki::assert(!triggered[i].flags.get(opros::ready::error), SL);
				utki::assert(!triggered[i].flags.get(opros::ready::write), SL);

				while(true){
					std::array<uint8_t, 0x2000> buf; // 8kb buffer
					size_t numBytesReceived;
					try{
						numBytesReceived = sockR.receive(utki::make_span(buf));
					}catch(std::exception& e){
						utki::assert(
							false,
							[&](auto&o){o << "sockR.Recv() failed: " << e.what();},
							SL
						);
					}
					utki::assert(numBytesReceived <= buf.size(), SL);
//					TRACE(<< "send_data_continuously::run(): " << numBytesReceived << " bytes received" << std::endl)

					if(numBytesReceived == 0){
						break;//~while(true)
					}

					auto p = buf.cbegin();
					for(unsigned i = 0; i < numBytesReceived && p != buf.end(); ++p, ++i){
						recvBuffer[recvBufBytes] = *p;
						++recvBufBytes;

						utki::assert(recvBufBytes <= recvBuffer.size(), SL);

						if(recvBufBytes == recvBuffer.size()){
							recvBufBytes = 0;
							uint32_t num = utki::deserialize32le(&*recvBuffer.begin());
							utki::assert(
									rcnt == num,
									[&](auto&o){o << "num = " << num << " rcnt = " << rcnt
											<< " rcnt - num = " << (rcnt - num)
											<< " recvBuffer = "
											<< unsigned(recvBuffer[0]) << ", "
											<< unsigned(recvBuffer[1]) << ", "
											<< unsigned(recvBuffer[2]) << ", "
											<< unsigned(recvBuffer[3]);
									},
									SL
								);
							++rcnt;
						}
					}//~for
				}//~while(true)
			}else{
				utki::assert(false, SL);
			}
		}//~for(triggered)
	}//~while

	ws.remove(sockS);
	ws.remove(sockR);
}

}//~namespace



namespace send_data_continuously{

void run(){
	setka::tcp_server_socket serverSock(13666);

	setka::tcp_socket sockS(setka::address("127.0.0.1", 13666));

	// accept connection
//	TRACE(<< "send_data_continuously::run(): accepting connection" << std::endl)
	setka::tcp_socket sockR;
	for(unsigned i = 0; i < 20 && sockR.is_empty(); ++i){
		std::this_thread::sleep_for(std::chrono::milliseconds(100));
		sockR = serverSock.accept();
	}

	utki::assert(!sockS.is_empty(), SL);
	utki::assert(!sockR.is_empty(), SL);

	// here we have 2 sockets sockS and sockR

	uint8_t scnt = 0;

	uint8_t rcnt = 0;

	uint32_t startTime = utki::get_ticks_ms();

	while(utki::get_ticks_ms() - startTime < 5000){ // 5 seconds

		// send

		try{
			utki::span<uint8_t> buf(&scnt, 1);
			auto res = sockS.send(buf);
			utki::assert(res <= 1, SL);
			if(res == 1){
				++scnt;
			}else{
				utki::assert(false, SL);
			}
		}catch(std::exception& e){
			utki::assert(
				false,
				[&](auto&o){o << "sockS.Send() failed: " << e.what();},
				SL
			);
		}


		// read

		while(true){
			std::array<uint8_t, 0x2000> buf; // 8kb buffer
			size_t numBytesReceived;
			try{
				numBytesReceived = sockR.receive(utki::make_span(buf));
			}catch(std::exception& e){
				utki::assert(
					false,
					[&](auto&o){o << "sockR.Recv() failed: " << e.what();},
					SL
				);
			}
			utki::assert(numBytesReceived <= buf.size(), SL);
//			TRACE(<< "send_data_continuously::run(): " << numBytesReceived << " bytes received" << std::endl)

			if(numBytesReceived == 0){
				break;//~while(true)
			}

			auto p = buf.cbegin();
			for(unsigned i = 0; i < numBytesReceived && p != buf.end(); ++p, ++i){
				utki::assert(
					rcnt == *p,
					[&](auto&o){o << "rcnt = " << unsigned(rcnt) << " *p = " << unsigned(*p) << " diff = " << unsigned(rcnt - *p);},
					SL
				);
				++rcnt;
			}//~for
		}//~while(true)
		
	}//~while
}

}//~namespace



namespace basic_ip_address_test{

void run(){
	{
		try{
			setka::address a("123.124.125.126", 5);
			utki::assert(a.host.get_v4() == (123 << 24) + (124 << 16) + (125 << 8) + 126, SL);
			utki::assert(a.port == 5, SL);
		}catch(std::exception& e){
			utki::assert(false, [&](auto&o){o << e.what();}, SL);
		}
	}

	{
		setka::address a(123, 124, 125, 126, 5);
		utki::assert(a.host.get_v4() == (123 << 24) + (124 << 16) + (125 << 8) + 126, SL);
		utki::assert(a.port == 5, SL);
	}

	//test copy constructor and operator=()
	{
		setka::address a(123, 124, 125, 126, 5);
		utki::assert(a.host.get_v4() == (123 << 24) + (124 << 16) + (125 << 8) + 126, SL);
		utki::assert(a.port == 5, SL);

		setka::address a1(a);
		utki::assert(a1.host.get_v4() == (123 << 24) + (124 << 16) + (125 << 8) + 126, SL);
		utki::assert(a1.port == 5, SL);

		setka::address a2;
		a2 = a1;
		utki::assert(a2.host.get_v4() == (123 << 24) + (124 << 16) + (125 << 8) + 126, SL);
		utki::assert(a2.port == 5, SL);

		utki::assert(a == a1, SL);
		utki::assert(a == a2, SL);
	}
}

}//~namespace



namespace basic_udp_sockets_test{

void run(){

	setka::udp_socket recvSock;

	try{
		recvSock = setka::udp_socket(13666);
	}catch(std::exception &e){
		utki::assert(false, [&](auto&o){o << e.what();}, SL);
	}

	utki::assert(recvSock.get_local_port() == 13666, SL);

	setka::udp_socket send_sock;

	try{
		send_sock = setka::udp_socket(0);

		std::array<uint8_t, 4> data;
		data[0] = '0';
		data[1] = '1';
		data[2] = '2';
		data[3] = '4';
		size_t num_bytes_send = 0;

		setka::address addr(
				is_ipv6_supported_by_os() ? "::1" : "127.0.0.1",
				13666
			);

		for(unsigned i = 0; i < 10; ++i){
			num_bytes_send = send_sock.send(utki::make_span(data), addr);
			utki::assert(num_bytes_send == 4 || num_bytes_send == 0, SL);
			if(num_bytes_send == 4){
				break;
			}
			
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
		}
		utki::assert(num_bytes_send == 4, SL);
	}catch(std::exception &e){
		utki::assert(false, [&](auto&o){o << e.what();}, SL);
	}

	try{
		std::array<uint8_t, 1024> buf;
		
		size_t num_bytes_received = 0;
		for(unsigned i = 0; i < 10; ++i){
			setka::address ip;
			num_bytes_received = recvSock.recieve(utki::make_span(buf), ip);
			utki::assert(num_bytes_received == 0 || num_bytes_received == 4, SL); // all or nothing
			if(num_bytes_received == 4){
				if(is_ipv6_supported_by_os()){
					utki::assert(
						ip.host.quad[3] == 1,
						[&](auto&o){o << "ip.host.Quad3() = " << std::hex << ip.host.quad[3] << std::dec;},
						SL
					);
				}else{
					utki::assert(
						ip.host.get_v4() == 0x7f000001,
						[&](auto&o){o << "ip.host.IPv4Host() = " << std::hex << ip.host.get_v4() << std::dec;},
						SL
					);
				}
				break;
			}
			
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
		}
		utki::assert(num_bytes_received == 4, SL);
		utki::assert(buf[0] == '0', SL);
		utki::assert(buf[1] == '1', SL);
		utki::assert(buf[2] == '2', SL);
		utki::assert(buf[3] == '4', SL);
	}catch(std::exception& e){
		utki::assert(
			false,
			[&](auto&o){o << e.what();},
			SL
		);
	}
}
}

namespace test_udp_socket_wait_for_writing{
void run(){
	try{
		setka::udp_socket send_sock(0);

		opros::wait_set ws(1);

		ws.add(send_sock, utki::make_flags({opros::ready::read, opros::ready::write}));

		std::array<opros::event_info, 1> triggered;

		if(ws.wait(3000, triggered) == 0){
			// if timeout was hit
			// NOTE: for some reason waiting for writing to UDP socket does not work on Win32 (aaarrrggghh).
#if CFG_OS == CFG_OS_WINDOWS
			utki::log([](auto&o){o << "WARNING: Waiting for writing to UDP socket does not work on Win32";});
#endif
		}else{
			utki::assert(triggered[0].flags.get(opros::ready::write), SL);
			utki::assert(!triggered[0].flags.get(opros::ready::read), SL);
		}

		ws.remove(send_sock);
	}catch(std::exception& e){
		utki::assert(false, [&](auto&o){o << e.what();}, SL);
	}
}
}

namespace test_ip_address{
void run(){
	try{//test IP-address without port string parsing
		try{//test correct string
			setka::address ip("127.0.0.1", 80);
			utki::assert(ip.host.get_v4() == 0x7f000001, SL);
			utki::assert(ip.port == 80, SL);
		}catch(std::exception&){
			utki::assert(false, SL);
		}
		
		try{//test correct string
			setka::address ip("127.0.0.1:23ddqwd", 80);
			utki::assert(ip.host.get_v4() == 0x7f000001, SL);
			utki::assert(ip.port == 80, SL);
		}catch(std::exception&){
			utki::assert(true, SL);
		}
		try{//test correct string
			setka::address ip("127.0.0.2555:23ddqwd", 80);
			utki::assert(ip.host.get_v4() == 0x7f0000ff, SL);
			utki::assert(ip.port == 80, SL);
		}catch(std::exception&){
			utki::assert(true, SL);
		}
		
		try{//test incorrect string
			setka::address ip("127.0.1803:65536");
			utki::assert(false, SL);
		}catch(std::exception&){
			//should get here
		}catch(...){
			utki::assert(false, SL);
		}
		
		try{//test incorrect string
			setka::address ip("127.0.270.1:65536");
			utki::assert(false, SL);
		}catch(std::exception&){
			//should get here
		}catch(...){
			utki::assert(false, SL);
		}
	}catch(...){
		utki::assert(false, SL);
	}
	
	try{//test IP-address with port string parsing
		try{//test correct string
			setka::address ip("127.0.0.1:80");
			utki::assert(ip.host.get_v4() == 0x7f000001, SL);
			utki::assert(ip.port == 80, SL);
		}catch(std::exception&){
			utki::assert(false, SL);
		}
		
		try{//test incorrect string
			setka::address ip("127.0.0.1803:43");
			utki::assert(false, SL);
		}catch(std::exception&){
			//should get here
		}catch(...){
			utki::assert(false, SL);
		}
		
		try{//test incorrect string
			setka::address ip("127.0.0.180p43");
			utki::assert(false, SL);
		}catch(std::exception&){
			//should get here
		}catch(...){
			utki::assert(false, SL);
		}
		
		try{//test incorrect string
			setka::address ip("127.0.0.180:123456");
			utki::assert(false, SL);
		}catch(std::exception&){
			//should get here
		}catch(...){
			utki::assert(false, SL);
		}
		
		try{//test incorrect string
			setka::address ip("127.0.0.180:72345");
			utki::assert(false, SL);
		}catch(std::exception&){
			//should get here
		}catch(...){
			utki::assert(false, SL);
		}
		
		try{//test incorrect string
			setka::address ip("127.0.0.1803:65536");
			utki::assert(false, SL);
		}catch(std::exception&){
			//should get here
		}catch(...){
			utki::assert(false, SL);
		}
		
		try{//test correct string
			setka::address ip("127.0.0.1:65535");
			utki::assert(ip.host.get_v4() == 0x7f000001, SL);
			utki::assert(ip.port == 0xffff, SL);
		}catch(std::exception&){
			utki::assert(false, SL);
		}
		
		try{//test correct string
			setka::address ip("127.0.0.1:0");
			utki::assert(ip.host.get_v4() == 0x7f000001, SL);
			utki::assert(ip.port == 0, SL);
		}catch(std::exception&){
			utki::assert(false, SL);
		}
		
		try{//test correct string
			setka::address ip("127.0.0.1:6535 ");
			utki::assert(ip.host.get_v4() == 0x7f000001, SL);
			utki::assert(ip.port == 6535, SL);
		}catch(std::exception&){
			utki::assert(false, SL);
		}
		
		try{//test correct string
			setka::address ip("127.0.0.1:6535dwqd 345");
			utki::assert(ip.host.get_v4() == 0x7f000001, SL);
			utki::assert(ip.port == 6535, SL);
		}catch(std::exception&){
			utki::assert(false, SL);
		}
	}catch(...){
		utki::assert(false, SL);
	}
	
	// Test IPv6
	if(is_ipv6_supported_by_os()){
		try{
			setka::address ip("1002:3004:5006::7008:900a");
			utki::assert(ip.port == 0, SL);
			utki::assert(ip.host.quad[0] == 0x10023004, SL);
			utki::assert(ip.host.quad[1] == 0x50060000, SL);
			utki::assert(ip.host.quad[2] == 0x00000000, SL);
			utki::assert(ip.host.quad[3] == 0x7008900a, SL);
		}catch(std::exception& e){
			utki::assert(false, [&](auto&o){o << "exception caught: " << e.what();}, SL);
		}catch(...){
			utki::assert(false, SL);
		}

		try{
			setka::address ip("[1002:3004:5006::7008:900a]:134");
			utki::assert(ip.port == 134, SL);
			utki::assert(ip.host.quad[0] == 0x10023004, SL);
			utki::assert(ip.host.quad[1] == 0x50060000, SL);
			utki::assert(ip.host.quad[2] == 0x00000000, SL);
			utki::assert(ip.host.quad[3] == 0x7008900a, SL);
		}catch(...){
			utki::assert(false, SL);
		}

		try{
			setka::address ip("[::ffff:127.0.0.1]:45");
			utki::assert(ip.port == 45, SL);
			utki::assert(ip.host.is_v4(), SL);
			utki::assert(ip.host.get_v4() == 0x7f000001, SL);
		}catch(...){
			utki::assert(false, SL);
		}
	}
}
}
