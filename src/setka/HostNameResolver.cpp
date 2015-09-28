#include <map>
#include <list>
#include <cstring>

#include <utki/config.hpp>
#include <utki/PoolStored.hpp>
#include <utki/types.hpp>

#include <nitki/MsgThread.hpp>

#include <aika/timer.hpp>

#if M_OS == M_OS_LINUX || M_OS == M_OS_MACOSX || M_OS == M_OS_UNIX
#	include <papki/FSFile.hpp>
#endif

#include "HostNameResolver.hpp"
#include "UDPSocket.hpp"
#include "Lib.hpp"



using namespace setka;



namespace{



const std::uint16_t D_DNSRecordA = 1;
const std::uint16_t D_DNSRecordAAAA = 28;


namespace dns{

//forward declaration
struct Resolver;



//After the successful completion the 'p' points to the byte right after the host name.
//In case of unsuccessful completion 'p' is undefined.
std::string ParseHostNameFromDNSPacket(const std::uint8_t* & p, const std::uint8_t* end){
	std::string host;
			
	for(;;){
		if(p == end){
			return "";
		}

		std::uint8_t len = *p;
		++p;

		if(len == 0){
			break;
		}

		if(host.size() != 0){//if not first label
			host += '.';
		}

		if(end - p < len){
			return "";
		}

		host += std::string(reinterpret_cast<const char*>(p), size_t(len));
		p += len;
		ASSERT(p <= end - 1 || p == end)
	}
//			TRACE(<< "host = " << host << std::endl)
	
	return host;
}



//this mutex is used to protect the dns::thread access.
std::mutex mutex;

typedef std::multimap<std::uint32_t, Resolver*> T_ResolversTimeMap;
typedef T_ResolversTimeMap::iterator T_ResolversTimeIter;

typedef std::map<std::uint16_t, Resolver*> T_IdMap;
typedef T_IdMap::iterator T_IdIter;

typedef std::list<Resolver*> T_RequestsToSendList;
typedef T_RequestsToSendList::iterator T_RequestsToSendIter;

typedef std::map<HostNameResolver*, std::unique_ptr<Resolver> > T_ResolversMap;
typedef T_ResolversMap::iterator T_ResolversIter;



struct Resolver : public utki::PoolStored<Resolver, 10>{
	HostNameResolver* hnr;
	
	std::string hostName; //host name to resolve
	
	std::uint16_t recordType; //type of DNS record to get
	
	T_ResolversTimeMap* timeMap;
	T_ResolversTimeIter timeMapIter;
	
	std::uint16_t id;
	T_IdIter idIter;
	
	T_RequestsToSendIter sendIter;
	
	setka::IPAddress dns;
};



class LookupThread : public nitki::MsgThread{
	setka::UDPSocket socket;
	pogodi::WaitSet waitSet;
	
	T_ResolversTimeMap resolversByTime1, resolversByTime2;
	
public:
	std::mutex mutex;//this mutex is used to protect access to members of the thread object.
	
	//this mutex is used to make sure that the callback has finished calling when Cancel_ts() method is called.
	//I.e. to guarantee that after Cancel_ts() method has returned the callback will not be called anymore.
	std::mutex completedMutex;
	
	//this variable is for joining and destroying previous thread object if there was any.
	std::unique_ptr<nitki::MsgThread> prevThread;
	
	//this is to indicate that the thread is exiting and new DNS lookup requests should be queued to
	//a new thread.
	volatile bool isExiting = true;//initially the thread is not running, so set to true
	
	//This variable is for detecting system clock ticks warp around.
	//True if last call to ting::GetTicks() returned value in first half.
	//False otherwise.
	bool lastTicksInFirstHalf;
	
	T_ResolversTimeMap* timeMap1;
	T_ResolversTimeMap* timeMap2;
	
	T_RequestsToSendList sendList;
	
	T_ResolversMap resolversMap;
	T_IdMap idMap;
	
	setka::IPAddress dns;
	
	void StartSending(){
		this->waitSet.change(this->socket, pogodi::Waitable::READ_AND_WRITE);
	}
	
	//NOTE: call to this function should be protected by mutex.
	//throws HostNameResolver::TooMuchRequestsExc if all IDs are occupied.
	std::uint16_t FindFreeId(){
		if(this->idMap.size() == 0){
			return 0;
		}
		
		if(this->idMap.begin()->first != 0){
			return this->idMap.begin()->first - 1;
		}
		
		if((--(this->idMap.end()))->first != std::uint16_t(-1)){
			return (--(this->idMap.end()))->first + 1;
		}
		
		T_IdIter i1 = this->idMap.begin();
		T_IdIter i2 = ++this->idMap.begin();
		for(; i2 != this->idMap.end(); ++i1, ++i2){
			if(i2->first - i1->first > 1){
				return i1->first + 1;
			}
		}
		
		throw HostNameResolver::TooMuchRequestsExc();
	}
	
	
	//NOTE: call to this function should be protected by mutex, to make sure the request is not canceled while sending.
	//returns true if request is sent, false otherwise.
	bool SendRequestToDNS(const dns::Resolver* r){
		std::array<std::uint8_t, 512> buf; //RFC 1035 limits DNS request UDP packet size to 512 bytes.
		
		size_t packetSize =
				2 + //ID
				2 + //flags
				2 + //Number of questions
				2 + //Number of answers
				2 + //Number of authority records
				2 + //Number of other records
				r->hostName.size() + 2 + //domain name
				2 + //Question type
				2   //Question class
			;
		
		ASSERT(packetSize <= buf.size())
		
		std::uint8_t* p = &*buf.begin();
		
		//ID
		utki::serialize16BE(r->id, p);
		p += 2;
		
		//flags
		utki::serialize16BE(0x100, p);
		p += 2;
		
		//Number of questions
		utki::serialize16BE(1, p);
		p += 2;
		
		//Number of answers
		utki::serialize16BE(0, p);
		p += 2;
		
		//Number of authority records
		utki::serialize16BE(0, p);
		p += 2;
		
		//Number of other records
		utki::serialize16BE(0, p);
		p += 2;
		
		//domain name
		for(size_t dotPos = 0; dotPos < r->hostName.size();){
			size_t oldDotPos = dotPos;
			dotPos = r->hostName.find('.', dotPos);
			if(dotPos == std::string::npos){
				dotPos = r->hostName.size();
			}
			
			ASSERT(dotPos <= 0xff)
			size_t labelLength = dotPos - oldDotPos;
			ASSERT(labelLength <= 0xff)
			
			*p = std::uint8_t(labelLength);//save label length
			++p;
			//copy the label bytes
			memcpy(p, r->hostName.c_str() + oldDotPos, labelLength);
			p += labelLength;
			
			++dotPos;
			
			ASSERT(p <= &*buf.end());
		}
		
		*p = 0; //terminate labels sequence
		++p;
		
		utki::serialize16BE(r->recordType, p);
		p += 2;
		
		//Question class (1 means inet)
		utki::serialize16BE(1, p);
		p += 2;
		
		ASSERT(&*buf.begin() <= p && p <= &*buf.end());
		ASSERT(size_t(p - &*buf.begin()) == packetSize);
		
		TRACE(<< "sending DNS request to " << std::hex << (r->dns.host.getIPv4Host()) << std::dec << " for " << r->hostName << ", reqID = " << r->id << std::endl)
		size_t ret = this->socket.send(utki::Buf<std::uint8_t>(&*buf.begin(), packetSize), r->dns);
		
		ASSERT(ret == packetSize || ret == 0)
		
//		TRACE(<< "DNS request sent, packetSize = " << packetSize << std::endl)
//#ifdef DEBUG
//		for(unsigned i = 0; i < packetSize; ++i){
//			TRACE(<< int(buf[i]) << std::endl)
//		}
//#endif
		return ret == packetSize;
	}
	
	
	
	//NOTE: call to this function should be protected by mutex
	inline void CallCallback(dns::Resolver* r, setka::HostNameResolver::E_Result result, IPAddress::Host ip = IPAddress::Host(0, 0, 0, 0))noexcept{
		this->completedMutex.lock();
		this->mutex.unlock();
		r->hnr->onCompleted_ts(result, ip);
		this->completedMutex.unlock();
		this->mutex.lock();
	}
	
	struct ParseResult{
		setka::HostNameResolver::E_Result result;
		setka::IPAddress::Host host;
		
		ParseResult(setka::HostNameResolver::E_Result result, setka::IPAddress::Host host = setka::IPAddress::Host(0, 0, 0, 0)) :
				result(result),
				host(host)
		{}
	};
	
	//NOTE: call to this function should be protected by mutex
	//This function will call the Resolver callback.
	ParseResult ParseReplyFromDNS(dns::Resolver* r, const utki::Buf<std::uint8_t> buf){
		TRACE(<< "dns::Resolver::ParseReplyFromDNS(): enter" << std::endl)
#ifdef DEBUG
		for(unsigned i = 0; i < buf.size(); ++i){
			TRACE(<< std::hex << int(buf[i]) << std::dec << std::endl)
		}
#endif
		
		if(buf.size() <
				2 + //ID
				2 + //flags
				2 + //Number of questions
				2 + //Number of answers
				2 + //Number of authority records
				2   //Number of other records
			)
		{
			return ParseResult(setka::HostNameResolver::E_Result::DNS_ERROR);
		}
		
		const std::uint8_t* p = buf.begin();
		p += 2;//skip ID
		
		{
			std::uint16_t flags = utki::deserialize16BE(p);
			p += 2;
			
			if((flags & 0x8000) == 0){//we expect it to be a response, not query.
				TRACE(<< "ParseReplyFromDNS(): (flags & 0x8000) = " << (flags & 0x8000) << std::endl)
				return ParseResult(setka::HostNameResolver::E_Result::DNS_ERROR);
			}
			
			//Check response code
			if((flags & 0xf) != 0){//0 means no error condition
				if((flags & 0xf) == 3){//name does not exist
					return ParseResult(setka::HostNameResolver::E_Result::NO_SUCH_HOST);
				}else{
					TRACE(<< "ParseReplyFromDNS(): (flags & 0xf) = " << (flags & 0xf) << std::endl)
					return ParseResult(setka::HostNameResolver::E_Result::DNS_ERROR);
				}
			}
		}
		
		{//check number of questions
			std::uint16_t numQuestions = utki::deserialize16BE(p);
			p += 2;
			
			if(numQuestions != 1){
				return ParseResult(setka::HostNameResolver::E_Result::DNS_ERROR);
			}
		}
		
		std::uint16_t numAnswers = utki::deserialize16BE(p);
		p += 2;
		ASSERT(buf.begin() <= p)
		ASSERT(p <= (buf.end() - 1) || p == buf.end())
		
		if(numAnswers == 0){
			return ParseResult(setka::HostNameResolver::E_Result::NO_SUCH_HOST);
		}
		
		{
//			std::uint16_t nscount = utki::deserialize16BE(p);
			p += 2;
		}
		
		{
//			std::uint16_t arcount = utki::deserialize16BE(p);
			p += 2;
		}
		
		//parse host name
		{
			std::string host = dns::ParseHostNameFromDNSPacket(p, buf.end());
//			TRACE(<< "host = " << host << std::endl)
			
			if(r->hostName != host){
//				TRACE(<< "this->hostName = " << this->hostName << std::endl)
				return ParseResult(setka::HostNameResolver::E_Result::DNS_ERROR);//wrong host name for ID.
			}
		}
		
		//check query type, we sent question type 1 (A query).
		{
			std::uint16_t type = utki::deserialize16BE(p);
			p += 2;
			
			if(type != r->recordType){
				return ParseResult(setka::HostNameResolver::E_Result::DNS_ERROR);//wrong question type
			}
		}
		
		//check query class, we sent question class 1 (inet).
		{
			std::uint16_t cls = utki::deserialize16BE(p);
			p += 2;
			
			if(cls != 1){
				return ParseResult(setka::HostNameResolver::E_Result::DNS_ERROR);//wrong question class
			}
		}
		
		ASSERT(buf.Overlaps(p) || p == buf.end())
		
		//loop through the answers
		for(std::uint16_t n = 0; n != numAnswers; ++n){
			if(p == buf.end()){
				return ParseResult(setka::HostNameResolver::E_Result::DNS_ERROR);//unexpected end of packet
			}
			
			//check if there is a domain name or a reference to the domain name
			if(((*p) >> 6) == 0){ //check if two high bits are set
				//skip possible domain name
				for(; p != buf.end() && *p != 0; ++p){
					ASSERT(buf.Overlaps(p))
				}
				if(p == buf.end()){
					return ParseResult(setka::HostNameResolver::E_Result::DNS_ERROR);//unexpected end of packet
				}
				++p;
			}else{
				//it is a reference to the domain name.
				//skip it
				p += 2;
			}
			
			if(buf.end() - p < 2){
				return ParseResult(setka::HostNameResolver::E_Result::DNS_ERROR);//unexpected end of packet
			}
			std::uint16_t type = utki::deserialize16BE(p);
			p += 2;
			
			if(buf.end() - p < 2){
				return ParseResult(setka::HostNameResolver::E_Result::DNS_ERROR);//unexpected end of packet
			}
//			std::uint16_t cls = ting::util::Deserialize16(p);
			p += 2;
			
			if(buf.end() - p < 4){
				return ParseResult(setka::HostNameResolver::E_Result::DNS_ERROR);//unexpected end of packet
			}
//			std::uint32_t ttl = ting::util::Deserialize32(p);//time till the returned value can be cached.
			p += 4;
			
			if(buf.end() - p < 2){
				return ParseResult(setka::HostNameResolver::E_Result::DNS_ERROR);//unexpected end of packet
			}
			std::uint16_t dataLen = utki::deserialize16BE(p);
			p += 2;
			
			if(buf.end() - p < dataLen){
				return ParseResult(setka::HostNameResolver::E_Result::DNS_ERROR);//unexpected end of packet
			}
			if(type == r->recordType){
				IPAddress::Host h;
				
				switch(type){
					case D_DNSRecordA: //'A' type answer
						if(dataLen < 4){
							return ParseResult(setka::HostNameResolver::E_Result::DNS_ERROR);//unexpected end of packet
						}

						h = IPAddress::Host(utki::deserialize32BE(p));
						break;
					case D_DNSRecordAAAA: //'AAAA' type answer
						if(dataLen < 2 * 8){
							return ParseResult(setka::HostNameResolver::E_Result::DNS_ERROR);//unexpected end of packet
						}

						h = IPAddress::Host(
								utki::deserialize32BE(p),
								utki::deserialize32BE(p + 4),
								utki::deserialize32BE(p + 8),
								utki::deserialize32BE(p + 12)
							);
						break;
					default:
						//we should not get here since if type is not the record type which we know then 'if(type == r->recordType)' condition will not trigger.
						ASSERT(false)
						h = IPAddress::Host(0,0,0,0);
						break;
				}
				
				TRACE(<< "host resolved: " << r->hostName << " = " << h.ToString() << std::endl)
				return ParseResult(setka::HostNameResolver::E_Result::OK, h);
			}
			p += dataLen;
		}
		
		return ParseResult(setka::HostNameResolver::E_Result::DNS_ERROR);//no answer found
	}
	
	
	
public:
	LookupThread() :
			waitSet(2),
			timeMap1(&resolversByTime1),
			timeMap2(&resolversByTime2)
	{
		ASSERT_INFO(setka::Lib::IsCreated(), "ting::net::Lib is not initialized before doing the DNS request")
	}
public:
	~LookupThread()noexcept{
		ASSERT(this->sendList.size() == 0)
		ASSERT(this->resolversMap.size() == 0)
		ASSERT(this->resolversByTime1.size() == 0)
		ASSERT(this->resolversByTime2.size() == 0)
		ASSERT(this->idMap.size() == 0)		
	}
	
	//returns Ptr owning the removed resolver, returns invalid Ptr if there was
	//no such resolver object found.
	//NOTE: call to this function should be protected by mutex.
	std::unique_ptr<dns::Resolver> RemoveResolver(HostNameResolver* resolver)noexcept{
		std::unique_ptr<dns::Resolver> r;
		{
			dns::T_ResolversIter i = this->resolversMap.find(resolver);
			if(i == this->resolversMap.end()){
				return r;
			}
			r = std::move(i->second);
			this->resolversMap.erase(i);
		}

		//the request is active, remove it from all the maps

		//if the request was not sent yet
		if(r->sendIter != this->sendList.end()){
			this->sendList.erase(r->sendIter);
		}

		r->timeMap->erase(r->timeMapIter);

		this->idMap.erase(r->idIter);
		
		return r;
	}
	
private:
	//NOTE: call to this function should be protected by dns::mutex
	void RemoveAllResolvers(){
		while(this->resolversMap.size() != 0){
			std::unique_ptr<dns::Resolver> r = this->RemoveResolver(this->resolversMap.begin()->first);
			ASSERT(r)

#if M_OS == M_OS_WINDOWS && defined(ERROR)
#	undef ERROR
#endif

			//OnCompleted_ts() does not throw any exceptions, so no worries about that.
			this->CallCallback(r.operator->(), HostNameResolver::E_Result::ERROR);
		}
	}
	
	
	void InitDNS(){
		try{
#if M_OS == M_OS_WINDOWS
			struct WinRegKey{
				HKEY	key;
			
				WinRegKey(){
					if(RegOpenKey(
							HKEY_LOCAL_MACHINE,
							"SYSTEM\\ControlSet001\\Services\\Tcpip\\Parameters\\Interfaces",
							&this->key
						) != ERROR_SUCCESS)
					{
						throw setka::Exc("InitDNS(): RegOpenKey() failed");
					}
				}
				
				~WinRegKey(){
					RegCloseKey(this->key);
				}
			} key;
			
			std::array<char, 256> subkey;//according to MSDN docs maximum key name length is 255 chars.
			
			for(unsigned i = 0; RegEnumKey(key.key, i, &*subkey.begin(), subkey.size()) == ERROR_SUCCESS; ++i){
				HKEY hSub;
				if(RegOpenKey(key.key, &*subkey.begin(), &hSub) != ERROR_SUCCESS){
					continue;
				}
				
				std::array<BYTE, 1024> value;
				
				DWORD len = value.size();
				
				if(RegQueryValueEx(hSub, "NameServer", 0, NULL, &*value.begin(), &len) != ERROR_SUCCESS){
					TRACE(<< "NameServer reading failed " << std::endl)
				}else{
					try{
						std::string str(reinterpret_cast<char*>(&*value.begin()));
						size_t spaceIndex = str.find(' ');

						std::string ip = str.substr(0, spaceIndex);
						TRACE(<< "NameServer ip = " << ip << std::endl)
				
						this->dns = setka::IPAddress(ip.c_str(), 53);
						RegCloseKey(hSub);
						return;
					}catch(...){}
				}

				len = value.size();
				if(RegQueryValueEx(hSub, "DhcpNameServer", 0, NULL, &*value.begin(), &len) != ERROR_SUCCESS){
					TRACE(<< "DhcpNameServer reading failed " << std::endl)
					RegCloseKey(hSub);
					continue;
				}

				try{
					std::string str(reinterpret_cast<char*>(&*value.begin()));
					size_t spaceIndex = str.find(' ');

					std::string ip = str.substr(0, spaceIndex);
					TRACE(<< "DhcpNameServer ip = " << ip << std::endl)
				
					this->dns = setka::IPAddress(ip.c_str(), 53);
					RegCloseKey(hSub);
					return;
				}catch(...){}
				RegCloseKey(hSub);
			}

#elif M_OS == M_OS_LINUX || M_OS == M_OS_MACOSX || M_OS == M_OS_UNIX
			papki::FSFile f("/etc/resolv.conf");
			
			std::vector<std::uint8_t> buf = f.loadWholeFileIntoMemory(0xfff);//4kb max
			
			for(std::uint8_t* p = &*buf.begin(); p != &*buf.end(); ++p){
				std::uint8_t* start = p;
				
				while(p != &*buf.end() && *p != '\n'){
					++p;
				}
				
				ASSERT(p >= start)
				std::string line(reinterpret_cast<const char*>(start), size_t(p - start));
				if(p == &*buf.end()){
					--p;
				}
				
				const std::string ns("nameserver ");
				
				size_t nsStart = line.find(ns);
				if(nsStart != 0){
					continue;
				}
				
				size_t ipStart = nsStart + ns.size();
				
				size_t ipEnd = line.find_first_not_of(":.0123456789", ipStart);//IPv6 address may contain ':'
				
				std::string ipstr = line.substr(ipStart, ipEnd - ipStart);
				
				TRACE(<< "dns ipstr = " << ipstr << std::endl)
				
				try{
					this->dns = setka::IPAddress(ipstr.c_str(), 53);
					return;
				}catch(...){}
			}
#else
			TRACE(<< "InitDNS(): don't know how to get DNS IP on this OS" << std::endl)
#endif
		}catch(...){
		}
		this->dns = setka::IPAddress(std::uint32_t(0), 0);
	}
	
	
	void run()override{
		TRACE(<< "DNS lookup thread started" << std::endl)
		
		//destroy previous thread if necessary
		if(this->prevThread){
			//NOTE: if the thread was not started due to some error during adding its
			//first DNS lookup request it is OK to call Join() on such not
			//started thread.
			this->prevThread->join();
			this->prevThread.reset();
			TRACE(<< "Previous thread destroyed" << std::endl)
		}
		
		TRACE(<< "calling this->InitDNS()" << std::endl)
		
		this->InitDNS();
		
		TRACE(<< "this->dns.host = " << this->dns.host.toString() << std::endl)
		
		{
			std::lock_guard<decltype(dns::mutex)> mutexGuard(dns::mutex);//mutex is needed because socket opening may fail and we will have to set isExiting flag which should be protected by mutex
			
			try{
				this->socket.open();
			}catch(...){
				this->isExiting = true;
				this->RemoveAllResolvers();
				return;
			}
		}
		
		this->waitSet.add(this->queue, pogodi::Waitable::READ);
		this->waitSet.add(this->socket, pogodi::Waitable::READ);
		
		while(!this->quitFlag){
			std::uint32_t timeout;
			{
				std::lock_guard<decltype(this->mutex)> mutexGuard(this->mutex);
				
				if(this->socket.errorCondition()){
					this->isExiting = true;
					this->RemoveAllResolvers();
					break;//exit thread
				}

				if(this->socket.canRead()){
					TRACE(<< "can read" << std::endl)
					try{
						std::array<std::uint8_t, 512> buf;//RFC 1035 limits DNS request UDP packet size to 512 bytes. So, no need to allocate bigger buffer.
						setka::IPAddress address;
						size_t ret = this->socket.recieve(buf, address);
						
						ASSERT(ret != 0)
						ASSERT(ret <= buf.size())
						if(ret >= 13){//at least there should be standard header and host name, otherwise ignore received UDP packet
							std::uint16_t id = utki::deserialize16BE(&*buf.begin());
							
							T_IdIter i = this->idMap.find(id);
							if(i != this->idMap.end()){
								ASSERT(id == i->second->id)
								
								//check by host name also
								const std::uint8_t* p = &*buf.begin() + 12;//start of the host name
								std::string host = dns::ParseHostNameFromDNSPacket(p, &*buf.end());
								
								if(host == i->second->hostName){
									ParseResult res = this->ParseReplyFromDNS(i->second, utki::Buf<std::uint8_t>(&*buf.begin(), ret));
									
									if(res.result == setka::HostNameResolver::E_Result::NO_SUCH_HOST && i->second->recordType == D_DNSRecordAAAA){
										//try getting record type A
										TRACE(<< "no record AAAA found, trying to get record type A" << std::endl)
										
										i->second->recordType = D_DNSRecordA;
										
										//add to send list
										ASSERT(i->second->sendIter == this->sendList.end())
										try{
											this->sendList.push_back(i->second);
											i->second->sendIter = --this->sendList.end();
											if(this->sendList.size() == 1){//if need to switch to wait for writing mode
												this->StartSending();
											}
										}catch(...){
											//failed adding to sending list, report error
											std::unique_ptr<dns::Resolver> r = this->RemoveResolver(i->second->hnr);
											this->CallCallback(r.operator->(), setka::HostNameResolver::E_Result::ERROR);
										}										
									}else{
										std::unique_ptr<dns::Resolver> r = this->RemoveResolver(i->second->hnr);
										//call callback
										this->CallCallback(r.operator->(), res.result, res.host);
									}
								}
							}
						}
					}catch(setka::Exc&){
						this->isExiting = true;
						this->RemoveAllResolvers();
						break;//exit thread
					}
				}

//				TRACE(<< "this->sendList.size() = " << (this->sendList.size()) << std::endl)
//Workaround for strange bug on Win32 (reproduced on WinXP at least).
//For some reason waiting for WRITE on UDP socket does not work. It hangs in the
//Wait() method until timeout is hit. So, just try to send data to the socket without waiting for WRITE.
#if M_OS == M_OS_WINDOWS
				if(this->sendList.size() != 0)
#else
				if(this->socket.canWrite())
#endif
				{
					TRACE(<< "can write" << std::endl)
					//send request
					ASSERT(this->sendList.size() > 0)
					
					try{
						while(this->sendList.size() != 0){
							dns::Resolver* r = this->sendList.front();
							if(r->dns.host.getIPv4Host() == 0){
								r->dns = this->dns;
							}

							if(r->dns.host.isValid()){
								if(!this->SendRequestToDNS(r)){
									TRACE(<< "request not sent" << std::endl)
									break;//socket is not ready for sending, go out of requests sending loop.
								}
								TRACE(<< "request sent" << std::endl)
								r->sendIter = this->sendList.end();//end() value will indicate that the request has already been sent
								this->sendList.pop_front();
							}else{
								std::unique_ptr<dns::Resolver> removedResolver = this->RemoveResolver(r->hnr);
								ASSERT(removedResolver)

								//Notify about error. OnCompleted_ts() does not throw any exceptions, so no worries about that.
								this->CallCallback(removedResolver.operator->(), HostNameResolver::E_Result::ERROR, 0);
							}
						}
					}catch(setka::Exc& DEBUG_CODE(e)){
						TRACE(<< "writing to a socket failed: " << e.what() << std::endl)
						this->isExiting = true;
						this->RemoveAllResolvers();
						break;//exit thread
					}
					
					if(this->sendList.size() == 0){
						//move socket to waiting for READ condition only
						this->waitSet.change(this->socket, pogodi::Waitable::READ);
						TRACE(<< "socket wait mode changed to read only" << std::endl)
					}
				}
				
				std::uint32_t curTime = aika::getTicks();
				{//check if time has warped around and it is necessary to swap time maps
					bool isFirstHalf = curTime < (std::uint32_t(-1) / 2);
					if(isFirstHalf && !this->lastTicksInFirstHalf){
						//Time warped.
						//Timeout all requests from first time map
						while(this->timeMap1->size() != 0){
							std::unique_ptr<dns::Resolver> r = this->RemoveResolver(this->timeMap1->begin()->second->hnr);
							ASSERT(r)

							//Notify about timeout. OnCompleted_ts() does not throw any exceptions, so no worries about that.
							this->CallCallback(r.operator->(), HostNameResolver::E_Result::TIMEOUT, 0);
						}
						
						ASSERT(this->timeMap1->size() == 0)
						std::swap(this->timeMap1, this->timeMap2);
					}
					this->lastTicksInFirstHalf = isFirstHalf;
				}
				
				while(this->timeMap1->size() != 0){
					if(this->timeMap1->begin()->first > curTime){
						break;
					}
					
					//timeout
					std::unique_ptr<dns::Resolver> r = this->RemoveResolver(this->timeMap1->begin()->second->hnr);
					ASSERT(r)
					
					//Notify about timeout. OnCompleted_ts() does not throw any exceptions, so no worries about that.
					this->CallCallback(r.operator->(), HostNameResolver::E_Result::TIMEOUT, 0);
				}
				
				if(this->resolversMap.size() == 0){
					this->isExiting = true;
					break;//exit thread
				}
				
				ASSERT(this->timeMap1->size() > 0)
				ASSERT(this->timeMap1->begin()->first > curTime)
				
//				TRACE(<< "DNS thread: curTime = " << curTime << std::endl)
//				TRACE(<< "DNS thread: this->timeMap1->begin()->first = " << (this->timeMap1->begin()->first) << std::endl)
				
				timeout = this->timeMap1->begin()->first - curTime;
			}
			
			//Make sure that ting::GetTicks is called at least 4 times per full time warp around cycle.
			utki::clampTop(timeout, std::uint32_t(-1) / 4);
			
//Workaround for strange bug on Win32 (reproduced on WinXP at least).
//For some reason waiting for WRITE on UDP socket does not work. It hangs in the
//Wait() method until timeout is hit. So, just check every 100ms if it is OK to write to UDP socket.
#if M_OS == M_OS_WINDOWS
			if(this->sendList.size() > 0){
				setka::util::ClampTop(timeout, std::uint32_t(100));
			}
#endif
			
			TRACE(<< "DNS thread: waiting with timeout = " << timeout << std::endl)
			if(this->waitSet.waitWithTimeout(timeout) == 0){
				//no Waitables triggered
//				TRACE(<< "timeout hit" << std::endl)
				continue;
			}
			
			if(this->queue.canRead()){
				while(auto m = std::move(this->queue.peekMsg())){
					m();
				}
			}			
		}//~while(!this->quitFlag)
		
		this->waitSet.remove(this->socket);
		this->waitSet.remove(this->queue);
		TRACE(<< "DNS lookup thread stopped" << std::endl)
	}
};

//accessing this variable must be protected by dnsMutex
std::unique_ptr<LookupThread> thread;



}//~namespace
}//~namespace



HostNameResolver::~HostNameResolver(){
#ifdef DEBUG
	//check that there is no ongoing DNS lookup operation.
	std::lock_guard<decltype(dns::mutex)> mutexGuard(dns::mutex);
	
	if(dns::thread){
		std::lock_guard<decltype(dns::thread->mutex)> mutexGuard(dns::thread->mutex);
		
		dns::T_ResolversIter i = dns::thread->resolversMap.find(this);
		if(i != dns::thread->resolversMap.end()){
			ASSERT_INFO_ALWAYS(false, "trying to destroy the HostNameResolver object while DNS lookup request is in progress, call HostNameResolver::Cancel_ts() first.")
		}
	}
#endif
}



void HostNameResolver::resolve_ts(const std::string& hostName, std::uint32_t timeoutMillis, const setka::IPAddress& dnsIP){
//	TRACE(<< "HostNameResolver::Resolve_ts(): enter" << std::endl)
	
	ASSERT(setka::Lib::IsCreated())
	
	if(hostName.size() > 253){
		throw DomainNameTooLongExc();
	}
	
	std::lock_guard<decltype(dns::mutex)> mutexGuard(dns::mutex);
	
	bool needStartTheThread = false;
	
	//check if thread is created
	if(!dns::thread){
		dns::thread = utki::makeUnique<dns::LookupThread>();
		needStartTheThread = true;
	}else{
		std::lock_guard<decltype(dns::thread->mutex)> mutexGuard(dns::thread->mutex);
		
		//check if already in progress
		if(dns::thread->resolversMap.find(this) != dns::thread->resolversMap.end()){
			throw AlreadyInProgressExc();
		}

		//Thread is created, check if it is running.
		//If there are active requests then the thread must be running.
		if(dns::thread->isExiting == true){
			auto t = utki::makeUnique<dns::LookupThread>();
			t->prevThread = std::move(dns::thread);
			dns::thread = std::move(t);
			needStartTheThread = true;
		}
	}
	
	ASSERT(dns::thread)
	
	std::unique_ptr<dns::Resolver> r(new dns::Resolver());
	r->hnr = this;
	r->hostName = hostName;
	r->dns = dnsIP;
	
#if M_OS == M_OS_WINDOWS
	//check OS version, if WinXP then start from record A, since ting does not support IPv6 on WinXP
	{
		OSVERSIONINFO osvi;
		memset(&osvi, 0, sizeof(osvi));
		osvi.dwOSVersionInfoSize = sizeof(osvi);

		GetVersionEx(&osvi); //TODO: GetVersionEx() is deprecated, replace with VerifyVersionInfo()

		if(osvi.dwMajorVersion > 5){
			r->recordType = D_DNSRecordAAAA;//start with IPv6 first
		}else{
			r->recordType = D_DNSRecordA;
		}
	}
#else
	r->recordType = D_DNSRecordAAAA;//start with IPv6 first
#endif
	
	std::lock_guard<decltype(dns::thread->mutex)> mutexGuard2(dns::thread->mutex);
	
	//Find free ID, it will throw TooMuchRequestsExc if there are no free IDs
	{
		r->id = dns::thread->FindFreeId();
		std::pair<dns::T_IdIter, bool> res =
				dns::thread->idMap.insert(std::pair<std::uint16_t, dns::Resolver*>(r->id, r.operator->()));
		ASSERT(res.second)
		r->idIter = res.first;
	}
	
	//calculate time
	std::uint32_t curTime = aika::getTicks();
	{
		std::uint32_t endTime = curTime + timeoutMillis;
//		TRACE(<< "HostNameResolver::Resolve_ts(): curTime = " << curTime << std::endl)
//		TRACE(<< "HostNameResolver::Resolve_ts(): endTime = " << endTime << std::endl)
		if(endTime < curTime){//if warped around
			r->timeMap = dns::thread->timeMap2;
		}else{
			r->timeMap = dns::thread->timeMap1;
		}
		try{
			r->timeMapIter = r->timeMap->insert(std::pair<std::uint32_t, dns::Resolver*>(endTime, r.operator->()));
		}catch(...){
			dns::thread->idMap.erase(r->idIter);
			throw;
		}
	}
	
	//add resolver to send queue
	try{
		dns::thread->sendList.push_back(r.operator->());
	}catch(...){
		r->timeMap->erase(r->timeMapIter);
		dns::thread->idMap.erase(r->idIter);
		throw;
	}
	r->sendIter = --dns::thread->sendList.end();
	
	//insert the resolver to main resolvers map
	try{
		dns::thread->resolversMap[this] = std::move(r);
	
		//If there was no send requests in the list, send the message to the thread to switch
		//socket to wait for sending mode.
		if(dns::thread->sendList.size() == 1){
			std::unique_ptr<dns::LookupThread>& t = dns::thread;
			dns::thread->pushMessage(
					[&t](){
						t->StartSending();
					}
				);
		}

		//Start the thread if we created the new one.
		if(needStartTheThread){
			dns::thread->lastTicksInFirstHalf = curTime < (std::uint32_t(-1) / 2);
			dns::thread->start();
			dns::thread->isExiting = false;//thread has just started, clear the exiting flag
			TRACE(<< "HostNameResolver::Resolve_ts(): thread started" << std::endl)
		}
	}catch(...){
		dns::thread->resolversMap.erase(this);
		dns::thread->sendList.pop_back();
		r->timeMap->erase(r->timeMapIter);
		dns::thread->idMap.erase(r->idIter);
		throw;
	}
}



bool HostNameResolver::cancel_ts()noexcept{
	std::lock_guard<decltype(dns::mutex)> mutexGuard(dns::mutex);
	
	if(!dns::thread){
		return false;
	}
	
	std::lock_guard<decltype(dns::thread->mutex)> mutexGuard2(dns::thread->mutex);
	
	bool ret = bool(dns::thread->RemoveResolver(this));
	
	if(dns::thread->resolversMap.size() == 0){
		dns::thread->pushPreallocatedQuitMessage();
	}
	
	if(!ret){
		//Make sure the callback has finished if it is in process of calling the callback.
		//Because upon calling the callback the resolver object is already removed from all the lists and maps
		//and if 'ret' is false then it is possible that the resolver is in process of calling the callback.
		//To do that, lock and unlock the mutex.
		std::lock_guard<decltype(dns::thread->completedMutex)> mutexGuard(dns::thread->completedMutex);
	}
	
	return ret;
}



//static
void HostNameResolver::cleanUp(){
	std::lock_guard<decltype(dns::mutex)> mutexGuard(dns::mutex);

	if(dns::thread){
		dns::thread->pushPreallocatedQuitMessage();
		dns::thread->join();

		ASSERT_INFO(dns::thread->resolversMap.size() == 0, "There are active DNS requests upon Sockets library de-initialization, all active DNS requests must be canceled before that.")

		dns::thread.reset();
	}
}
