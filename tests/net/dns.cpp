#include "dns.hpp"

#include "../../src/setka/dns_resolver.hpp"

#include <nitki/thread.hpp>
#include <nitki/semaphore.hpp>

#include <memory>
#include <vector>

#ifdef assert
#	undef assert
#endif

namespace test_simple_dns_lookup{

class resolver : public setka::dns_resolver{
	
public:
	
	resolver(nitki::semaphore& sema, std::string host_name = std::string()) :
			sema(sema),
			host_name(std::move(host_name))
	{}
	
	setka::address::ip ip;
	
	nitki::semaphore& sema;
	
	setka::dns_result res;
	
	std::string host_name;
	
	void resolve_host(){
		this->resolve(this->host_name, 10000);
	}
	
	void on_completed(setka::dns_result res, setka::address::ip ip)noexcept override{
		LOG([&](auto&o){o << "onCompleted_ts(): result = " << unsigned(res) << " ip = " << ip.to_string() << std::endl;})
		
//		ASSERT_INFO_ALWAYS(result == ting::net::dns_resolver::OK, "result = " << result)
		this->res = res;
		
		this->ip = ip;
		
		this->sema.signal();
	}
};

void run(){
	{//test one resolve at a time
		nitki::semaphore sema;

		resolver r(sema);

		r.resolve("google.com", 10000);

		LOG([&](auto&o){o << "test_simple_dns_lookup::run(): waiting on semaphore" << std::endl;})
		
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

		using resolver_list_type = std::vector<std::unique_ptr<resolver> >;
		resolver_list_type r;

		r.push_back(std::make_unique<resolver>(sema, "google.ru"));
		r.push_back(std::make_unique<resolver>(sema, "ya.ru"));
		r.push_back(std::make_unique<resolver>(sema, "mail.ru"));
		r.push_back(std::make_unique<resolver>(sema, "vk.com"));
		
//		TRACE(<< "starting resolutions" << std::endl)
		
		for(auto& rslvr : r){
			rslvr->resolve_host();
		}
		
		for(unsigned i = 0; i < r.size(); ++i){
			if(!sema.wait(11000)){
				utki::assert(false, SL);
			}
		}
//		TRACE(<< "resolutions done" << std::endl)
		
		for(auto& rslvr: r){
			utki::assert(
				rslvr->res == setka::dns_result::ok,
				[&](auto&o){o << "result = " << unsigned(rslvr->res) << " host to resolve = " << rslvr->host_name;},
				SL
			);

			utki::assert(rslvr->ip.is_valid(), SL);
		}
	}
}

}



namespace test_request_from_callback{

class resolver : public setka::dns_resolver{
	
public:
	
	resolver(nitki::semaphore& sema) :
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

void run(){
	nitki::semaphore sema;
	
	resolver r(sema);
	
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



namespace test_cancel_dns_lookup{
class resolver : public setka::dns_resolver{
	
public:	
	volatile bool called = false;
	
	void on_completed(setka::dns_result res, setka::address::ip ip)noexcept override{
		this->called = true;
	}
};

void run(){
	utki::log([&](auto&o){o << "\tRunning 'cacnel DNS lookup' test, it will take about 4 seconds" << std::endl;});
	resolver r;
	
	r.resolve("rfesweefdqfdf.ru", 3000, setka::address("1.2.3.4", 53));
	
	std::this_thread::sleep_for(std::chrono::milliseconds(500));
	
	bool res = r.cancel();
	
	std::this_thread::sleep_for(std::chrono::milliseconds(3000));
	
	utki::assert(res, SL);
	
	utki::assert(!r.called, SL);
}
}
