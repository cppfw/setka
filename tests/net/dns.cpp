#include "dns.hpp"

#include "../../src/setka/dns_resolver.hpp"

#include <nitki/thread.hpp>
#include <nitki/semaphore.hpp>

#include <memory>
#include <vector>

namespace TestSimpleDNSLookup{

class Resolver : public setka::dns_resolver{
	
public:
	
	Resolver(nitki::semaphore& sema, const std::string& hostName = std::string()) :
			sema(sema),
			hostName(hostName)
	{}
	
	setka::address::ip ip;
	
	nitki::semaphore& sema;
	
	setka::dns_result res;
	
	std::string hostName;
	
	void Resolve(){
		this->resolve(this->hostName, 10000);
	}
	
	void on_completed(setka::dns_result res, setka::address::ip ip)noexcept override{
		LOG([&](auto&o){o << "onCompleted_ts(): result = " << unsigned(res) << " ip = " << ip.to_string() << std::endl;})
		
//		ASSERT_INFO_ALWAYS(result == ting::net::dns_resolver::OK, "result = " << result)
		this->res = res;
		
		this->ip = ip;
		
		this->sema.signal();
	}
};

void Run(){
	{//test one resolve at a time
		nitki::semaphore sema;

		Resolver r(sema);

		r.resolve("google.com", 10000);

		LOG([&](auto&o){o << "TestSimpleDNSLookup::Run(): waiting on semaphore" << std::endl;})
		
		if(!sema.wait(11000)){
			utki::assert(false, SL);
		}

		utki::assert(
			r.res == setka::dns_result::ok,
			[&](auto&o){o << "r.result = " << unsigned(r.res);},
			SL
		);

//		ASSERT_INFO_ALWAYS(r.ip == 0x4D581503 || r.ip == 0x57FAFB03, "r.ip = " << r.ip)
		utki::assert(
			r.ip.is_valid(),
			[&](auto&o){o << "ip = " << r.ip.to_string();},
			SL
		);

		LOG([&](auto&o){o << "ip = " << r.ip.to_string() << std::endl;})
	}
	
	{//test several resolves at a time
		nitki::semaphore sema;

		typedef std::vector<std::unique_ptr<Resolver> > T_ResolverList;
		typedef T_ResolverList::iterator T_ResolverIter;
		T_ResolverList r;

		r.push_back(std::make_unique<Resolver>(sema, "google.ru"));
		r.push_back(std::make_unique<Resolver>(sema, "ya.ru"));
		r.push_back(std::make_unique<Resolver>(sema, "mail.ru"));
		r.push_back(std::make_unique<Resolver>(sema, "vk.com"));
		
//		TRACE(<< "starting resolutions" << std::endl)
		
		for(T_ResolverIter i = r.begin(); i != r.end(); ++i){
			(*i)->Resolve();
		}
		
		for(unsigned i = 0; i < r.size(); ++i){
			if(!sema.wait(11000)){
				utki::assert(false, SL);
			}
		}
//		TRACE(<< "resolutions done" << std::endl)
		
		for(T_ResolverIter i = r.begin(); i != r.end(); ++i){
			utki::assert(
				(*i)->res == setka::dns_result::ok,
				[&](auto&o){o << "result = " << unsigned((*i)->res) << " host to resolve = " << (*i)->hostName;},
				SL
			);
//			ASSERT_INFO_ALWAYS((*i)->ip == 0x4D581503 || (*i)->ip == 0x57FAFB03, "(*i)->ip = " << (*i)->ip)
			utki::assert((*i)->ip.is_valid(), SL);
		}
	}
}

}



namespace TestRequestFromCallback{

class Resolver : public setka::dns_resolver{
	
public:
	
	Resolver(nitki::semaphore& sema) :
			sema(sema)
	{}
	
	std::string host;
	
	setka::address::ip ip;
	
	nitki::semaphore& sema;
	
	setka::dns_result res;
	

	void on_completed(setka::dns_result res, setka::address::ip ip)noexcept override{
//		ASSERT_INFO_ALWAYS(result == ting::net::dns_resolver::OK, "result = " << result)
		
		if(this->host.size() == 0){
			utki::assert(
				res == setka::dns_result::not_found,
				[&](auto&o){o << "result = " << unsigned(res);},
				SL
			);
			utki::assert(!ip.is_valid(), SL);
			
			this->host = "ya.ru";
			this->resolve(this->host, 5000);
		}else{
			utki::assert(this->host == "ya.ru", SL);
			this->res = res;
			this->ip = ip;
			this->sema.signal();
		}
	}
};

void Run(){
	nitki::semaphore sema;
	
	Resolver r(sema);
	
	r.resolve("rfesfdf.ru", 3000);
	
	if(!sema.wait(8000)){
		utki::assert(false, SL);
	}
	
	utki::assert(
		r.res == setka::dns_result::ok,
		[&](auto&o){o << "r.result = " << unsigned(r.res);},
		SL
	);

//	ASSERT_INFO_ALWAYS(r.ip == 0x4D581503 || r.ip == 0x57FAFB03, "r.ip = " << r.ip)
	utki::assert(r.ip.is_valid(), SL);
}
}



namespace TestCancelDNSLookup{
class Resolver : public setka::dns_resolver{
	
public:
	
	Resolver(){}
	
	volatile bool called = false;
	
	void on_completed(setka::dns_result res, setka::address::ip ip)noexcept override{
		this->called = true;
	}
};

void Run(){
	TRACE_ALWAYS(<< "\tRunning 'cacnel DNS lookup' test, it will take about 4 seconds" << std::endl)
	Resolver r;
	
	r.resolve("rfesweefdqfdf.ru", 3000, setka::address("1.2.3.4", 53));
	
	std::this_thread::sleep_for(std::chrono::milliseconds(500));
	
	bool res = r.cancel();
	
	std::this_thread::sleep_for(std::chrono::milliseconds(3000));
	
	utki::assert(res, SL);
	
	utki::assert(!r.called, SL);
}
}
