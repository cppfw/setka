#include <utki/debug.hpp>
#include "../../src/setka/init_guard.hpp"

#include "dns.hpp"
#include "socket.hpp"


inline void TestTingSocket(){
	setka::init_guard netLib;
	
	Basicip_addressTest::Run();
	Testip_address::Run();
		
	BasicClientServerTest::Run();
	BasicUDPSocketsTest::Run();
	TestUDPSocketWaitForWriting::Run();
	SendDataContinuouslyWithWaitSet::Run();
	SendDataContinuously::Run();

	TestSimpleDNSLookup::Run();
	TestRequestFromCallback::Run();
	TestCancelDNSLookup::Run();

	TRACE_ALWAYS(<< "[PASSED]: Socket test" << std::endl)
}
