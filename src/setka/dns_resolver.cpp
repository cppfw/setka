/*
The MIT License (MIT)

Copyright (c) 2015-2023 Ivan Gagis <igagis@gmail.com>

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

/* ================ LICENSE END ================ */

#include <cstring>
#include <list>
#include <map>
#include <memory>

#include <nitki/queue.hpp>
#include <nitki/thread.hpp>
#include <opros/wait_set.hpp>
#include <utki/config.hpp>
#include <utki/time.hpp>
#include <utki/types.hpp>
#include <utki/util.hpp>

#if CFG_OS == CFG_OS_LINUX || CFG_OS == CFG_OS_MACOSX || CFG_OS == CFG_OS_UNIX
#	include <papki/fs_file.hpp>
#endif

#include "dns_resolver.hpp"
#include "init_guard.hpp"
#include "udp_socket.hpp"

using namespace setka;

namespace {

const uint16_t dns_record_a_id = 1;
const uint16_t dns_record_aaaa_id = 28;

namespace dns {

class resolver;

// After the successful completion the 'p' points to the byte right after the host name.
// In case of unsuccessful completion 'p' is undefined.
std::string parse_host_name_from_dns_packet(const uint8_t*& p, const uint8_t* end)
{
	std::string host;

	for (;;) {
		if (p == end) {
			return "";
		}

		uint8_t len = *p;
		++p; // NOLINT

		if (len == 0) {
			break;
		}

		if (host.size() != 0) { // if not first label
			host += '.';
		}

		if (end - p < len) {
			return "";
		}

		host += std::string(reinterpret_cast<const char*>(p), size_t(len)); // NOLINT
		p += len; // NOLINT
		ASSERT(p <= end - 1 || p == end)
	}
	//			TRACE(<< "host = " << host << std::endl)

	return host;
}

// this mutex is used to protect the dns::thread access.
std::mutex mutex; // NOLINT

using resolvers_time_map_type = std::multimap<uint32_t, resolver*>;
using resolvers_time_iter_type = resolvers_time_map_type::iterator;

using id_map_type = std::map<uint16_t, resolver*>;
using id_iter_type = id_map_type::iterator;

using requests_to_send_list_type = std::list<resolver*>; // TODO: use vector
using requests_to_send_iter_type = requests_to_send_list_type::iterator;

using resolvers_map_type = std::map<dns_resolver*, std::unique_ptr<resolver>>;
using resolvers_iter_type = resolvers_map_type::iterator;

class resolver
{
public:
	// TODO: figure out why on MacOS it caomplains about this line
	// NOLINTNEXTLINE(bugprone-exception-escape, "for some reason clang-tidy complains about this line only on MacOS")
	resolver() = default;

	resolver(const resolver&) = default;
	resolver& operator=(const resolver&) = default;

	resolver(resolver&&) = delete;
	resolver& operator=(resolver&&) = delete;

	~resolver() = default;

	dns_resolver* hnr = nullptr;

	std::string host_name; // host name to resolve

	uint16_t recordType = 0; // type of DNS record to get

	resolvers_time_map_type* timeMap = nullptr;
	resolvers_time_iter_type timeMapIter;

	uint16_t id = 0;
	id_iter_type idIter;

	requests_to_send_iter_type sendIter; // TODO: get rid of iter

	setka::address dns;
};

class lookup_thread : public nitki::thread
{
	setka::udp_socket socket;
	opros::wait_set wait_set;

	resolvers_time_map_type resolvers_by_time_1, resolvers_by_time_2;

public:
	volatile bool quit_flag = false;
	nitki::queue queue;

	std::mutex mutex; // this mutex is used to protect access to members of the thread object.

	// this mutex is used to make sure that the callback has finished calling when Cancel_ts() method is called.
	// I.e. to guarantee that after Cancel_ts() method has returned the callback will not be called anymore.
	std::mutex completed_mutex;

	// this variable is for joining and destroying previous thread object if there was any.
	std::unique_ptr<nitki::thread> prev_thread;

	// this is to indicate that the thread is exiting and new DNS lookup requests should be queued to
	// a new thread.
	volatile bool is_exiting = true; // initially the thread is not running, so set to true

	// This variable is for detecting system clock ticks warp around.
	// True if last call to ting::GetTicks() returned value in first half.
	// False otherwise.
	bool last_ticks_in_first_half = false;

	resolvers_time_map_type* time_map_1;
	resolvers_time_map_type* time_map_2;

	requests_to_send_list_type send_list;

	resolvers_map_type resolvers_map;
	id_map_type id_map;

	setka::address dns;

	void start_sending()
	{
		this->wait_set.change(this->socket, utki::make_flags({opros::ready::read, opros::ready::write}), &this->socket);
	}

	// NOTE: call to this function should be protected by mutex.
	//       throws dns_resolver::TooMuchRequestsExc if all IDs are occupied.
	uint16_t find_free_id()
	{
		if (this->id_map.size() == 0) {
			return 0;
		}

		if (this->id_map.begin()->first != 0) {
			return this->id_map.begin()->first - 1;
		}

		if ((--(this->id_map.end()))->first != uint16_t(-1)) {
			return (--(this->id_map.end()))->first + 1;
		}

		auto i1 = this->id_map.begin();
		auto i2 = ++this->id_map.begin();
		for (; i2 != this->id_map.end(); ++i1, ++i2) {
			if (i2->first - i1->first > 1) {
				return i1->first + 1;
			}
		}

		throw dns_resolver::too_many_requests();
	}

	// NOTE: call to this function should be protected by mutex, to make sure the request is not canceled while sending.
	//       returns true if request is sent, false otherwise.
	bool send_request_to_dns(const dns::resolver* r)
	{
		// RFC 1035 limits DNS request UDP packet size to 512 bytes.
		std::array<uint8_t, utki::kilobyte / 2> buf; // NOLINT

		size_t packet_size = 2 + // ID
			2 + // flags
			2 + // Number of questions
			2 + // Number of answers
			2 + // Number of authority records
			2 + // Number of other records
			r->host_name.size() + 2 + // domain name
			2 + // Question type
			2 // Question class
			;

		ASSERT(packet_size <= buf.size())

		uint8_t* p = &*buf.begin();

		// ID
		utki::serialize16be(r->id, p);
		p += 2; // NOLINT

		// flags
		utki::serialize16be(0x100, p); // NOLINT
		p += 2; // NOLINT

		// Number of questions
		utki::serialize16be(1, p);
		p += 2; // NOLINT

		// Number of answers
		utki::serialize16be(0, p);
		p += 2; // NOLINT

		// Number of authority records
		utki::serialize16be(0, p);
		p += 2; // NOLINT

		// Number of other records
		utki::serialize16be(0, p);
		p += 2; // NOLINT

		// domain name
		for (size_t dot_pos = 0; dot_pos < r->host_name.size();) {
			size_t old_dot_pos = dot_pos;
			dot_pos = r->host_name.find('.', dot_pos);
			if (dot_pos == std::string::npos) {
				dot_pos = r->host_name.size();
			}

			ASSERT(dot_pos <= 0xff)
			size_t label_length = dot_pos - old_dot_pos;
			ASSERT(label_length <= 0xff)

			*p = uint8_t(label_length); // save label length
			++p; // NOLINT
			// copy the label bytes
			memcpy(p, r->host_name.c_str() + old_dot_pos, label_length); // NOLINT
			p += label_length; // NOLINT

			++dot_pos;

			ASSERT(p <= &*buf.end());
		}

		*p = 0; // terminate labels sequence
		++p; // NOLINT

		utki::serialize16be(r->recordType, p);
		p += 2; // NOLINT

		// Question class (1 means inet)
		utki::serialize16be(1, p);
		p += 2; // NOLINT

		ASSERT(&*buf.begin() <= p && p <= &*buf.end());
		ASSERT(size_t(p - &*buf.begin()) == packet_size);

		LOG([&](auto& o) {
			o << "sending DNS request to " << std::hex << (r->dns.host.get_v4()) << std::dec << " for " << r->host_name
			  << ", reqID = " << r->id << std::endl;
		})
		size_t ret = this->socket.send(utki::make_span(&*buf.begin(), packet_size), r->dns);

		ASSERT(ret == packet_size || ret == 0)

		//		TRACE(<< "DNS request sent, packet_size = " << packet_size << std::endl)
		// #ifdef DEBUG
		//		for(unsigned i = 0; i < packet_size; ++i){
		//			TRACE(<< int(buf[i]) << std::endl)
		//		}
		// #endif
		return ret == packet_size;
	}

	// NOTE: call to this function should be protected by mutex
	inline void call_callback(
		dns::resolver* r,
		setka::dns_result result,
		address::ip ip = address::ip(0, 0, 0, 0)
	) noexcept
	{
		this->completed_mutex.lock();
		this->mutex.unlock();
		try {
			r->hnr->on_completed(result, ip);
		} catch (...) {
			// ignore
		}
		this->completed_mutex.unlock();
		this->mutex.lock();
	}

	struct parse_result {
		setka::dns_result result;
		setka::address::ip host;

		parse_result(setka::dns_result result, setka::address::ip host = setka::address::ip(0, 0, 0, 0)) :
			result(result),
			host(host)
		{}
	};

	// NOTE: call to this function should be protected by mutex,
	//       this function will call the resolver callback
	parse_result parse_reply_from_dns(dns::resolver* r, const utki::span<uint8_t> buf)
	{
		LOG([&](auto& o) {
			o << "dns::resolver::parse_reply_from_dns(): enter" << std::endl;
		})
#ifdef DEBUG
		for (unsigned i = 0; i < buf.size(); ++i) {
			LOG([&](auto& o) {
				o << std::hex << int(buf[i]) << std::dec << std::endl;
			})
		}
#endif

		if (buf.size() < 2 + // ID
				2 + // flags
				2 + // Number of questions
				2 + // Number of answers
				2 + // Number of authority records
				2 // Number of other records
		)
		{
			return {setka::dns_result::dns_error};
		}

		const uint8_t* p = buf.begin();
		// skip ID
		p += 2; // NOLINT

		{
			uint16_t flags = utki::deserialize16be(p);
			p += 2; // NOLINT

			constexpr auto response_flag_mask = 0x8000;
			if ((flags & response_flag_mask) == 0) { // we expect it to be a response, not query.
				LOG([&](auto& o) {
					o << "parse_reply_from_dns(): (flags & response_flag_mask) = " << (flags & response_flag_mask)
					  << std::endl;
				})
				return {setka::dns_result::dns_error};
			}

			// Check response code
			if ((flags & utki::lower_nibble_mask) != 0) { // 0 means no error condition
				if ((flags & utki::lower_nibble_mask) == 3) { // name does not exist
					return {setka::dns_result::not_found};
				} else {
					LOG([&](auto& o) {
						o << "parse_reply_from_dns(): (flags & 0xf) = " << (flags & 0xf) << std::endl;
					})
					return {setka::dns_result::dns_error};
				}
			}
		}

		// check number of questions
		{
			uint16_t num_questions = utki::deserialize16be(p);
			p += 2; // NOLINT

			if (num_questions != 1) {
				return {setka::dns_result::dns_error};
			}
		}

		uint16_t num_answers = utki::deserialize16be(p);
		p += 2; // NOLINT
		ASSERT(buf.begin() <= p)
		ASSERT(p <= (buf.end() - 1) || p == buf.end())

		if (num_answers == 0) {
			return {setka::dns_result::not_found};
		}

		{
			//			uint16_t nscount = utki::deserialize16be(p);
			p += 2; // NOLINT
		}

		{
			//			uint16_t arcount = utki::deserialize16be(p);
			p += 2; // NOLINT
		}

		// parse host name
		{
			std::string host = dns::parse_host_name_from_dns_packet(p, buf.end());
			//			TRACE(<< "host = " << host << std::endl)

			if (r->host_name != host) {
				//				TRACE(<< "this->host_name = " << this->host_name << std::endl)
				return {setka::dns_result::dns_error}; // wrong host name for ID.
			}
		}

		// check query type, we sent question type 1 (A query).
		{
			uint16_t type = utki::deserialize16be(p);
			p += 2; // NOLINT

			if (type != r->recordType) {
				return {setka::dns_result::dns_error}; // wrong question type
			}
		}

		// check query class, we sent question class 1 (inet).
		{
			uint16_t cls = utki::deserialize16be(p);
			p += 2; // NOLINT

			if (cls != 1) {
				return {setka::dns_result::dns_error}; // wrong question class
			}
		}

		ASSERT(buf.overlaps(p) || p == buf.end())

		// loop through the answers
		for (uint16_t n = 0; n != num_answers; ++n) {
			if (p == buf.end()) {
				return {setka::dns_result::dns_error}; // unexpected end of packet
			}

			// check if there is a domain name or a reference to the domain name

			// check if two high bits are set
			if (((*p) >> 6) == 0) { // NOLINT
				// skip possible domain name
				for (; p != buf.end() && *p != 0; ++p) { // NOLINT
					ASSERT(buf.overlaps(p))
				}
				if (p == buf.end()) {
					return {setka::dns_result::dns_error}; // unexpected end of packet
				}
				++p; // NOLINT
			} else {
				// it is a reference to the domain name.
				// skip it
				p += 2; // NOLINT
			}

			if (buf.end() - p < 2) {
				return {setka::dns_result::dns_error}; // unexpected end of packet
			}
			uint16_t type = utki::deserialize16be(p);
			p += 2; // NOLINT

			if (buf.end() - p < 2) {
				return {setka::dns_result::dns_error}; // unexpected end of packet
			}
			//			uint16_t cls = ting::util::Deserialize16(p);
			p += 2; // NOLINT

			if (buf.end() - p < 4) {
				return {setka::dns_result::dns_error}; // unexpected end of packet
			}
			//			uint32_t ttl = ting::util::Deserialize32(p);//time till the returned value can be cached.
			p += 4; // NOLINT

			if (buf.end() - p < 2) {
				return {setka::dns_result::dns_error}; // unexpected end of packet
			}
			uint16_t data_len = utki::deserialize16be(p);
			p += 2; // NOLINT

			if (buf.end() - p < data_len) {
				return {setka::dns_result::dns_error}; // unexpected end of packet
			}
			if (type == r->recordType) {
				address::ip h{};

				switch (type) {
					case dns_record_a_id: // 'A' type answer
						if (data_len < 4) {
							return {setka::dns_result::dns_error}; // unexpected end of packet
						}

						h = address::ip(utki::deserialize32be(p));
						break;
					case dns_record_aaaa_id: // 'AAAA' type answer
						if (data_len < 2 * 8) { // NOLINT
							return {setka::dns_result::dns_error}; // unexpected end of packet
						}

						h = address::ip(
							utki::deserialize32be(p),
							utki::deserialize32be(p + 4), // NOLINT
							utki::deserialize32be(p + 4 * 2), // NOLINT
							utki::deserialize32be(p + 4 * 3) // NOLINT
						);
						break;
					default:
						// we should not get here since if type is not the record type which we know then 'if(type ==
						// r->recordType)' condition will not trigger.
						ASSERT(false)
						h = address::ip(0, 0, 0, 0);
						break;
				}

				LOG([&](auto& o) {
					o << "host resolved: " << r->host_name << " = " << h.to_string() << std::endl;
				})
				return {setka::dns_result::ok, h};
			}
			p += data_len; // NOLINT
		}

		return {setka::dns_result::dns_error}; // no answer found
	}

public:
	lookup_thread() :
		wait_set(2),
		time_map_1(&resolvers_by_time_1),
		time_map_2(&resolvers_by_time_2)
	{
		// TODO: get init guard?
		// ASSERT(setka::init_guard::is_created(), [&](auto& o) {
		// 	o << "setka::init_guard is not created before doing the DNS request";
		// })
	}

	// NOLINTNEXTLINE(modernize-use-equals-default, "destructor is not trivial in DEBUG build")
	~lookup_thread() noexcept override{ASSERT(this->send_list.size() == 0) ASSERT(this->resolvers_map.size() == 0)
										   ASSERT(this->resolvers_by_time_1.size() == 0)
											   ASSERT(this->resolvers_by_time_2.size() == 0)
												   ASSERT(this->id_map.size() == 0)}

	lookup_thread(const lookup_thread&) = delete;
	lookup_thread& operator=(const lookup_thread&) = delete;

	lookup_thread(lookup_thread&&) = delete;
	lookup_thread& operator=(lookup_thread&&) = delete;

	// returns the removed resolver, returns nullptr if there was
	// no such resolver object found.
	// NOTE: call to this function should be protected by mutex.
	std::unique_ptr<dns::resolver> remove_resolver(dns_resolver* resolver) noexcept
	{
		std::unique_ptr<dns::resolver> r;
		{
			auto i = this->resolvers_map.find(resolver);
			if (i == this->resolvers_map.end()) {
				return r;
			}
			r = std::move(i->second);
			this->resolvers_map.erase(i);
		}

		// the request is active, remove it from all the maps

		// if the request was not sent yet
		if (r->sendIter != this->send_list.end()) {
			this->send_list.erase(r->sendIter);
		}

		r->timeMap->erase(r->timeMapIter);

		this->id_map.erase(r->idIter);

		return r;
	}

private:
	// NOTE: call to this function should be protected by dns::mutex
	void remove_all_resolvers()
	{
		while (this->resolvers_map.size() != 0) {
			std::unique_ptr<dns::resolver> r = this->remove_resolver(this->resolvers_map.begin()->first);
			ASSERT(r)

#if CFG_OS == CFG_OS_WINDOWS && defined(ERROR)
#	undef ERROR
#endif

			// on_completed() does not throw any exceptions, so no worries about that.
			this->call_callback(r.operator->(), dns_result::error);
		}
	}

	void init_dns()
	{
		try {
#if CFG_OS == CFG_OS_WINDOWS
			struct win_reg_key {
				HKEY key;

				win_reg_key()
				{
					if (RegOpenKey(
							HKEY_LOCAL_MACHINE,
							R"(SYSTEM\ControlSet001\Services\Tcpip\Parameters\Interfaces)",
							&this->key
						)
						!= ERROR_SUCCESS)
					{
						throw std::runtime_error("init_dns(): RegOpenKey() failed");
					}
				}

				~win_reg_key()
				{
					RegCloseKey(this->key);
				}
			} key;

			std::array<char, 256> subkey; // according to MSDN docs maximum key name length is 255 chars.

			for (unsigned i = 0; RegEnumKey(key.key, i, &*subkey.begin(), DWORD(subkey.size())) == ERROR_SUCCESS; ++i) {
				HKEY h_sub;
				if (RegOpenKey(key.key, &*subkey.begin(), &h_sub) != ERROR_SUCCESS) {
					continue;
				}

				std::array<BYTE, 1024> value;

				auto len = DWORD(value.size());

				if (RegQueryValueEx(h_sub, "NameServer", nullptr, nullptr, value.data(), &len) != ERROR_SUCCESS) {
					LOG([&](auto& o) {
						o << "NameServer reading failed " << std::endl;
					})
				} else {
					try {
						std::string str(reinterpret_cast<char*>(&*value.begin()));
						size_t space_index = str.find(' ');

						std::string ip = str.substr(0, space_index);
						LOG([&](auto& o) {
							o << "NameServer ip = " << ip << std::endl;
						})

						this->dns = setka::address(ip.c_str(), 53);
						RegCloseKey(h_sub);
						return;
					} catch (...) {
					}
				}

				len = DWORD(value.size());
				if (RegQueryValueEx(h_sub, "DhcpNameServer", nullptr, nullptr, value.data(), &len) != ERROR_SUCCESS) {
					LOG([&](auto& o) {
						o << "DhcpNameServer reading failed " << std::endl;
					})
					RegCloseKey(h_sub);
					continue;
				}

				try {
					std::string str(reinterpret_cast<char*>(&*value.begin()));
					size_t space_index = str.find(' ');

					std::string ip = str.substr(0, space_index);
					LOG([&](auto& o) {
						o << "DhcpNameServer ip = " << ip << std::endl;
					})

					this->dns = setka::address(ip.c_str(), 53);
					RegCloseKey(h_sub);
					return;
				} catch (...) {
				}
				RegCloseKey(h_sub);
			}

#elif CFG_OS == CFG_OS_LINUX || CFG_OS == CFG_OS_MACOSX || CFG_OS == CFG_OS_UNIX
			papki::fs_file f("/etc/resolv.conf");

			std::vector<uint8_t> buf = f.load(size_t(utki::kilobyte) * 4); // 4kb max

			for (uint8_t* p = &*buf.begin(); p != &*buf.end(); ++p) { // NOLINT
				uint8_t* start = p;

				while (p != &*buf.end() && *p != '\n') {
					++p; // NOLINT
				}

				ASSERT(p >= start)
				std::string line(reinterpret_cast<const char*>(start), size_t(p - start)); // NOLINT
				if (p == &*buf.end()) {
					--p; // NOLINT
				}

				const std::string ns("nameserver ");

				size_t ns_start = line.find(ns);
				if (ns_start != 0) {
					continue;
				}

				size_t ip_start = ns_start + ns.size();

				size_t ip_end = line.find_first_not_of(":.0123456789", ip_start); // IPv6 address may contain ':'

				std::string ipstr = line.substr(ip_start, ip_end - ip_start);

				LOG([&](auto& o) {
					o << "dns ipstr = " << ipstr << std::endl;
				})

				try {
					constexpr auto dns_port = 53;
					this->dns = setka::address(ipstr.c_str(), dns_port);
					return;
				} catch (...) {
				}
			}
#else
			LOG([&](auto& o) {
				o << "init_dns(): don't know how to get DNS IP on this OS" << std::endl;
			})
#endif
		} catch (...) {
		}
		this->dns = setka::address(uint32_t(0), 0);
	}

	void run() override
	{
		LOG([&](auto& o) {
			o << "DNS lookup thread started" << std::endl;
		})

		// destroy previous thread if necessary
		if (this->prev_thread) {
			// NOTE: if the thread was not started due to some error during adding its
			// first DNS lookup request it is OK to call Join() on such not
			// started thread.
			this->prev_thread->join();
			this->prev_thread.reset();
			LOG([&](auto& o) {
				o << "Previous thread destroyed" << std::endl;
			})
		}

		LOG([&](auto& o) {
			o << "calling this->init_dns()" << std::endl;
		})

		this->init_dns();

		LOG([&](auto& o) {
			o << "this->dns.host = " << this->dns.host.to_string() << std::endl;
		})

		{
			std::lock_guard<decltype(dns::mutex)> mutex_guard(dns::mutex
			); // mutex is needed because socket opening may fail and we will have to set is_exiting flag which should
			   // be protected by mutex

			try {
				this->socket = setka::udp_socket(0);
			} catch (...) {
				this->is_exiting = true;
				this->remove_all_resolvers();
				return;
			}
		}

		this->wait_set.add(this->queue, utki::make_flags({opros::ready::read}), &this->queue);
		this->wait_set.add(this->socket, utki::make_flags({opros::ready::read}), &this->socket);

		// TODO: rewrite using nitki::loop_thread
		while (!this->quit_flag) {
			uint32_t timeout = 0;

			{
				std::lock_guard<decltype(this->mutex)> mutex_guard(this->mutex);

				utki::flags<opros::ready> flags(false);
				for (const auto& t : this->wait_set.get_triggered()) {
					if (t.user_data == &this->socket) {
						flags = t.flags;
						LOG([&](auto& o) {
							o << "socket flags = " << flags << std::endl;
						})
					}
				}

				if (flags.get(opros::ready::error)) {
					this->is_exiting = true;
					this->remove_all_resolvers();
					break; // exit thread
				}

				if (flags.get(opros::ready::read)) {
					LOG([&](auto& o) {
						o << "can read" << std::endl;
					})
					try {
						// RFC 1035 limits DNS request UDP packet size to 512 bytes. So, no need to allocate bigger
						// buffer.
						// NOLINTNEXTLINE(cppcoreguidelines-pro-type-member-init)
						std::array<uint8_t, utki::kilobyte / 2> buf;
						setka::address address;
						size_t ret = this->socket.recieve(utki::make_span(buf), address);

						constexpr auto host_name_start_offset = 12;

						ASSERT(ret != 0)
						ASSERT(ret <= buf.size())
						// at least there should be standard header and host name, otherwise ignore received UDP packet
						if (ret > host_name_start_offset) {
							uint16_t id = utki::deserialize16be(buf.data());

							auto i = this->id_map.find(id);
							if (i != this->id_map.end()) {
								ASSERT(id == i->second->id)

								// check by host name also
								const uint8_t* p = buf.data() + host_name_start_offset;
								std::string host = dns::parse_host_name_from_dns_packet(p, &*buf.end());

								if (host == i->second->host_name) {
									parse_result res =
										this->parse_reply_from_dns(i->second, utki::span<uint8_t>(&*buf.begin(), ret));

									if (res.result == setka::dns_result::not_found
										&& i->second->recordType == dns_record_aaaa_id)
									{
										// try getting record type A
										LOG([&](auto& o) {
											o << "no record AAAA found, trying to get record type A" << std::endl;
										})

										i->second->recordType = dns_record_a_id;

										// add to send list
										ASSERT(i->second->sendIter == this->send_list.end())
										try {
											this->send_list.push_back(i->second);
											i->second->sendIter = --this->send_list.end();
											if (this->send_list.size() == 1)
											{ // if need to switch to wait for writing mode
												this->start_sending();
											}
										} catch (...) {
											// failed adding to sending list, report error
											std::unique_ptr<dns::resolver> r = this->remove_resolver(i->second->hnr);
											this->call_callback(r.operator->(), setka::dns_result::error);
										}
									} else {
										std::unique_ptr<dns::resolver> r = this->remove_resolver(i->second->hnr);
										// call callback
										this->call_callback(r.operator->(), res.result, res.host);
									}
								}
							}
						}
					} catch (std::exception&) {
						this->is_exiting = true;
						this->remove_all_resolvers();
						break; // exit thread
					}
				}

//				TRACE(<< "this->send_list.size() = " << (this->send_list.size()) << std::endl)
// WORKAROUND: for strange bug on Win32 (reproduced on WinXP at least).
//             For some reason waiting for WRITE on UDP socket does not work. It hangs in the
//             wait() method until timeout is hit. So, just try to send data to the socket without waiting for WRITE.
#if CFG_OS == CFG_OS_WINDOWS
				if (this->send_list.size() != 0)
#else
				if (flags.get(opros::ready::write))
#endif
				{
					LOG([&](auto& o) {
						o << "can write" << std::endl;
					})
					// send request
					ASSERT(this->send_list.size() > 0)

					try {
						while (this->send_list.size() != 0) {
							dns::resolver* r = this->send_list.front();
							if (r->dns.host.get_v4() == 0) {
								r->dns = this->dns;
							}

							if (r->dns.host.is_valid()) {
								if (!this->send_request_to_dns(r)) {
									LOG([&](auto& o) {
										o << "request not sent" << std::endl;
									})
									break; // socket is not ready for sending, go out of requests sending loop.
								}
								LOG([&](auto& o) {
									o << "request sent" << std::endl;
								})
								r->sendIter = this->send_list.end(
								); // end() value will indicate that the request has already been sent
								this->send_list.pop_front();
							} else {
								std::unique_ptr<dns::resolver> removed_resolver = this->remove_resolver(r->hnr);
								ASSERT(removed_resolver)

								// Notify about error. OnCompleted_ts() does not throw any exceptions, so no worries
								// about that.
								this->call_callback(removed_resolver.operator->(), dns_result::error, 0);
							}
						}
					} catch (std::exception&
#ifdef DEBUG
								 e
#endif
					)
					{
						LOG([&](auto& o) {
							o << "writing to a socket failed: " << e.what() << std::endl;
						})
						this->is_exiting = true;
						this->remove_all_resolvers();
						break; // exit thread
					}

					if (this->send_list.size() == 0) {
						// move socket to waiting for READ condition only
						this->wait_set.change(this->socket, utki::make_flags({opros::ready::read}), &this->socket);
						LOG([&](auto& o) {
							o << "socket wait mode changed to read only" << std::endl;
						})
					}
				}

				uint32_t cur_time = utki::get_ticks_ms();
				{ // check if time has wrapped around and it is necessary to swap time maps
					bool is_first_half = cur_time < (uint32_t(-1) / 2);
					if (is_first_half && !this->last_ticks_in_first_half) {
						// Time wrapped.
						// Timeout all requests from first time map
						while (this->time_map_1->size() != 0) {
							std::unique_ptr<dns::resolver> r =
								this->remove_resolver(this->time_map_1->begin()->second->hnr);
							ASSERT(r)

							// Notify about timeout.
							this->call_callback(r.operator->(), dns_result::timeout, 0);
						}

						ASSERT(this->time_map_1->size() == 0)
						std::swap(this->time_map_1, this->time_map_2);
					}
					this->last_ticks_in_first_half = is_first_half;
				}

				while (this->time_map_1->size() != 0) {
					if (this->time_map_1->begin()->first > cur_time) {
						break;
					}

					// timeout
					std::unique_ptr<dns::resolver> r = this->remove_resolver(this->time_map_1->begin()->second->hnr);
					ASSERT(r)

					// Notify about timeout. OnCompleted_ts() does not throw any exceptions, so no worries about that.
					this->call_callback(r.operator->(), dns_result::timeout, 0);
				}

				if (this->resolvers_map.size() == 0) {
					this->is_exiting = true;
					break; // exit thread
				}

				ASSERT(this->time_map_1->size() > 0)
				ASSERT(this->time_map_1->begin()->first > cur_time)

				//				TRACE(<< "DNS thread: cur_time = " << cur_time << std::endl)
				//				TRACE(<< "DNS thread: this->time_map_1->begin()->first = " <<
				//(this->time_map_1->begin()->first)
				//<< std::endl)

				timeout = this->time_map_1->begin()->first - cur_time;
			}

			// Make sure that utki::get_ticks_ms() is called at least 4 times per full time warp around cycle.
			timeout = std::min(timeout, uint32_t(-1) / 4); // clamp top

// Workaround for strange bug on Win32 (reproduced on WinXP at least).
// For some reason waiting for WRITE on UDP socket does not work. It hangs in the
// Wait() method until timeout is hit. So, just check every 100ms if it is OK to write to UDP socket.
#if CFG_OS == CFG_OS_WINDOWS
			if (this->send_list.size() > 0) {
				timeout = std::min(timeout, uint32_t(100)); // clamp top
			}
#endif

			LOG([&](auto& o) {
				o << "DNS thread: waiting with timeout = " << timeout << std::endl;
			})
			bool is_triggered = this->wait_set.wait(timeout);
			if (!is_triggered) {
				// no waitables triggered
				LOG([](auto& o) {
					o << "timeout hit" << std::endl;
				})
				continue;
			}

			while (auto m = this->queue.pop_front()) {
				m();
			}
		} // ~while(!this->quit_flag)

		this->wait_set.remove(this->socket);
		this->wait_set.remove(this->queue);
		LOG([&](auto& o) {
			o << "DNS lookup thread stopped" << std::endl;
		})
	}
};

// accessing this variable must be protected by dnsMutex
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
std::unique_ptr<lookup_thread> thread;

} // namespace dns
} // namespace

// NOLINTNEXTLINE(modernize-use-equals-default, "destructor is not trivial in DEBUG mode")
dns_resolver::~dns_resolver()
{
#ifdef DEBUG
	// check that there is no ongoing DNS lookup operation.
	std::lock_guard<decltype(dns::mutex)> mutex_guard(dns::mutex);

	if (dns::thread) {
		std::lock_guard<decltype(dns::thread->mutex)> mutex_guard(dns::thread->mutex);

		dns::resolvers_iter_type i = dns::thread->resolvers_map.find(this);
		if (i != dns::thread->resolvers_map.end()) {
			utki::assert(
				false,
				[&](auto& o) {
					o << "trying to destroy the dns_resolver object while DNS lookup request is in progress, call dns_resolver::Cancel_ts() first.";
				},
				SL
			);
		}
	}
#endif
}

void dns_resolver::resolve(const std::string& host_name, uint32_t timeout_ms, const setka::address& dns_ip)
{
	//	TRACE(<< "dns_resolver::Resolve_ts(): enter" << std::endl)

	// TODO: get init guard?
	// ASSERT(setka::init_guard::is_created())

	constexpr auto max_host_name_size = 253;

	if (host_name.size() > max_host_name_size) {
		throw std::logic_error("Too long domain name, it should not exceed 253 characters according to RFC 2181");
	}

	std::lock_guard<decltype(dns::mutex)> mutex_guard(dns::mutex);

	bool need_start_the_thread = false;

	// check if thread is created
	if (!dns::thread) {
		dns::thread = std::make_unique<dns::lookup_thread>();
		need_start_the_thread = true;
	} else {
		std::lock_guard<decltype(dns::thread->mutex)> mutex_guard(dns::thread->mutex);

		// check if already in progress
		if (dns::thread->resolvers_map.find(this) != dns::thread->resolvers_map.end()) {
			throw std::logic_error("DNS lookup operation is already in progress");
		}

		// Thread is created, check if it is running.
		// If there are active requests then the thread must be running.
		if (dns::thread->is_exiting == true) {
			auto t = std::make_unique<dns::lookup_thread>();
			t->prev_thread = std::move(dns::thread);
			dns::thread = std::move(t);
			need_start_the_thread = true;
		}
	}

	ASSERT(dns::thread)

	auto r = std::make_unique<dns::resolver>();
	r->hnr = this;
	r->host_name = host_name;
	r->dns = dns_ip;

#if CFG_OS == CFG_OS_WINDOWS
	// check OS version, if WinXP then start from record A, since setka does not support IPv6 on WinXP
	{
		OSVERSIONINFOEX osvi;
		memset(&osvi, 0, sizeof(osvi));
		osvi.dwOSVersionInfoSize = sizeof(osvi);

		osvi.dwMajorVersion = 5; // version 5 is WinXP
		osvi.dwMinorVersion = 0;
		osvi.wServicePackMajor = 0;
		osvi.wServicePackMinor = 0;

		DWORD mask = VER_MAJORVERSION | VER_MINORVERSION | VER_SERVICEPACKMAJOR | VER_SERVICEPACKMINOR;

		if (VerifyVersionInfo(
				&osvi,
				mask,
				VerSetConditionMask(0, mask, VER_GREATER) // we check if current Windows version is greater than WinXP
			)
			== 0)
		{
			DWORD last_error = GetLastError();
			if (last_error != ERROR_OLD_WIN_VERSION) {
				throw std::system_error(int(last_error), std::generic_category(), "Win32: VerifyVersionInfo() failed");
			}

			// Windows version is WinXP or before

			r->recordType = dns_record_a_id;
		} else {
			r->recordType = dns_record_aaaa_id; // start with IPv6 first
		}
	}
#else
	r->recordType = dns_record_aaaa_id; // start with IPv6 first
#endif

	std::lock_guard<decltype(dns::thread->mutex)> mutex_guard_2(dns::thread->mutex);

	// find free ID, it will throw TooMuchRequestsExc if there are no free IDs
	{
		r->id = dns::thread->find_free_id();
		std::pair<dns::id_iter_type, bool> res =
			dns::thread->id_map.insert(std::pair<uint16_t, dns::resolver*>(r->id, r.operator->()));
		ASSERT(res.second)
		r->idIter = res.first;
	}

	// calculate time
	uint32_t cur_time = utki::get_ticks_ms();
	{
		uint32_t end_time = cur_time + timeout_ms;
		//		TRACE(<< "dns_resolver::Resolve_ts(): cur_time = " << cur_time << std::endl)
		//		TRACE(<< "dns_resolver::Resolve_ts(): end_time = " << end_time << std::endl)
		if (end_time < cur_time) { // if warped around
			r->timeMap = dns::thread->time_map_2;
		} else {
			r->timeMap = dns::thread->time_map_1;
		}
		try {
			r->timeMapIter = r->timeMap->insert(std::pair<uint32_t, dns::resolver*>(end_time, r.operator->()));
		} catch (...) {
			dns::thread->id_map.erase(r->idIter);
			throw;
		}
	}

	// add resolver to send queue
	try {
		dns::thread->send_list.push_back(r.operator->());
	} catch (...) {
		r->timeMap->erase(r->timeMapIter);
		dns::thread->id_map.erase(r->idIter);
		throw;
	}
	r->sendIter = --dns::thread->send_list.end();

	// insert the resolver to main resolvers map
	try {
		dns::thread->resolvers_map[this] = std::move(r);

		// If there was no send requests in the list, send the message to the thread to switch
		// socket to wait for sending mode.
		if (dns::thread->send_list.size() == 1) {
			dns::thread->queue.push_back([]() {
				dns::thread->start_sending();
			});
		}

		// Start the thread if we created the new one.
		if (need_start_the_thread) {
			dns::thread->last_ticks_in_first_half = cur_time < (uint32_t(-1) / 2);
			dns::thread->start();
			dns::thread->is_exiting = false; // thread has just started, clear the exiting flag
			LOG([&](auto& o) {
				o << "dns_resolver::Resolve_ts(): thread started" << std::endl;
			})
		}
	} catch (...) {
		dns::thread->resolvers_map.erase(this);
		dns::thread->send_list.pop_back();
		r->timeMap->erase(r->timeMapIter);
		dns::thread->id_map.erase(r->idIter);
		throw;
	}
}

bool dns_resolver::cancel() noexcept
{
	std::lock_guard<decltype(dns::mutex)> mutex_guard(dns::mutex);

	if (!dns::thread) {
		return false;
	}

	std::lock_guard<decltype(dns::thread->mutex)> mutex_guard_2(dns::thread->mutex);

	bool ret = bool(dns::thread->remove_resolver(this));

	if (dns::thread->resolvers_map.size() == 0) {
		dns::thread->quit_flag = true;
		dns::thread->queue.push_back([]() {});
	}

	if (!ret) {
		// Make sure the callback has finished if it is in process of calling the callback.
		// Because upon calling the callback the resolver object is already removed from all the lists and maps
		// and if 'ret' is false then it is possible that the resolver is in process of calling the callback.
		// To do that, lock and unlock the mutex.
		std::lock_guard<decltype(dns::thread->completed_mutex)> mutex_guard(dns::thread->completed_mutex);
	}

	return ret;
}

void dns_resolver::clean_up()
{
	std::lock_guard<decltype(dns::mutex)> mutex_guard(dns::mutex);

	if (dns::thread) {
		dns::thread->quit_flag = true;
		dns::thread->queue.push_back([]() {});
		dns::thread->join();

		ASSERT(dns::thread->resolvers_map.size() == 0, [&](auto& o) {
			o << "There are active DNS requests upon Sockets library de-initialization, all active DNS requests must be canceled before that.";
		})

		dns::thread.reset();
	}
}

void dns_resolver::on_completed(dns_result res, address::ip address)
{
	if (this->completed_handler) {
		this->completed_handler(res, address);
	}
}
