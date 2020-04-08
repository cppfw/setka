#include <utki/debug.hpp>
#include "../../src/setka/Setka.hpp"

#include "dns.hpp"
#include "socket.hpp"


inline void TestTingSocket(){
	setka::Setka netLib;
	
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
