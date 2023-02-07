#include <utki/debug.hpp>
#include "../../src/setka/init_guard.hpp"

#include "dns.hpp"
#include "socket.hpp"


inline void TestTingSocket(){
	setka::init_guard netLib;
	
	Basicip_addressTest::run();
	Testip_address::run();
		
	BasicClientServerTest::run();
	BasicUDPSocketsTest::run();
	TestUDPSocketWaitForWriting::run();
	SendDataContinuouslyWithWaitSet::run();
	SendDataContinuously::run();

	test_simple_dns_lookup::run();
	test_request_from_callback::run();
	test_cancel_dns_lookup::run();

	utki::log([&](auto&o){o << "[PASSED]: Socket test" << std::endl;});
}
