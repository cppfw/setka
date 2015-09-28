#include "../../src/ting/debug.hpp"
#include "../../src/ting/net/Lib.hpp"

#include "dns.hpp"
#include "socket.hpp"


inline void TestTingSocket(){
	ting::net::Lib netLib;
	
	BasicIPAddressTest::Run();
	TestIPAddress::Run();
		
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
