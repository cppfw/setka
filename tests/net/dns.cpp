#include "dns.hpp"

#include "../../src/ting/net/HostNameResolver.hpp"
#include "../../src/ting/mt/Thread.hpp"
#include "../../src/ting/mt/Semaphore.hpp"

#include <memory>
#include <vector>

namespace TestSimpleDNSLookup{

class Resolver : public ting::net::HostNameResolver{
	
public:
	
	Resolver(ting::mt::Semaphore& sema, const std::string& hostName = std::string()) :
			sema(sema),
			hostName(hostName)
	{}
	
	ting::net::IPAddress::Host ip;
	
	ting::mt::Semaphore& sema;
	
	E_Result result;
	
	std::string hostName;
	
	void Resolve(){
		this->Resolve_ts(this->hostName, 10000);
	}
	
	//override
	void OnCompleted_ts(E_Result result, ting::net::IPAddress::Host ip)noexcept{
		TRACE(<< "OnCompleted_ts(): result = " << result << " ip = " << ip.ToString() << std::endl)
		
//		ASSERT_INFO_ALWAYS(result == ting::net::HostNameResolver::OK, "result = " << result)
		this->result = result;
		
		this->ip = ip;
		
		this->sema.Signal();
	}
};

void Run(){
	{//test one resolve at a time
		ting::mt::Semaphore sema;

		Resolver r(sema);

		r.Resolve_ts("google.com", 10000);

		TRACE(<< "TestSimpleDNSLookup::Run(): waiting on semaphore" << std::endl)
		
		if(!sema.Wait(11000)){
			ASSERT_ALWAYS(false)
		}

		ASSERT_INFO_ALWAYS(r.result == ting::net::HostNameResolver::OK, "r.result = " << r.result)

//		ASSERT_INFO_ALWAYS(r.ip == 0x4D581503 || r.ip == 0x57FAFB03, "r.ip = " << r.ip)
		ASSERT_INFO_ALWAYS(r.ip.IsValid(), "ip = " << r.ip.ToString())

		TRACE(<< "ip = " << r.ip.ToString() << std::endl)
	}
	
	{//test several resolves at a time
		ting::mt::Semaphore sema;

		typedef std::vector<std::unique_ptr<Resolver> > T_ResolverList;
		typedef T_ResolverList::iterator T_ResolverIter;
		T_ResolverList r;

		r.push_back(std::unique_ptr<Resolver>(new Resolver(sema, "google.ru")));
		r.push_back(std::unique_ptr<Resolver>(new Resolver(sema, "ya.ru")));
		r.push_back(std::unique_ptr<Resolver>(new Resolver(sema, "mail.ru")));
		r.push_back(std::unique_ptr<Resolver>(new Resolver(sema, "vk.com")));
		
//		TRACE(<< "starting resolutions" << std::endl)
		
		for(T_ResolverIter i = r.begin(); i != r.end(); ++i){
			(*i)->Resolve();
		}
		
		for(unsigned i = 0; i < r.size(); ++i){
			if(!sema.Wait(11000)){
				ASSERT_ALWAYS(false)
			}
		}
//		TRACE(<< "resolutions done" << std::endl)
		
		for(T_ResolverIter i = r.begin(); i != r.end(); ++i){
			ASSERT_INFO_ALWAYS((*i)->result == ting::net::HostNameResolver::OK, "result = " << (*i)->result << " host to resolve = " << (*i)->hostName)
//			ASSERT_INFO_ALWAYS((*i)->ip == 0x4D581503 || (*i)->ip == 0x57FAFB03, "(*i)->ip = " << (*i)->ip)
			ASSERT_ALWAYS((*i)->ip.IsValid())
		}
	}
}

}



namespace TestRequestFromCallback{

class Resolver : public ting::net::HostNameResolver{
	
public:
	
	Resolver(ting::mt::Semaphore& sema) :
			sema(sema)
	{}
	
	std::string host;
	
	ting::net::IPAddress::Host ip;
	
	ting::mt::Semaphore& sema;
	
	E_Result result;
	
	//override
	void OnCompleted_ts(E_Result result, ting::net::IPAddress::Host ip)noexcept{
//		ASSERT_INFO_ALWAYS(result == ting::net::HostNameResolver::OK, "result = " << result)
		
		if(this->host.size() == 0){
			ASSERT_INFO_ALWAYS(result == ting::net::HostNameResolver::NO_SUCH_HOST, "result = " << result)
			ASSERT_ALWAYS(!ip.IsValid())
			
			this->host = "ya.ru";
			this->Resolve_ts(this->host, 5000);
		}else{
			ASSERT_ALWAYS(this->host == "ya.ru")
			this->result = result;
			this->ip = ip;
			this->sema.Signal();
		}
	}
};

void Run(){
	ting::mt::Semaphore sema;
	
	Resolver r(sema);
	
	r.Resolve_ts("rfesfdf.ru", 3000);
	
	if(!sema.Wait(8000)){
		ASSERT_ALWAYS(false)
	}
	
	ASSERT_INFO_ALWAYS(r.result == ting::net::HostNameResolver::OK, "r.result = " << r.result)

//	ASSERT_INFO_ALWAYS(r.ip == 0x4D581503 || r.ip == 0x57FAFB03, "r.ip = " << r.ip)
	ASSERT_ALWAYS(r.ip.IsValid())
}
}



namespace TestCancelDNSLookup{
class Resolver : public ting::net::HostNameResolver{
	
public:
	
	Resolver(){}
	
	volatile bool called = false;
	
	//override
	void OnCompleted_ts(E_Result result, ting::net::IPAddress::Host ip)noexcept{
		this->called = true;
	}
};

void Run(){
	TRACE_ALWAYS(<< "\tRunning 'cacnel DNS lookup' test, it will take about 4 seconds" << std::endl)
	Resolver r;
	
	r.Resolve_ts("rfesweefdqfdf.ru", 3000, ting::net::IPAddress("1.2.3.4", 53));
	
	ting::mt::Thread::Sleep(500);
	
	bool res = r.Cancel_ts();
	
	ting::mt::Thread::Sleep(3000);
	
	ASSERT_ALWAYS(res)
	
	ASSERT_ALWAYS(!r.called)
}
}//~namespace
