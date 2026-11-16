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

#include <atomic>
#include <bitset>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <nitki/queue.hpp>
#include <nitki/thread.hpp>
#include <opros/wait_set.hpp>
#include <utki/config.hpp>
#include <utki/debug.hpp>
#include <utki/log.hpp>
#include <utki/span.hpp>
#include <utki/types.hpp>
#include <utki/utility.hpp>

#if CFG_OS == CFG_OS_WINDOWS
#	include <winreg.h>
#	include <winsock2.h>
#	include <ws2tcpip.h>
#elif CFG_OS == CFG_OS_LINUX || CFG_OS == CFG_OS_MACOSX || CFG_OS == CFG_OS_UNIX
#	include <fsif/native_file.hpp>
#endif

#include "address.hpp"
#include "dns_resolver.hpp"
#include "init_guard.hpp"
#include "tcp_socket.hpp"
#include "udp_socket.hpp"

using namespace setka;

namespace {

constexpr uint16_t dns_type_a = 1;
constexpr uint16_t dns_type_aaaa = 28;
constexpr uint16_t dns_type_opt = 41; // the EDNS0 OPT record

constexpr auto dns_port = uint16_t(53);
// The UDP payload size advertised via EDNS0, chosen so that typical DNS responses fit
// into a single non-fragmented IP datagram.
constexpr auto edns_udp_payload_size = uint16_t(1232);
// The size of the buffer used for receiving UDP responses.
constexpr auto udp_response_buffer_size = edns_udp_payload_size;
// The maximum number of concurrent TCP fallback connections.
constexpr auto max_tcp_fallbacks = 4u;
// The wait set capacity: the wake-up queue + the UDP socket + the TCP fallback connections.
constexpr auto wait_set_capacity = 2u + max_tcp_fallbacks;
// The initial interval between the retransmissions of a DNS query, the interval doubles
// with every retransmission up to max_retransmit_interval_ms.
constexpr auto initial_retransmit_interval_ms = uint32_t(1000);
constexpr auto max_retransmit_interval_ms = uint32_t(8000);
// The maximum lifetime of a positive DNS result cache entry, regardless of the record's TTL.
constexpr auto max_positive_cache_ttl_ms = uint64_t(600 * 1000);
// The lifetime of a negative (not found) DNS result cache entry.
constexpr auto negative_cache_ttl_ms = uint64_t(60 * 1000);
// The maximum number of entries in the DNS result cache.
constexpr auto max_cache_entries = 256u;

using clock_type = std::chrono::steady_clock;
using time_point = clock_type::time_point;
using milliseconds = std::chrono::milliseconds;

// True while the execution is inside a dns_resolver::on_completed() callback invoked by
// the background DNS lookup thread.
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables, "it is a thread local variable, protected by the thread confinement")
thread_local bool in_dns_callback = false;

// -----------------------------------------------------------------------------------------------
// The DNS result cache
// -----------------------------------------------------------------------------------------------

namespace dns_cache {

struct key {
	std::string host;
	// The record type the entry was requested with:
	// A for dns_record_type::ipv4, AAAA for dns_record_type::ipv6, 0 for dns_record_type::both.
	uint16_t type = 0;

	bool operator<(const key& other) const
	{
		if (this->host != other.host) {
			return this->host < other.host;
		}
		return this->type < other.type;
	}
};

struct entry {
	std::vector<setka::address::ip> addresses;
	bool not_found = false;
	time_point expiry{};
};

using map_type = std::map<key, entry>;

} // namespace dns_cache

using cache_key = dns_cache::key;

// -----------------------------------------------------------------------------------------------
// The global state
// -----------------------------------------------------------------------------------------------

// NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables)

namespace dns {

class request;
class lookup_thread;

// Protects the thread pointer, the DNS servers list and the "servers loaded" flag.
std::mutex dns_mutex;

// The background DNS lookup thread.
std::unique_ptr<lookup_thread> thread;

// The DNS servers discovered from the OS.
std::vector<setka::address> dns_servers;
bool dns_servers_loaded = false;
std::string dns_servers_error;

// The resolvers that have an outstanding (not yet invoked) completion callback.
// Global (not per thread), because a lookup request can be completed by a thread that is
// already being replaced by a new one (the thread object that completes the request is
// not necessarily the current dns::thread). Allows the resolvers' destructors/cancel()
// (called from another thread) to wait until their completion callback (if any) is
// finished, so that the callback is never invoked on a destroyed object.
std::mutex pending_mutex;
std::condition_variable pending_cv;
std::set<dns_resolver*> pending;

// Waits until there are no outstanding (not yet invoked) completion callbacks for the
// given resolver. NOTE: must not be called from the background thread.
inline void wait_no_pending(dns_resolver* r)
{
	std::unique_lock p_lock(pending_mutex);
	pending_cv.wait(p_lock, [&r] {
		return pending.count(r) == 0;
	});
}

} // namespace dns

// The DNS result cache.
std::mutex cache_mutex;
dns_cache::map_type cache;

// NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables)

namespace dns {

// -----------------------------------------------------------------------------------------------
// DNS packet building and parsing
// -----------------------------------------------------------------------------------------------

/**
 * @brief Builds a DNS query packet.
 *
 * The query contains a single question and an EDNS0 OPT record in the additional section.
 *
 * NOTE: the caller must ensure that host_name is a valid domain name (labels of 1..255 characters,
 *       total length of at most 253 characters).
 */
std::vector<uint8_t> build_query(uint16_t id, const std::string& host_name, uint16_t type)
{
	// the worst-case packet size: the header (12) + the name (up to 253 characters, each one with a
	// length byte, plus the terminating root label) + the question type and class (4)
	// + the EDNS0 OPT record (11)
	constexpr size_t max_size = 12 + 2 * 253 + 1 + 4 + 11;
	std::vector<uint8_t> buf(max_size);
	uint8_t* p = buf.data();

	// the header
	utki::serialize16be(id, p);
	p += 2; // NOLINT
	utki::serialize16be(0x0100, p); // the RD flag is set // NOLINT
	p += 2; // NOLINT
	utki::serialize16be(1, p); // QDCOUNT
	p += 2; // NOLINT
	utki::serialize16be(0, p); // ANCOUNT
	p += 2; // NOLINT
	utki::serialize16be(0, p); // NSCOUNT
	p += 2; // NOLINT
	utki::serialize16be(1, p); // ARCOUNT (the EDNS0 OPT record)
	p += 2; // NOLINT

	// the question section: the domain name
	{
		size_t pos = 0;
		while (pos < host_name.size()) {
			size_t end = host_name.find('.', pos);
			if (end == std::string::npos) {
				end = host_name.size();
			}
			*p++ = uint8_t(end - pos); // NOLINT
			if (end > pos) {
				memcpy(p, host_name.data() + pos, end - pos); // NOLINT
				p += end - pos; // NOLINT
			}
			if (end == host_name.size()) {
				break;
			}
			pos = end + 1;
		}
		*p++ = 0; // the terminating root label // NOLINT
	}

	// the question section: the type and the class
	utki::serialize16be(type, p);
	p += 2; // NOLINT
	utki::serialize16be(1, p); // the IN class
	p += 2; // NOLINT

	// the additional section: the EDNS0 OPT record
	*p++ = 0; // the root domain name // NOLINT
	utki::serialize16be(dns_type_opt, p);
	p += 2; // NOLINT
	utki::serialize16be(edns_udp_payload_size, p); // the "class" field of the OPT record
	p += 2; // NOLINT
	utki::serialize32be(0, p); // the "TTL" field of the OPT record (extended RCODE and flags)
	p += 4; // NOLINT
	utki::serialize16be(0, p); // the RDLENGTH
	p += 2; // NOLINT

	buf.resize(size_t(p - buf.data()));
	return buf;
}

/**
 * @brief Skips a (possibly compressed) domain name in a DNS packet.
 *
 * The compression pointers are not followed.
 *
 * On success 'p' is left pointing right after the name, otherwise false is returned.
 */
bool skip_name(const uint8_t*& p, const uint8_t* end)
{
	while (p != end) {
		uint8_t len = *p++; // NOLINT
		if (len == 0) {
			return true;
		}
		if ((len & 0xc0) != 0) { // NOLINT
			// a compression pointer, it takes 2 bytes in total
			if (p == end) {
				return false; // the second byte is missing
			}
			++p; // skip the second byte of the pointer // NOLINT
			return true;
		}
		if (size_t(end - p) < len) {
			return false;
		}
		p += len; // NOLINT
	}
	return false;
}

/**
 * @brief Parses a host name from a DNS packet, following the compression pointers.
 *
 * On success returns the name and leaves 'p' pointing right after it,
 * otherwise returns an empty string.
 */
std::string parse_host_name(const uint8_t* base, const uint8_t*& p, const uint8_t* end)
{
	std::string host;
	std::array<uint16_t, 16> visited{}; // NOLINT
	unsigned visited_count = 0;
	bool need_dot = false;

	while (p != end) {
		uint8_t len = *p;
		++p; // NOLINT

		if (len == 0) {
			break;
		}

		if ((len & 0xc0) == 0xc0) { // NOLINT
			// a compression pointer
			if (p == end) {
				return "";
			}
			auto offset = uint16_t(((uint16_t(len) & 0x3f) << 8) | uint16_t(*p)); // NOLINT
			++p; // NOLINT

			if (size_t(offset) >= size_t(end - base)) {
				return "";
			}
			bool cycle = false;
			for (unsigned i = 0; i < visited_count; ++i) {
				if (visited[i] == offset) {
					cycle = true;
					break;
				}
			}
			if (cycle || visited_count == 16) { // NOLINT
				return ""; // a cyclic or too deeply nested compression pointer
			}
			visited[visited_count++] = offset;
			if (need_dot) {
				host += '.';
			}
			p = base + offset; // NOLINT
			continue;
		}

		if (size_t(end - p) < len) {
			return "";
		}
		if (need_dot) {
			host += '.';
		}
		host.append(reinterpret_cast<const char*>(p), len); // NOLINT
		need_dot = true;
		p += len; // NOLINT
	}

	return host;
}

// -----------------------------------------------------------------------------------------------
// The DNS lookup request
// -----------------------------------------------------------------------------------------------

/**
 * @brief The state of a single in-progress DNS lookup.
 *
 * A request consists of one or two "legs", each leg being a UDP query (with an optional
 * TCP fallback) for a single record type (A or AAAA). For dns_record_type::both the legs
 * are processed in parallel and the request completes with the first leg that returned
 * one or more addresses.
 */
class request
{
public:
	request() = default;
	request(const request&) = delete;
	request& operator=(const request&) = delete;
	request(request&&) = delete;
	request& operator=(request&&) = delete;
	~request() = default;

	/**
	 * @brief A single record type query of the request.
	 */
	struct leg {
		enum class tcp_phase_t {
			none,
			connecting,
			sending,
			receiving
		};

		uint16_t id = 0; // the DNS transaction ID
		bool in_flight = false; // the id is registered in the thread's id map
		uint16_t type = 0; // the DNS record type (A or AAAA)
		uint16_t server_index = 0; // the index of the current DNS server in request::servers
		bool sent = false; // a UDP query (initial or a retransmission) has been sent
		bool needs_send = false; // the UDP query of this leg has to be (re)sent
		time_point next_retransmit{}; // empty = no pending retransmission
		uint32_t retransmits = 0;
		bool done = false; // the leg is terminal, no further responses for it will be processed
		setka::dns_result done_result = setka::dns_result::error;

		// the TCP fallback state
		bool tcp_active = false; // the TCP fallback socket is registered in the wait set
		tcp_phase_t tcp_phase = tcp_phase_t::none;
		setka::tcp_socket tcp_sock;
		std::vector<uint8_t> tcp_tx; // the 2-byte length prefixed query
		size_t tcp_tx_sent = 0;
		std::vector<uint8_t> tcp_rx; // the accumulated 2-byte length prefixed response
	};

	dns_resolver* hnr = nullptr;
	std::string host_name;
	std::vector<setka::address> servers;
	setka::dns_record_type record_type = setka::dns_record_type::both;
	time_point deadline{};

	// A cache hit: the request completes immediately with this result, no query is sent.
	bool complete_immediately = false;
	setka::dns_result immediate_result = setka::dns_result::error;

	// The resolved addresses (filled in on success) and their metadata.
	std::vector<setka::address::ip> addresses;
	uint32_t ttl = 0; // the minimum TTL of the resolved records, in seconds
	bool nxdomain = false; // the DNS server reported that the domain does not exist

	std::array<leg, 2> legs;
	uint8_t num_legs = 0;
};

// -----------------------------------------------------------------------------------------------
// The DNS response parsing
// -----------------------------------------------------------------------------------------------

/**
 * @brief The result of parsing a DNS response.
 */
struct reply {
	bool matches = false; // a well-formed response to our query
	bool truncated = false; // the TC flag
	uint16_t rcode = 0;
	std::vector<setka::address::ip> addresses; // the answer records of the requested type
	uint32_t ttl = 0; // the minimum TTL among the answer records, in seconds
};

/**
 * @brief Parses a DNS response packet.
 *
 * On success (a well-formed response with a matching ID, question name and question type)
 * sets reply::matches to true and fills in the remaining fields.
 */
void parse_reply(
	const request* r,
	uint16_t expected_id,
	uint16_t expected_type,
	const uint8_t* data,
	size_t len,
	reply& out
)
{
	if (len < 12) { // NOLINT
		return;
	}

	const uint8_t* p = data;
	const uint8_t* end = data + len; // NOLINT

	// the header
	uint16_t id = utki::deserialize16be(p);
	p += 2; // NOLINT
	if (id != expected_id) {
		return;
	}
	uint16_t flags = utki::deserialize16be(p);
	p += 2; // NOLINT
	if ((flags & 0x8000) == 0) { // the QR flag must be set // NOLINT
		return;
	}
	out.truncated = (flags & 0x0200) != 0; // NOLINT
	out.rcode = flags & 0x000f; // NOLINT
	uint16_t qdcount = utki::deserialize16be(p);
	p += 2; // NOLINT
	uint16_t ancount = utki::deserialize16be(p);
	p += 2; // NOLINT
	uint16_t nscount = utki::deserialize16be(p);
	p += 2; // NOLINT
	uint16_t arcount = utki::deserialize16be(p);
	p += 2; // NOLINT

	// the question section
	if (qdcount != 1) {
		return;
	}
	std::string host = parse_host_name(data, p, end);
	if (host != r->host_name) {
		return;
	}
	if (end - p < 4) {
		return;
	}
	uint16_t qtype = utki::deserialize16be(p);
	p += 2; // NOLINT
	uint16_t qclass = utki::deserialize16be(p);
	p += 2; // NOLINT
	if (qtype != expected_type || qclass != 1) {
		return;
	}

	// the answer section
	for (uint16_t i = 0; i < ancount; ++i) {
		if (!skip_name(p, end)) {
			return;
		}
		if (end - p < 10) { // type(2) + class(2) + ttl(4) + rdlength(2) // NOLINT
			return;
		}
		uint16_t type = utki::deserialize16be(p);
		p += 2; // NOLINT
		p += 2; // the class // NOLINT
		uint32_t ttl = utki::deserialize32be(p);
		p += 4; // NOLINT
		uint16_t rdlength = utki::deserialize16be(p);
		p += 2; // NOLINT
		if (end - p < rdlength) {
			return;
		}
		if (type == expected_type) {
			if (type == dns_type_a) {
				if (rdlength < 4) {
					return;
				}
				out.addresses.emplace_back(utki::deserialize32be(p)); // NOLINT
			} else { // dns_type_aaaa
				if (rdlength < 16) { // NOLINT
					return;
				}
				out.addresses.emplace_back( // NOLINT
					utki::deserialize32be(p),
					utki::deserialize32be(p + 4), // NOLINT
					utki::deserialize32be(p + 8), // NOLINT
					utki::deserialize32be(p + 12) // NOLINT
				);
			}
			if (out.ttl == 0 || ttl < out.ttl) {
				out.ttl = ttl;
			}
		}
		p += rdlength; // NOLINT
	}

	// the authority and the additional sections are not of interest to us
	(void)nscount;
	(void)arcount;

	out.matches = true;
}

// -----------------------------------------------------------------------------------------------
// The background DNS lookup thread
// -----------------------------------------------------------------------------------------------

/**
 * @brief The background thread performing the DNS lookups.
 *
 * The thread is persistent: once started it stays alive until the library is de-initialized,
 * serving lookups from any number of resolver objects.
 */
class lookup_thread : public nitki::thread
{
	setka::udp_socket udp_sock;
	opros::wait_set wait_set{wait_set_capacity};
	nitki::queue queue;

public:
	lookup_thread() = default;
	lookup_thread(const lookup_thread&) = delete;
	lookup_thread& operator=(const lookup_thread&) = delete;
	lookup_thread(lookup_thread&&) = delete;
	lookup_thread& operator=(lookup_thread&&) = delete;
	~lookup_thread() override = default;

	// The previous (exited) thread object that this thread should join and destroy,
	// set when this thread replaces a dead (fatal) predecessor.
	std::unique_ptr<lookup_thread> prev_thread;

	// NOTE: all the thread-owned state below must be accessed with the mutex held,
	//       the atomics are the exception.

	std::mutex mutex;

	// Set to make the thread exit.
	std::atomic<bool> quit_flag = false;
	// Set by the resolver when the thread has been started, so that the thread is started
	// only once.
	std::atomic<bool> started = false;
	// Set if the thread encountered a fatal error (e.g. could not open the UDP socket) and
	// has exited or is about to exit.
	std::atomic<bool> fatal = false;

	// Serializes the completion callbacks. Only the background thread ever takes this lock.
	std::mutex completed_mutex;

	// The resolver objects that have been deregistered (cancelled or destroyed) from within
	// a completion callback in the current batch: their already queued (but not yet invoked)
	// completions must be skipped, the objects may be destroyed before the completions
	// would be invoked. Guarded by skip_mutex, which is never held while the user code runs.
	std::mutex skip_mutex;
	std::set<dns_resolver*> skip;

	// Wakes up the background thread.
	void wake_up()
	{
		this->queue.push_back([]() {});
	}

	// The DNS transaction ID allocator.
	std::bitset<65536> ids_used; // NOLINT
	uint16_t next_id_hint = 0;

	// The requests awaiting the initial UDP queries to be sent.
	std::deque<request*> send_queue;

	// All the in-progress requests, keyed by the resolver object.
	std::map<dns_resolver*, std::unique_ptr<request>> requests;

	// The in-progress requests, keyed by the DNS transaction ID.
	std::map<uint16_t, request*> ids;

	// The request deadlines, keyed by the absolute (monotonic) deadline.
	std::multimap<time_point, request*> deadlines;

	// The legs with the active TCP fallback connections, keyed by the leg.
	std::map<request::leg*, request*> tcp_legs;
	uint32_t tcp_fallback_count = 0;

	bool udp_write_interest = false;

	// A request that finished in the current loop iteration, the completion callbacks are
	// invoked for these outside of the mutex protection.
	struct completion {
		std::unique_ptr<request> req;
		setka::dns_result result;
	};

	// ---------------------------------------------------------------------------
	// The transaction ID management
	// ---------------------------------------------------------------------------

	uint16_t take_id()
	{
		if (this->ids_used.count() == this->ids_used.size()) {
			throw dns_resolver::too_many_requests();
		}
		for (size_t i = 0; i < this->ids_used.size(); ++i) {
			auto id = uint16_t((this->next_id_hint + i) % this->ids_used.size()); // NOLINT
			if (!this->ids_used.test(id)) {
				this->ids_used.set(id);
				this->next_id_hint = uint16_t((id + 1) % this->ids_used.size());
				return id;
			}
		}
		throw dns_resolver::too_many_requests(); // unreachable
	}

	void release_id(uint16_t id)
	{
		this->ids_used.reset(id);
	}

	// ---------------------------------------------------------------------------
	// The request removal
	// ---------------------------------------------------------------------------

	// Stops the leg's TCP fallback connection (if any), closing the socket.
	// NOTE: must be called with the leg registered in the wait set (leg->tcp_active is true).
	void stop_tcp_leg(request::leg* leg)
	{
		this->wait_set.remove(leg->tcp_sock);
		this->tcp_legs.erase(leg);
		leg->tcp_sock.disconnect();
		leg->tcp_active = false;
		leg->tcp_phase = request::leg::tcp_phase_t::none;
		--this->tcp_fallback_count;
	}

	// Removes the request from all the thread's data structures without invoking the
	// completion callback and returns it.
	std::unique_ptr<request> detach_request(request* req)
	{
		// from the deadline map (several requests can share the same deadline)
		auto dr = this->deadlines.equal_range(req->deadline);
		for (auto di = dr.first; di != dr.second; ++di) {
			if (di->second == req) {
				this->deadlines.erase(di);
				break;
			}
		}

		// from the id map, close the TCP fallbacks
		for (unsigned l = 0; l < req->num_legs; ++l) {
			auto* leg = &req->legs[l];
			if (leg->in_flight) {
				this->ids.erase(leg->id);
				this->release_id(leg->id);
				leg->in_flight = false;
			}
			if (leg->tcp_active) {
				this->stop_tcp_leg(leg);
			}
		}

		// from the send queue
		for (auto it = this->send_queue.begin(); it != this->send_queue.end();) {
			if (*it == req) {
				it = this->send_queue.erase(it);
			} else {
				++it;
			}
		}

		// out of the requests map
		auto ri = this->requests.find(req->hnr);
		std::unique_ptr<request> removed = std::move(ri->second);
		this->requests.erase(ri);

		return removed;
	}

	// Removes the request (if any) from all the thread's data structures without invoking
	// the completion callback and returns it. Returns nullptr if there was no request.
	std::unique_ptr<request> remove_request(dns_resolver* hnr)
	{
		auto ri = this->requests.find(hnr);
		if (ri == this->requests.end()) {
			return nullptr;
		}
		return this->detach_request(ri->second.operator->());
	}

	// Removes the request from all the thread's data structures and schedules its
	// completion, the completion callback is invoked outside of the mutex protection.
	// NOTE: the caller must hold this->mutex.
	void complete_request(request* req, setka::dns_result result, std::vector<completion>& completed)
	{
		auto removed = this->detach_request(req);
		// NOTE: the designated initializers are not used because the C++17 standard is required by some of the CI builds
		completed.push_back(completion{std::move(removed), result}); // NOLINT
		{
			std::lock_guard p_lock(dns::pending_mutex);
			dns::pending.insert(req->hnr);
		}
	}

	// Fails all the in-progress requests with the error result.
	void fail_all(std::vector<completion>& completed)
	{
		std::vector<request*> reqs;
		for (auto& kv : this->requests) {
			reqs.push_back(kv.second.operator->());
		}
		for (auto* req : reqs) {
			this->complete_request(req, setka::dns_result::error, completed);
		}
	}

	// Drops all the in-progress requests without scheduling their completions. Used when the
	// thread is shutting down because the library is being de-initialized: by that time all
	// the resolver objects are destroyed, invoking their completion callbacks would be a
	// use-after-free. NOTE: the caller must hold this->mutex.
	void drop_all()
	{
		std::vector<request*> reqs;
		for (auto& kv : this->requests) {
			reqs.push_back(kv.second.operator->());
		}
		for (auto* req : reqs) {
			this->detach_request(req);
		}
	}

	// ---------------------------------------------------------------------------
	// The UDP send and the retransmissions
	// ---------------------------------------------------------------------------

	void update_udp_write_interest()
	{
		bool need_write = this->send_queue.size() > 0;
		if (need_write != this->udp_write_interest) {
			this->udp_write_interest = need_write;
			this->wait_set.change(
				this->udp_sock,
				need_write ? utki::make_flags({opros::ready::read, opros::ready::write})
						   : utki::make_flags({opros::ready::read}),
				&this->udp_sock
			);
		}
	}

	// Sends the initial UDP queries of the requests in the send queue.
	void send_pending()
	{
		while (this->send_queue.size() > 0) {
			request* req = this->send_queue.front();
			bool all_sent = true;

			for (unsigned l = 0; l < req->num_legs; ++l) {
				auto* leg = &req->legs[l];
				if (!leg->needs_send || leg->tcp_active || leg->done) {
					continue;
				}

				// the query
				auto qbuf = build_query(leg->id, req->host_name, leg->type);

				setka::address server = req->servers[leg->server_index % req->servers.size()];
				// The servers loaded from the OS have the default port set, the user-supplied
				// server may have a custom port, default to 53 only when no port is set.
				if (server.port == 0) {
					server.port = dns_port;
				}

				size_t ret = this->udp_sock.send(qbuf, server);
				if (ret == qbuf.size()) {
					leg->sent = true;
					leg->needs_send = false;
				} else {
					// can't send right now, will retry later
					all_sent = false;
					break;
				}
			}

			if (all_sent) {
				this->send_queue.pop_front();
			} else {
				break;
			}
		}

		this->update_udp_write_interest();
	}

	// Retransmits the queries whose retransmission time has come.
	void process_retransmissions(time_point now)
	{
		for (auto& kv : this->requests) {
			request* req = kv.second.operator->();
			if (req->complete_immediately) {
				continue;
			}
			for (unsigned l = 0; l < req->num_legs; ++l) {
				auto* leg = &req->legs[l];
				if (leg->sent && !leg->done && !leg->tcp_active && leg->next_retransmit != time_point{} &&
					leg->next_retransmit <= now)
				{
					++leg->retransmits;
					leg->needs_send = true; // retransmit the query
					// After the first retransmission (which retries the same server) alternate
					// between the configured DNS servers.
					if (req->servers.size() > 1 && (leg->retransmits % 2) == 1) {
						leg->server_index = uint16_t((leg->server_index + 1) % req->servers.size());
					}

					// Schedule the next retransmission with the exponential backoff.
					uint32_t interval = initial_retransmit_interval_ms;
					for (unsigned i = 0; i < leg->retransmits && interval < max_retransmit_interval_ms; ++i) {
						interval *= 2;
					}
					if (interval > max_retransmit_interval_ms) {
						interval = max_retransmit_interval_ms;
					}
					time_point next = now + milliseconds(interval);
					leg->next_retransmit = next < req->deadline ? next : time_point{};

					this->send_queue.push_back(req);
				}
			}
		}
	}

	// ---------------------------------------------------------------------------
	// The response processing
	// ---------------------------------------------------------------------------

	// Checks whether all the legs of the request are terminal, and if so completes the
	// request with the aggregated result.
	void check_all_legs_done(request* req, std::vector<completion>& completed)
	{
		for (unsigned l = 0; l < req->num_legs; ++l) {
			if (!req->legs[l].done) {
				return; // still waiting for the other leg
			}
		}
		bool any_not_found = false;
		for (unsigned l = 0; l < req->num_legs; ++l) {
			if (req->legs[l].done_result == setka::dns_result::not_found) {
				any_not_found = true;
			}
		}
		this->complete_request(
			req,
			any_not_found ? setka::dns_result::not_found : setka::dns_result::dns_error,
			completed
		);
	}

	// Marks the leg as terminal and completes the request if all the legs are done.
	void finish_leg(request* req, request::leg* leg, setka::dns_result result, std::vector<completion>& completed)
	{
		leg->done = true;
		leg->done_result = result;
		this->check_all_legs_done(req, completed);
	}

	// Starts the TCP fallback for a truncated UDP response.
	void start_tcp_fallback(request* req, request::leg* leg, std::vector<completion>& completed)
	{
		if (this->tcp_fallback_count >= max_tcp_fallbacks) {
			// Too many concurrent TCP fallbacks, this leg will just fail.
			this->finish_leg(req, leg, setka::dns_result::dns_error, completed);
			return;
		}

		setka::address server = req->servers[leg->server_index % req->servers.size()];
		// Same as in send_pending(): default to the standard DNS port only when unset.
		if (server.port == 0) {
			server.port = dns_port;
		}

		try {
			leg->tcp_sock = setka::tcp_socket(server);
		} catch (std::exception&) {
			// An immediate connection failure (e.g. no route to host).
			this->finish_leg(req, leg, setka::dns_result::dns_error, completed);
			return;
		}

		// the query, the maximum size is 534 bytes, see build_query()
		auto qbuf = build_query(leg->id, req->host_name, leg->type);
		leg->tcp_tx.resize(qbuf.size() + 2);
		utki::serialize16be(uint16_t(qbuf.size()), leg->tcp_tx.data());
		memcpy(leg->tcp_tx.data() + 2, qbuf.data(), qbuf.size()); // NOLINT
		leg->tcp_tx_sent = 0;
		leg->tcp_rx.clear();
		leg->tcp_phase = request::leg::tcp_phase_t::connecting;

		try {
			this->wait_set.add(leg->tcp_sock, utki::make_flags({opros::ready::read, opros::ready::write}), leg);
		} catch (std::exception&) {
			// Should not happen (the wait set has enough capacity), but handle it anyway.
			leg->tcp_sock.disconnect();
			leg->tcp_phase = request::leg::tcp_phase_t::none;
			this->finish_leg(req, leg, setka::dns_result::dns_error, completed);
			return;
		}

		leg->tcp_active = true; // from this point on the socket is registered in the wait set
		++this->tcp_fallback_count;
		this->tcp_legs[leg] = req;
	}

	// Processes a parsed DNS response (received over UDP or TCP) for the leg.
	void process_reply(request* req, request::leg* leg, reply& rep, std::vector<completion>& completed)
	{
		// The TCP fallback for this leg is no longer needed, we got a complete response.
		if (leg->tcp_active) {
			this->stop_tcp_leg(leg);
		}

		if (rep.rcode == 3) {
			// NXDOMAIN: the domain does not exist, so there cannot be any records of
			// any other type either.
			req->nxdomain = true;
			this->complete_request(req, setka::dns_result::not_found, completed);
			return;
		}

		if (rep.addresses.size() > 0) {
			req->addresses = std::move(rep.addresses);
			req->ttl = rep.ttl;
			this->complete_request(req, setka::dns_result::ok, completed);
			return;
		}

		if (rep.rcode == 0) {
			if (rep.truncated) {
				// The response was truncated, get the full response over TCP.
				this->start_tcp_fallback(req, leg, completed);
				return;
			}
			// NOERROR without the answers of the requested type: this leg is done.
			this->finish_leg(req, leg, setka::dns_result::not_found, completed);
			return;
		}

		// Some other error reported by the DNS server (e.g. SERVFAIL).
		this->finish_leg(req, leg, setka::dns_result::dns_error, completed);
	}

	// Processes a received UDP datagram.
	void handle_datagram(const uint8_t* data, size_t len, std::vector<completion>& completed)
	{
		if (len < 12) { // NOLINT
			return;
		}
		uint16_t id = utki::deserialize16be(data);
		auto ii = this->ids.find(id);
		if (ii == this->ids.end()) {
			return; // not our query (or it was already completed)
		}
		request* req = ii->second;
		request::leg* leg = nullptr;
		for (unsigned l = 0; l < req->num_legs; ++l) {
			if (req->legs[l].in_flight && req->legs[l].id == id) {
				leg = &req->legs[l];
				break;
			}
		}
		if (!leg) {
			return;
		}

		reply rep;
		parse_reply(req, id, leg->type, data, len, rep);
		if (!rep.matches) {
			return; // malformed or a mismatched response, ignore it
		}

		this->process_reply(req, leg, rep, completed);
	}

	// ---------------------------------------------------------------------------
	// The TCP fallback state machine
	// ---------------------------------------------------------------------------

	// Processes a state machine event for the leg's TCP fallback connection.
	void process_tcp_event(
		request* req,
		request::leg* leg,
		utki::flags<opros::ready> flags,
		std::vector<completion>& completed
	)
	{
		auto fail_tcp = [this, req, leg, &completed]() {
			if (leg->tcp_active) {
				this->stop_tcp_leg(leg);
			}
			this->finish_leg(req, leg, setka::dns_result::dns_error, completed);
		};

		if (flags.get(opros::ready::error)) {
			fail_tcp();
			return;
		}

		if (leg->tcp_phase == request::leg::tcp_phase_t::connecting) {
			if (flags.get(opros::ready::read)) {
				// A readable socket while connecting means a reset or a closed connection.
				fail_tcp();
				return;
			}
			if (flags.get(opros::ready::write)) {
				// The connection is (probably) established, try to send the query.
				// If the connection failed, the send will fail and we will find out then.
				leg->tcp_phase = request::leg::tcp_phase_t::sending;
			}
		}

		if (leg->tcp_phase == request::leg::tcp_phase_t::sending) {
			if (flags.get(opros::ready::write)) {
				try {
					size_t n = leg->tcp_sock.send(utki::make_span(
						leg->tcp_tx.data() + leg->tcp_tx_sent, // NOLINT
						leg->tcp_tx.size() - leg->tcp_tx_sent
					) // NOLINT
					);
					if (n > 0) {
						leg->tcp_tx_sent += n;
						if (leg->tcp_tx_sent == leg->tcp_tx.size()) {
							leg->tcp_phase = request::leg::tcp_phase_t::receiving;
						}
					} else {
						// The write was signalled as ready but nothing could be sent,
						// the connection has been disconnected.
						fail_tcp();
						return;
					}
				} catch (std::exception&) {
					fail_tcp();
					return;
				}
			}
			if (!leg->tcp_active) {
				return; // the request was completed
			}
		}

		if (leg->tcp_phase == request::leg::tcp_phase_t::receiving) {
			if (flags.get(opros::ready::read)) {
				std::array<uint8_t, 4096> buf; // NOLINT
				size_t n = 0;
				try {
					n = leg->tcp_sock.receive(utki::make_span(buf.data(), buf.size()));
				} catch (std::exception&) {
					fail_tcp();
					return;
				}
				if (n > 0) {
					leg->tcp_rx.insert(leg->tcp_rx.end(), buf.data(), buf.data() + n);
					this->process_tcp_rx(req, leg, completed);
					if (!leg->tcp_active) {
						return; // the request was completed
					}
				} else {
					// The read was signalled as ready but there was nothing to read: the
					// connection was closed by the peer without the full response, fail.
					fail_tcp();
					return;
				}
			}
		}
	}

	// Processes the accumulated TCP response buffer, extracting the complete
	// 2-byte length prefixed DNS messages.
	void process_tcp_rx(request* req, request::leg* leg, std::vector<completion>& completed)
	{
		// A DNS message over TCP is at most 65535 bytes, if the buffer exceeds that then
		// something is wrong with the connection.
		if (leg->tcp_rx.size() > 65535u + 2u) { // NOLINT
			if (leg->tcp_active) {
				this->stop_tcp_leg(leg);
			}
			this->finish_leg(req, leg, setka::dns_result::dns_error, completed);
			return;
		}

		while (true) {
			if (leg->tcp_rx.size() < 2) {
				break;
			}
			uint16_t msg_len = utki::deserialize16be(leg->tcp_rx.data());
			if (leg->tcp_rx.size() < size_t(2) + msg_len) {
				break;
			}

			reply rep;
			parse_reply(req, leg->id, leg->type, leg->tcp_rx.data() + 2, msg_len, rep); // NOLINT
			// Consume the message (it might not match our query, in which case we just
			// skip it).
			leg->tcp_rx.erase(leg->tcp_rx.begin(), leg->tcp_rx.begin() + size_t(2) + msg_len);
			if (rep.matches) {
				this->process_reply(req, leg, rep, completed);
				return;
			}
		}
	}

	// Processes a triggered event for one of the TCP fallback sockets.
	void process_tcp_event(const opros::event_info& e, std::vector<completion>& completed)
	{
		auto it = this->tcp_legs.find(static_cast<request::leg*>(e.user_data));
		if (it == this->tcp_legs.end()) {
			return;
		}
		this->process_tcp_event(it->second, it->first, e.flags, completed);
	}

	// ---------------------------------------------------------------------------
	// The completion callbacks
	// ---------------------------------------------------------------------------

	// Stores the result of the completed request into the DNS result cache (if applicable).
	void cache_store(const request* req, setka::dns_result result)
	{
		if (result != setka::dns_result::ok && result != setka::dns_result::not_found) {
			return;
		}

		uint16_t cache_type = 0;
		switch (req->record_type) {
			case setka::dns_record_type::ipv4:
				cache_type = dns_type_a;
				break;
			case setka::dns_record_type::ipv6:
				cache_type = dns_type_aaaa;
				break;
			default:
				cache_type = 0; // setka::dns_record_type::both
				break;
		}

		auto now = clock_type::now();
		{
			std::lock_guard lock(cache_mutex);
			if (result == setka::dns_result::ok) {
				if (req->ttl == 0) {
					return; // don't cache the records with the zero TTL
				}
				uint64_t ttl_ms = uint64_t(req->ttl) * 1000; // NOLINT
				if (ttl_ms > max_positive_cache_ttl_ms) {
					ttl_ms = max_positive_cache_ttl_ms;
				}
				dns_cache::entry e;
				e.addresses = req->addresses;
				e.expiry = now + milliseconds(ttl_ms);
				cache[cache_key{req->host_name, cache_type}] = std::move(e);
			} else { // not_found
				dns_cache::entry e;
				e.not_found = true;
				e.expiry = now + milliseconds(negative_cache_ttl_ms);
				cache[cache_key{req->host_name, cache_type}] = e;
				if (req->nxdomain) {
					// NXDOMAIN: the domain does not exist, so no records of any type exist.
					cache[cache_key{req->host_name, dns_type_a}] = e;
					cache[cache_key{req->host_name, dns_type_aaaa}] = e;
				}
			}
			// Keep the cache size bounded.
			while (cache.size() > max_cache_entries) {
				cache.erase(cache.begin());
			}
		}
	}

	// Invokes the completion callback for the completed request.
	void call_callback(const completion& c)
	{
		this->cache_store(c.req.operator->(), c.result);

		bool skipped = false;
		{
			std::lock_guard s_lock(this->skip_mutex);
			if (this->skip.count(c.req->hnr)) {
				// The resolver was deregistered from within a completion callback,
				// the object may be destroyed, skip the completion.
				this->skip.erase(c.req->hnr);
				skipped = true;
			}
		}

		if (!skipped) {
			std::lock_guard c_lock(this->completed_mutex);
			in_dns_callback = true;
			try {
				c.req->hnr->on_completed(c.result, utki::make_span(c.req->addresses));
			} catch (...) { // NOLINT
				// swallow the exceptions thrown by the user code
			}
			in_dns_callback = false;
		}

		{
			std::lock_guard p_lock(dns::pending_mutex);
			dns::pending.erase(c.req->hnr);
		}
		dns::pending_cv.notify_all();
	}

	// Invokes the completion callbacks for the given completed requests.
	// NOTE: must be called without the mutex held.
	void invoke_completions(std::vector<completion>& completed)
	{
		for (auto& c : completed) {
			this->call_callback(c);
		}
		{
			std::lock_guard s_lock(this->skip_mutex);
			this->skip.clear();
		}
		completed.clear();
	}

	// ---------------------------------------------------------------------------
	// The thread main loop
	// ---------------------------------------------------------------------------

	void run() override
	{
		// Destroy the previous (exited) thread object if there is one. The predecessor
		// has finished its run() by the time this thread is started, so the join is
		// guaranteed to finish.
		if (this->prev_thread) {
			this->prev_thread->join();
			this->prev_thread.reset();
		}

		std::vector<completion> completed;
		bool waitables_added = false;

		{
			std::lock_guard lock(this->mutex);

			// Open the UDP socket.
			try {
				this->udp_sock = setka::udp_socket(0);
			} catch (std::exception& e) {
				utki::log([&](auto& o) {
					o << "dns_resolver: could not open the UDP socket: " << e.what() << std::endl;
				});
				this->fatal = true;
				this->fail_all(completed);
			}

			if (!this->fatal) {
				this->wait_set.add(this->queue, utki::make_flags({opros::ready::read}), &this->queue);
				this->wait_set.add(this->udp_sock, utki::make_flags({opros::ready::read}), &this->udp_sock);
				this->udp_write_interest = this->send_queue.size() > 0;
				this->update_udp_write_interest();
				waitables_added = true;
			}

			LOG([&](auto& o) {
				o << "DNS lookup thread started" << std::endl;
			})
		}

		while (!this->quit_flag && !this->fatal) {
			uint32_t wait_ms = 0;
			bool infinite_wait = false;

			{
				std::lock_guard lock(this->mutex);

				time_point now = clock_type::now();

				// Process the triggered events.
				for (const opros::event_info& e : this->wait_set.get_triggered()) {
					if (e.user_data == &this->udp_sock) {
						if (e.flags.get(opros::ready::error)) {
							utki::log([&](auto& o) {
								o << "dns_resolver: error on the DNS UDP socket, failing all the pending requests"
								  << std::endl;
							});
							this->fatal = true;
							this->fail_all(completed);
							break;
						}
						if (e.flags.get(opros::ready::read)) {
							std::array<uint8_t, udp_response_buffer_size> buf; // NOLINT
							while (true) {
								auto [n, from] = this->udp_sock.receive(utki::make_span(buf.data(), buf.size()));
								if (n == 0) {
									break;
								}
								(void)from; // the sender is not checked, the requests are matched by the query ids
								this->handle_datagram(buf.data(), n, completed);
							}
						}
					} else if (e.user_data == &this->queue) {
						// just a wake-up signal
					} else {
						this->process_tcp_event(e, completed);
					}
				}

				if (!this->fatal) {
					// Process the expired deadlines.
					while (this->deadlines.size() > 0 && this->deadlines.begin()->first <= now) {
						request* req = this->deadlines.begin()->second;
						this->complete_request(req, setka::dns_result::timeout, completed);
					}

					// Process the retransmissions whose time has come, they are queued into
					// the send queue.
					this->process_retransmissions(now);

					// Send the pending UDP queries (the initial ones and the retransmissions).
					this->send_pending();

					// Process the immediate completions (the cache hits).
					{
						std::vector<request*> reqs;
						for (auto& kv : this->requests) {
							if (kv.second->complete_immediately) {
								reqs.push_back(kv.second.operator->());
							}
						}
						for (auto* req : reqs) {
							this->complete_request(req, req->immediate_result, completed);
						}
					}

					// Compute the wait timeout.
					std::optional<time_point> next_time;
					if (this->deadlines.size() > 0) {
						next_time = this->deadlines.begin()->first;
					}
					for (auto& kv : this->requests) {
						request* req = kv.second.operator->();
						for (unsigned l = 0; l < req->num_legs; ++l) {
							auto* leg = &req->legs[l];
							if (leg->sent && !leg->done && !leg->tcp_active && leg->next_retransmit != time_point{}) {
								if (!next_time || *next_time > leg->next_retransmit) {
									next_time = leg->next_retransmit;
								}
							}
						}
					}
					if (next_time) {
						auto ms = std::chrono::duration_cast<milliseconds>(*next_time - now).count();
						if (ms > 0) {
							if (ms < uint32_t(-1)) {
								wait_ms = uint32_t(ms);
							} else {
								infinite_wait = true;
							}
						}
					} else {
						infinite_wait = true;
					}
					// If there are requests that couldn't be sent right now, don't wait long,
					// they are waiting for the UDP socket to become writable.
					if (this->send_queue.size() > 0) {
						infinite_wait = false;
						if (wait_ms > 100) { // NOLINT
							wait_ms = 100; // NOLINT
						}
					}
				}
			}

			if (this->fatal) {
				break;
			}

			// Invoke the completion callbacks for the requests completed in this
			// iteration, outside of the mutex protection.
			this->invoke_completions(completed);

			// Wait for the events.
			if (infinite_wait) {
				this->wait_set.wait();
			} else {
				this->wait_set.wait(wait_ms);
			}

			// Process the wake-up queue.
			while (auto proc = this->queue.pop_front()) {
				proc();
			}
		}

		{
			std::lock_guard lock(this->mutex);

			// Drop the in-progress requests (if any) without invoking their completion
			// callbacks: the thread is shutting down because the library is being
			// de-initialized, by that time all the resolver objects are destroyed, so
			// invoking their completion callbacks would be a use-after-free.
			this->drop_all();

			// Remove the waitables from the wait set.
			if (waitables_added) {
				this->wait_set.remove(this->udp_sock);
				this->wait_set.remove(this->queue);
			}
		}

		LOG([&](auto& o) {
			o << "DNS lookup thread stopped" << std::endl;
		})

		// Invoke the remaining completion callbacks (the fail-all ones).
		this->invoke_completions(completed);
	}
};

// -----------------------------------------------------------------------------------------------
// The OS-specific helpers
// -----------------------------------------------------------------------------------------------

// Parses a DNS server address from a string, returns true on success.
bool parse_dns_server_address(const std::string& str, setka::address& out)
{
	try {
		out = setka::address(str.c_str(), 0);
	} catch (std::exception&) {
		return false;
	}
	return out.host.is_valid();
}

// Adds the DNS server to the servers list if it is valid and not already present.
void add_dns_server(const std::string& str)
{
	setka::address a;
	if (!parse_dns_server_address(str, a)) {
		return;
	}
	for (auto& existing : dns_servers) {
		if (existing.host == a.host) {
			return;
		}
	}
	dns_servers.push_back(a);
}

// Loads the DNS servers from the OS configuration.
// NOTE: call with the dns_mutex held.
void load_dns_servers()
{
#if CFG_OS == CFG_OS_WINDOWS
	// Get the DNS servers from the registry: the interfaces' NameServer and
	// DhcpNameServer values.

	struct win_reg_key {
		HKEY key = nullptr;

		win_reg_key()
		{
			if (RegOpenKeyA(
					HKEY_LOCAL_MACHINE,
					R"(SYSTEM\ControlSet001\Services\Tcpip\Parameters\Interfaces)",
					&this->key
				) != ERROR_SUCCESS)
			{
				throw std::runtime_error("load_dns_servers(): RegOpenKey() failed");
			}
		}

		win_reg_key(const win_reg_key&) = delete;
		win_reg_key& operator=(const win_reg_key&) = delete;
		win_reg_key(win_reg_key&&) = delete;
		win_reg_key& operator=(win_reg_key&&) = delete;

		~win_reg_key()
		{
			RegCloseKey(this->key);
		}
	};

	try {
		win_reg_key key;

		// According to MSDN docs maximum key name length is 255 chars.
		// Add also space for terminating zero.
		// NOLINT(cppcoreguidelines-pro-type-member-init)
		std::array<char, std::numeric_limits<uint8_t>::max() + 1> subkey;

		// TODO: rewrite using RegEnumKeyEx() as RegEnumKeyA() is only provided for compatibility with 16-bit Windows
		for (unsigned i = 0; RegEnumKeyA(
								 key.key, //
								 i,
								 subkey.data(),
								 DWORD(subkey.size())
							 ) == ERROR_SUCCESS;
			 ++i)
		{
			HKEY h_sub = nullptr;
			// TODO: rewrite using RegOpenKeyEx() as RegOpenKeyA() is only provided for compatibility with 16-bit Windows
			if (RegOpenKeyA(
					key.key, //
					subkey.data(),
					&h_sub
				) != ERROR_SUCCESS)
			{
				continue;
			}

			for (const char* value_name : {"NameServer", "DhcpNameServer"}) {
				// NOLINT(cppcoreguidelines-pro-type-member-init)
				std::array<BYTE, utki::kilobyte> value;
				auto len = DWORD(value.size());
				DWORD type = 0;

				if (RegQueryValueExA(
						h_sub, //
						value_name,
						nullptr,
						&type,
						value.data(),
						&len
					) != ERROR_SUCCESS)
				{
					continue;
				}

				// Both the values can be REG_SZ or REG_MULTI_SZ.
				if (type == REG_SZ || type == REG_MULTI_SZ) {
					// The values are a sequence of NUL terminated strings (a single string
					// for REG_SZ).
					size_t start = 0;
					while (start < len) {
						size_t end = start;
						while (end < len && value[end] != '\0') {
							++end;
						}
						if (end > start) {
							std::string str(reinterpret_cast<const char*>(value.data() + start), end - start);
							// Trim the whitespace.
							size_t b = str.find_first_not_of(" \t");
							if (b != std::string::npos) {
								add_dns_server(str.substr(b));
							}
						}
						if (type == REG_SZ || end >= len) {
							break;
						}
						start = end + 1;
					}
				}
			}

			RegCloseKey(h_sub);
		}
	} catch (std::exception&) {
		// ignore the error
	}

	if (dns_servers.empty()) {
		dns_servers_error = "no DNS servers found in the Windows registry";
	}

#elif CFG_OS == CFG_OS_LINUX || CFG_OS == CFG_OS_MACOSX || CFG_OS == CFG_OS_UNIX
	// Get the DNS servers from /etc/resolv.conf.
	try {
		fsif::native_file f("/etc/resolv.conf");
		std::vector<uint8_t> buf = f.load(size_t(utki::kilobyte) * 4); // 4kb max

		for (const uint8_t* p = buf.data(); p < utki::end_pointer(buf);) {
			const uint8_t* start = p;
			while (p < utki::end_pointer(buf) && *p != '\n') {
				++p; // NOLINT
			}
			std::string line(reinterpret_cast<const char*>(start), size_t(p - start)); // NOLINT
			if (p < utki::end_pointer(buf)) {
				++p; // NOLINT
			}

			// Trim the leading whitespace.
			size_t b = line.find_first_not_of(" \t\r");
			if (b == std::string::npos) {
				continue;
			}
			line = line.substr(b);
			// NOTE: the starts_with() is not used because the C++17 standard is required by some of the CI builds
			if (line.find("nameserver") != 0) { // NOLINT
				continue;
			}

			// Extract the IP address (the first whitespace separated token).
			std::string ipstr = line.substr(10); // NOLINT
			size_t tok_start = ipstr.find_first_not_of(" \t\r");
			if (tok_start == std::string::npos) {
				continue;
			}
			size_t tok_end = ipstr.find_first_of(" \t\r", tok_start);
			ipstr = ipstr.substr(tok_start, tok_end == std::string::npos ? std::string::npos : tok_end - tok_start);
			add_dns_server(ipstr);
		}
	} catch (std::exception& e) {
		dns_servers_error = std::string("could not read /etc/resolv.conf: ") + e.what();
	}

	if (dns_servers.empty() && dns_servers_error.empty()) {
		dns_servers_error = "no DNS servers found in /etc/resolv.conf";
	}
#else
	dns_servers_error = "don't know how to get the DNS servers on this OS";
#endif
}

} // namespace dns

} // namespace

// -----------------------------------------------------------------------------------------------
// The dns_resolver implementation
// -----------------------------------------------------------------------------------------------

void dns_resolver::set_completed_handler(completed_handler_type handler)
{
	std::lock_guard<std::mutex> guard(this->completed_handler_mutex);
	this->completed_handler = std::move(handler);
}

dns_resolver::~dns_resolver()
{
	this->deregister();
}

bool dns_resolver::deregister() noexcept
{
	dns::lookup_thread* t = nullptr;
	std::unique_ptr<dns::request> removed;
	{
		std::lock_guard dns_lock(dns::dns_mutex);

		if (!dns::thread) {
			return false;
		}
		t = dns::thread.get();

		std::lock_guard t_lock(t->mutex);
		removed = t->remove_request(this);
	}
	// The dns_mutex is intentionally released before the wait below, so that this method
	// cannot deadlock with a completion callback (invoked on the background thread) that
	// calls resolve(), which takes the dns_mutex.

	if (in_dns_callback) {
		// We are inside a completion callback invoked by the background thread. Any of this
		// object's completions that are already queued in the current batch must be skipped,
		// the object may be destroyed before they would be invoked. The object's in-progress
		// request (if any) was removed above, so no new completions can be generated for it.
		std::lock_guard s_lock(t->skip_mutex);
		t->skip.insert(this);
	} else {
		// Wait for the completion callback (if any) to finish, so that after this method
		// returns the on_completed() callback is not invoked on this (possibly destroyed)
		// object. The wait is a no-op if there is no outstanding completion for this object.
		dns::wait_no_pending(this);
	}

	return bool(removed);
}

bool dns_resolver::cancel() noexcept
{
	return this->deregister();
}

void dns_resolver::resolve(
	const std::string& host_name,
	uint32_t timeout_ms,
	dns_record_type record_type,
	const setka::address& dns_ip
)
{
	constexpr auto max_host_name_size = 253;

	if (host_name.size() > max_host_name_size) {
		throw std::logic_error("Too long domain name, it should not exceed 253 characters according to RFC 2181");
	}

	// Validate the domain name: no empty labels, no labels longer than 255 characters.
	{
		size_t pos = 0;
		while (pos < host_name.size()) {
			size_t end = host_name.find('.', pos);
			if (end == std::string::npos) {
				end = host_name.size();
			}
			if (end == pos || end - pos > 255) { // NOLINT
				throw std::logic_error("Malformed domain name: " + host_name);
			}
			pos = end + 1;
		}
	}

	// The list of the DNS servers to use.
	std::vector<setka::address> servers;
	{
		std::lock_guard dns_lock(dns::dns_mutex);

		if (dns_ip.host.is_valid()) {
			servers.push_back(dns_ip);
		} else {
			if (!dns::dns_servers_loaded) {
				dns::load_dns_servers();
				dns::dns_servers_loaded = true;
			}
			if (dns::dns_servers.empty()) {
				throw std::runtime_error(
					std::string("No DNS server is available to use: ") +
					(dns::dns_servers_error.empty() ? "unknown reason" : dns::dns_servers_error)
				);
			}
			servers = dns::dns_servers;
		}
	}

	// The record types to query. If the OS does not support IPv6, the AAAA queries
	// will simply be answered with the 'not found' result (or time out).
	std::vector<uint16_t> types;
	if (record_type == dns_record_type::ipv4) {
		types.push_back(dns_type_a);
	} else if (record_type == dns_record_type::ipv6) {
		types.push_back(dns_type_aaaa);
	} else { // dns_record_type::both
		types.push_back(dns_type_a);
		types.push_back(dns_type_aaaa);
	}

	// Check the DNS result cache for the immediate result.
	std::optional<setka::dns_result> immediate_result;
	std::vector<setka::address::ip> immediate_addresses;
	{
		uint16_t cache_type = 0;
		switch (record_type) {
			case dns_record_type::ipv4:
				cache_type = dns_type_a;
				break;
			case dns_record_type::ipv6:
				cache_type = dns_type_aaaa;
				break;
			default:
				cache_type = 0; // dns_record_type::both
				break;
		}
		std::lock_guard c_lock(cache_mutex);
		auto it = cache.find(cache_key{host_name, cache_type});
		if (it != cache.end() && it->second.expiry > clock_type::now()) {
			if (it->second.not_found) {
				immediate_result = setka::dns_result::not_found;
			} else if (it->second.addresses.size() > 0) {
				immediate_result = setka::dns_result::ok;
				immediate_addresses = it->second.addresses;
			}
		}
	}

	auto time_now = clock_type::now();

	// Create the request.
	auto req = std::make_unique<dns::request>();
	req->hnr = this;
	req->host_name = host_name;
	req->servers = std::move(servers);
	req->record_type = record_type;
	req->deadline = time_now + milliseconds(timeout_ms);

	if (immediate_result) {
		// A cache hit: the request completes immediately, no queries are sent.
		req->complete_immediately = true;
		req->immediate_result = *immediate_result;
		req->addresses = std::move(immediate_addresses);
	} else {
		req->num_legs = uint8_t(types.size());
		for (unsigned i = 0; i < types.size(); ++i) {
			req->legs[i].type = types[i];
			req->legs[i].needs_send = true;
			time_point t = time_now + milliseconds(initial_retransmit_interval_ms);
			req->legs[i].next_retransmit = t < req->deadline ? t : time_point{};
		}
	}

	// Make sure the background thread exists and is started, check the preconditions
	// and insert the request into the thread's data structures. All done in one
	// critical section to make the operation atomic.
	{
		std::lock_guard dns_lock(dns::dns_mutex);

		if (!dns::thread || dns::thread->fatal.load()) {
			auto t = std::make_unique<dns::lookup_thread>();
			if (dns::thread) {
				// The old thread has exited with a fatal error, the new thread will
				// join and destroy it (the thread object must be joined before it
				// can be destroyed, and it cannot be joined here, because the thread
				// may still be invoking the completion callbacks, which can in turn
				// call resolve() and wait for the dns_mutex we hold).
				t->prev_thread = std::move(dns::thread);
			}
			dns::thread = std::move(t);
		}

		std::lock_guard t_lock(dns::thread->mutex);

		// Check if there is already an in-progress DNS lookup on this resolver object.
		if (dns::thread->requests.find(this) != dns::thread->requests.end()) {
			throw std::logic_error("DNS lookup operation is already in progress");
		}

		if (!dns::thread->started.load()) {
			try {
				dns::thread->start();
			} catch (...) {
				throw; // nothing to clean up, the thread will be re-created on the next call
			}
			dns::thread->started = true;
			LOG([&](auto& o) {
				o << "dns_resolver::resolve(): thread started" << std::endl;
			})
		}

		// Allocate the DNS transaction IDs for the request legs.
		for (unsigned i = 0; i < req->num_legs; ++i) {
			try {
				req->legs[i].id = dns::thread->take_id(); // may throw too_many_requests
			} catch (...) {
				// Release the IDs already taken for the previous legs.
				for (unsigned j = 0; j < i; ++j) {
					dns::thread->release_id(req->legs[j].id);
				}
				throw;
			}
		}

		// NOTE: capture the values needed below before moving 'req' into the map,
		//       since after the move 'req' is empty and cannot be dereferenced.
		dns::request* r = req.operator->();
		const bool complete_immediately = req->complete_immediately;
		const time_point deadline = req->deadline;

		try {
			for (unsigned i = 0; i < r->num_legs; ++i) {
				r->legs[i].in_flight = true;
				dns::thread->ids[r->legs[i].id] = r;
			}
			dns::thread->requests[this] = std::move(req);
			if (!complete_immediately) {
				dns::thread->send_queue.push_back(r);
				dns::thread->deadlines.insert({deadline, r});
			}
		} catch (...) {
			// Roll back the partially inserted request.
			for (unsigned i = 0; i < r->num_legs; ++i) {
				if (r->legs[i].in_flight) {
					dns::thread->ids.erase(r->legs[i].id);
					dns::thread->release_id(r->legs[i].id);
				}
			}
			dns::thread->requests.erase(this); // no-op if the request was not inserted into the map
			throw;
		}
	}

	// Wake up the background thread.
	dns::thread->wake_up();
}

void dns_resolver::clean_up()
{
	std::unique_ptr<dns::lookup_thread> t;
	{
		std::lock_guard dns_lock(dns::dns_mutex);

		if (!dns::thread) {
			return;
		}

		dns::thread->quit_flag = true;
		// Take the thread out of the global variable, so that a new thread can be
		// created by a concurrent (or a callback-initiated) resolve() call while we
		// are joining the old one.
		t = std::move(dns::thread);
		dns::thread = nullptr;
	}

	t->wake_up();
	t->join();
}
