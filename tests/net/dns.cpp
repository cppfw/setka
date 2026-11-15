#include "dns.hpp"

#include "../../src/setka/dns_resolver.hpp"
#include "../../src/setka/tcp_server_socket.hpp"
#include "../../src/setka/tcp_socket.hpp"
#include "../../src/setka/udp_socket.hpp"

#include <atomic>
#include <nitki/thread.hpp>
#include <nitki/semaphore.hpp>

#include <array>
#include <chrono>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#ifdef assert
#  undef assert
#endif

namespace test_simple_dns_lookup {

class resolver : public setka::dns_resolver
{
public:
	resolver(nitki::semaphore& sema, std::string host_name = std::string()) :
			sema(sema),
			host_name(std::move(host_name))
	{}

	setka::address::ip ip{};

	nitki::semaphore& sema;

	setka::dns_result res = setka::dns_result::error;

	std::string host_name;

	void resolve_host()
	{
		this->resolve(this->host_name, 10000);
	}

	void on_completed(setka::dns_result res, utki::span<const setka::address::ip> ips) noexcept override
	{
		LOG([&](auto& o) {
			o << "on_completed(): result = " << unsigned(res) << std::endl;
		})

		this->res = res;

		if (res == setka::dns_result::ok) {
			this->ip = ips.front();
		}

		this->sema.signal();
	}
};

void run()
{
	// test one resolve at a time
	{
		nitki::semaphore sema;

		resolver r(sema);

		r.resolve("google.com", 10000);

		LOG([&](auto& o) {
			o << "test_simple_dns_lookup::run(): waiting on semaphore" << std::endl;
		})

		if (!sema.wait(11000)) {
			utki::assert_always(false, SL);
		}

		utki::assert_always(
			r.res == setka::dns_result::ok,
			[&](auto& o) {
				o << "r.result = " << unsigned(r.res);
			},
			SL
		);

		utki::assert_always(
			r.ip.is_valid(),
			[&](auto& o) {
				o << "ip = " << r.ip.to_string();
			},
			SL
		);

		LOG([&](auto& o) {
			o << "ip = " << r.ip.to_string() << std::endl;
		})
	}

	// test several resolves at a time
	{
		nitki::semaphore sema;

		using resolver_list_type = std::vector<std::unique_ptr<resolver>>;
		resolver_list_type r;

		r.push_back(std::make_unique<resolver>(sema, "google.com"));
		r.push_back(std::make_unique<resolver>(sema, "gagistech.com"));
		r.push_back(std::make_unique<resolver>(sema, "yle.fi"));
		r.push_back(std::make_unique<resolver>(sema, "facebook.com"));

		for (auto& rslvr : r) {
			rslvr->resolve_host();
		}

		for (unsigned i = 0; i < r.size(); ++i) {
			if (!sema.wait(11000)) {
				utki::assert_always(false, SL);
			}
		}

		for (auto& rslvr : r) {
			utki::assert_always(
				rslvr->res == setka::dns_result::ok,
				[&](auto& o) {
					o << "result = " << unsigned(rslvr->res) << " host to resolve = " << rslvr->host_name;
				},
				SL
			);

			utki::assert_always(rslvr->ip.is_valid(), SL);
		}
	}
}

}

namespace test_request_from_callback {

class resolver : public setka::dns_resolver
{
public:
	resolver(nitki::semaphore& sema) :
			sema(sema)
	{}

	std::string host;

	setka::address::ip ip{};

	nitki::semaphore& sema;

	setka::dns_result res = setka::dns_result::error;

	void on_completed(setka::dns_result res, utki::span<const setka::address::ip> ips) noexcept override
	{
		if (this->host.size() == 0) {
			utki::assert_always(
				res == setka::dns_result::not_found,
				[&](auto& o) {
					o << "result = " << unsigned(res);
				},
				SL
			);
			utki::assert_always(ips.size() == 0, SL);

			this->host = "google.com";
			this->resolve(this->host, 5000);
		} else {
			utki::assert_always(this->host == "google.com", SL);
			utki::assert_always(res == setka::dns_result::ok, SL);
			utki::assert_always(ips.size() > 0, SL);

			this->res = res;
			this->ip = ips.front();
			this->sema.signal();
		}
	}
};

void run()
{
	nitki::semaphore sema;

	resolver r(sema);

	r.resolve("rfesfdf.com", 3000);

	if (!sema.wait(8000)) {
		utki::assert_always(false, SL);
	}

	utki::assert_always(
		r.res == setka::dns_result::ok,
		[&](auto& o) {
			o << "r.result = " << unsigned(r.res);
		},
		SL
	);

	utki::assert_always(r.ip.is_valid(), SL);
}
}

namespace test_cancel_dns_lookup {

class resolver : public setka::dns_resolver
{
public:
	std::atomic<bool> called = false;

	void on_completed(setka::dns_result, utki::span<const setka::address::ip>) noexcept override
	{
		this->called = true;
	}
};

void run()
{
	utki::log([&](auto& o) {
		o << "\tRunning 'cancel DNS lookup' test, it will take about 4 seconds" << std::endl;
	});

	// cancel() on a resolver with no in-progress lookup must return false
	{
		resolver r;
		utki::assert_always(!r.cancel(), SL);
	}

	resolver r;

	r.resolve("rfesweefdqfdf.com", 3000, setka::dns_record_type::both, setka::address("1.2.3.4", 53));

	std::this_thread::sleep_for(std::chrono::milliseconds(500));

	bool res = r.cancel();

	std::this_thread::sleep_for(std::chrono::milliseconds(3000));

	utki::assert_always(res, SL);
	utki::assert_always(!r.called, SL);

	// cancel() again after the lookup has been cancelled must return false
	utki::assert_always(!r.cancel(), SL);
}
}

namespace test_completed_handler {

void run()
{
	// Use the non-derived (handler based) API: no need to subclass the dns_resolver.
	{
		nitki::semaphore sema;
		setka::dns_result res = setka::dns_result::error;
		setka::address::ip ip{};

		setka::dns_resolver r;
		r.set_completed_handler([&](setka::dns_result result, utki::span<const setka::address::ip> ips) {
			res = result;
			if (result == setka::dns_result::ok) {
				ip = ips.front();
			}
			sema.signal();
		});

		r.resolve("google.com", 10000);

		if (!sema.wait(11000)) {
			utki::assert_always(false, SL);
		}

		utki::assert_always(
			res == setka::dns_result::ok,
			[&](auto& o) {
				o << "res = " << unsigned(res);
			},
			SL
		);
		utki::assert_always(ip.is_valid(), SL);

		LOG([&](auto& o) {
			o << "test_completed_handler: ip = " << ip.to_string() << std::endl;
		})
	}

	// A second lookup of the same host must be served from the DNS result cache
	// and therefore complete much faster than the (short) timeout.
	{
		nitki::semaphore sema;
		setka::dns_result res = setka::dns_result::error;

		setka::dns_resolver r;
		r.set_completed_handler([&](setka::dns_result result, utki::span<const setka::address::ip>) {
			res = result;
			sema.signal();
		});

		r.resolve("google.com", 10000);
		if (!sema.wait(11000)) {
			utki::assert_always(false, SL);
		}
		utki::assert_always(res == setka::dns_result::ok, SL);

		res = setka::dns_result::error;

		// The result of the previous lookup is in the cache now, so this lookup
		// should complete almost instantly, even with the short timeout.
		r.resolve("google.com", 100);
		if (!sema.wait(500)) {
			utki::assert_always(false, SL);
		}
		utki::assert_always(
			res == setka::dns_result::ok,
			[&](auto& o) {
				o << "res = " << unsigned(res);
			},
			SL
		);
	}

	// A negative (not found) lookup result must also be cached: the second lookup
	// of the same non-existent domain must complete almost instantly.
	{
		nitki::semaphore sema;
		setka::dns_result res = setka::dns_result::error;

		setka::dns_resolver r;
		r.set_completed_handler([&](setka::dns_result result, utki::span<const setka::address::ip>) {
			res = result;
			sema.signal();
		});

		r.resolve("sdlkfjweoifjwer.com", 10000);
		if (!sema.wait(11000)) {
			utki::assert_always(false, SL);
		}
		utki::assert_always(res == setka::dns_result::not_found, SL);

		res = setka::dns_result::error;

		r.resolve("sdlkfjweoifjwer.com", 100);
		if (!sema.wait(500)) {
			utki::assert_always(false, SL);
		}
		utki::assert_always(
			res == setka::dns_result::not_found,
			[&](auto& o) {
				o << "res = " << unsigned(res);
			},
			SL
		);
	}
}
}

namespace test_resolve_record_types {

void run()
{
	// IPv4 (A) records
	{
		nitki::semaphore sema;
		setka::dns_result res = setka::dns_result::error;
		std::vector<setka::address::ip> ips;

		setka::dns_resolver r;
		r.set_completed_handler([&](setka::dns_result result, utki::span<const setka::address::ip> a) {
			res = result;
			ips.assign(a.begin(), a.end());
			sema.signal();
		});

		r.resolve("google.com", 10000, setka::dns_record_type::ipv4);

		if (!sema.wait(11000)) {
			utki::assert_always(false, SL);
		}

		utki::assert_always(
			res == setka::dns_result::ok,
			[&](auto& o) {
				o << "res = " << unsigned(res);
			},
			SL
		);
		utki::assert_always(ips.size() > 0, SL);
		for (auto& ip : ips) {
			utki::assert_always(ip.is_v4(), SL);
		}
	}

	// IPv6 (AAAA) records
	{
		nitki::semaphore sema;
		setka::dns_result res = setka::dns_result::error;
		std::vector<setka::address::ip> ips;

		setka::dns_resolver r;
		r.set_completed_handler([&](setka::dns_result result, utki::span<const setka::address::ip> a) {
			res = result;
			ips.assign(a.begin(), a.end());
			sema.signal();
		});

		r.resolve("google.com", 10000, setka::dns_record_type::ipv6);

		if (!sema.wait(11000)) {
			utki::assert_always(false, SL);
		}

		// The lookup can legitimately complete with not_found if the DNS server
		// does not have the AAAA record, in which case the addresses must be empty.
		utki::assert_always(
			res == setka::dns_result::ok || res == setka::dns_result::not_found,
			[&](auto& o) {
				o << "res = " << unsigned(res);
			},
			SL
		);
		if (res == setka::dns_result::ok) {
			utki::assert_always(ips.size() > 0, SL);
			for (auto& ip : ips) {
				utki::assert_always(ip.is_v6(), SL);
			}
		} else {
			utki::assert_always(ips.size() == 0, SL);
		}
	}
}
}

namespace test_dns_tcp_fallback {

// The a-record the fake server returns: 10.20.30.40
constexpr uint8_t fake_a_record[4] = {0x0a, 0x14, 0x1e, 0x28};

// The aaaa-record the fake server returns: 2001:db8::10.20.30.40
constexpr uint8_t fake_aaaa_record[16] = {
	0x20, 0x01, 0x0d, 0xb8, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x0a, 0x14, 0x1e, 0x28
};

// Sends the entire buffer over the connection, retrying while nothing can be sent.
void send_full(setka::tcp_socket& sock, const std::vector<uint8_t>& buf)
{
	size_t sent = 0;
	for (int i = 0; i < 2000 && sent < buf.size(); ++i) {
		size_t n = sock.send(utki::make_span(buf.data() + sent, buf.size() - sent));
		if (n > 0) {
			sent += n;
		} else {
			std::this_thread::sleep_for(std::chrono::milliseconds(2));
		}
	}
}

// Extracts the dotted host name from the dns query (the name in the question
// section), or an empty string if the query is malformed.
std::string query_host_name(const uint8_t* q, size_t len)
{
	if (len < 12) {
		return "";
	}
	std::string host;
	const uint8_t* p = q + 12;
	const uint8_t* end = q + len;
	bool first = true;
	while (p < end && *p != 0) {
		uint8_t label_len = *p & 0x3f;
		if (size_t(p - q) + 1 + label_len > len) {
			return "";
		}
		if (!first) {
			host += '.';
		}
		host.append(reinterpret_cast<const char*>(p + 1), label_len); // NOLINT
		p += 1 + label_len;
		first = false;
	}
	return p < end ? host : "";
}

// The fake dns server for the tcp fallback test:
//  - responds to every udp query with a truncated response (the TC flag set,
//    no answers), forcing the client to retry the query over tcp,
//  - serves the tcp query with the full response (a single a or aaaa record
//    answer), except for the queries with the "close" in the host name: the
//    corresponding tcp connection is closed immediately without a response.
class server : public nitki::thread
{
public:
	explicit server(uint16_t port) :
			udp(port),
			tcp(port)
	{}

	std::atomic<bool> quit_flag = false;

private:
	setka::udp_socket udp;
	setka::tcp_server_socket tcp;
	std::chrono::steady_clock::time_point start_time;
	std::atomic<bool> close_next_tcp = false;

	// Reads the 2-byte length prefixed query from the connection and responds
	// with the 2-byte length prefixed full response: the header, the question
	// (copied as-is from the query) and a single a-record answer.
	void serve(setka::tcp_socket& sock)
	{
		std::vector<uint8_t> rx;
		uint16_t msg_len = 0;
		for (int i = 0; i < 2000; ++i) {
			uint8_t tmp[512];
			size_t m = 0;
			try {
				m = sock.receive(utki::make_span(tmp, sizeof(tmp)));
			} catch (std::exception&) {
				return; // the client went away
			}
			if (m > 0) {
				rx.insert(rx.end(), tmp, tmp + m);
			}
			if (rx.size() >= 2) {
				msg_len = uint16_t((rx[0] << 8) | rx[1]); // NOLINT
				if (rx.size() >= size_t(2) + msg_len) {
					break;
				}
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(2));
		}

		if (rx.size() < size_t(2) + msg_len) {
			return; // the client went away before the full query was sent
		}

		// the query occupies rx[2 .. 2 + msg_len): the 12-byte header, the question
		// (the name, the qtype and the qclass) and, optionally, the additional section
		// (the edns0 opt record). The question must be copied into the response as-is.
		size_t p = 2 + 12;
		while (p < size_t(2) + msg_len && rx[p] != 0) { // NOLINT
			p += 1 + (rx[p] & 0x3f); // NOLINT the label: the length byte and the label itself
		}
		if (p >= size_t(2) + msg_len) {
			return; // malformed query
		}
		size_t q_end = p + 1 + 4; // NOLINT the name terminator, the qtype and the qclass

		std::vector<uint8_t> resp;
		resp.reserve(64);
		resp.push_back(rx[2]); // NOLINT the query id, echoed
		resp.push_back(rx[3]);
		resp.push_back(0x81); // flags: QR|RD|RA
		resp.push_back(0x80);
		resp.push_back(0x00); resp.push_back(0x01); // QDCOUNT = 1
		resp.push_back(0x00); resp.push_back(0x01); // ANCOUNT = 1
		resp.push_back(0x00); resp.push_back(0x00); // NSCOUNT = 0
		resp.push_back(0x00); resp.push_back(0x00); // ARCOUNT = 0
		resp.insert(resp.end(), rx.begin() + 14, rx.begin() + q_end); // the question
		resp.push_back(0xc0); resp.push_back(0x0c); // the answer name: a pointer to the question
		uint16_t qtype = uint16_t((rx[q_end - 4] << 8) | rx[q_end - 3]); // NOLINT the question type, echoed
		resp.push_back(uint8_t(qtype >> 8));
		resp.push_back(uint8_t(qtype & 0xff));
		resp.push_back(0x00); resp.push_back(0x01); // the IN class
		resp.push_back(0x00); resp.push_back(0x00); resp.push_back(0x00); resp.push_back(0x3c); // ttl = 60
		if (qtype == 1) { // the a-record
			resp.push_back(0x00); resp.push_back(0x04); // rdlength = 4
			resp.insert(resp.end(), fake_a_record, fake_a_record + 4);
		} else { // the aaaa-record
			resp.push_back(0x00); resp.push_back(0x10); // rdlength = 16
			resp.insert(resp.end(), fake_aaaa_record, fake_aaaa_record + 16);
		}

		std::vector<uint8_t> out;
		out.reserve(2 + resp.size());
		uint16_t len = uint16_t(resp.size());
		out.push_back(uint8_t(len >> 8));
		out.push_back(uint8_t(len & 0xff));
		out.insert(out.end(), resp.begin(), resp.end());

		try {
			send_full(sock, out);
		} catch (std::exception&) {
			// the client went away, nothing to do
		}
	}

	void run() override
	{
		this->start_time = std::chrono::steady_clock::now();
		std::array<uint8_t, 2048> buf;
		while (!this->quit_flag) {
			// the udp side: respond to every query with a truncated response to
			// trigger the tcp fallback. The responses are delayed to force the client
			// to retransmit the query before the first response arrives.
			auto [n, from] = this->udp.receive(utki::make_span(buf.data(), buf.size()));
			if (
				n >= 12 &&
				std::chrono::steady_clock::now() - this->start_time >= std::chrono::milliseconds(1100)
			) {
				const std::string host = query_host_name(buf.data(), n);
				if (host.find("close") != std::string::npos) {
					this->close_next_tcp = true;
				}
				buf[2] = 0x83; // flags: QR|RD|RA|TC
				buf[3] = 0x80;
				this->udp.send(utki::make_span(buf.data(), n), from);
			}

			// the tcp side: accept a new connection and serve it, unless the
			// connection is to be closed immediately (the "close" scenario)
			setka::tcp_socket sock = this->tcp.accept();
			if (!sock.empty()) {
				if (this->close_next_tcp.exchange(false)) {
					sock.disconnect();
				} else {
					this->serve(sock);
				}
			}

			std::this_thread::sleep_for(std::chrono::milliseconds(5));
		}
	}
};

void run()
{
	LOG([&](auto& o) { o << "Running 'dns tcp fallback' test" << std::endl; })

	constexpr uint16_t port = 39101;

	server srv(port);
	srv.start();

	nitki::semaphore sema;
	setka::dns_result res = setka::dns_result::error;
	std::vector<setka::address::ip> ips;

	setka::dns_resolver r;
	r.set_completed_handler([&](setka::dns_result result, utki::span<const setka::address::ip> a) {
		res = result;
		ips.assign(a.begin(), a.end());
		sema.signal();
	});

	r.resolve("fake.tcp.fallback", 10000, setka::dns_record_type::ipv4, setka::address("127.0.0.1", port));

	if (!sema.wait(11000)) {
		utki::assert_always(false, SL);
	}

	utki::assert_always(
		res == setka::dns_result::ok,
		[&](auto& o) {
			o << "res = " << unsigned(res);
		},
		SL
	);
	utki::assert_always(ips.size() == 1, SL);
	utki::assert_always(ips[0].is_v4(), SL);
	utki::assert_always(
		ips[0].get_v4() == 0x0a141e28, // 10.20.30.40
		[&](auto& o) {
			o << "ip = " << ips[0].to_string();
		},
		SL
	);

	// the aaaa record via the tcp fallback
	{
		nitki::semaphore sema2;
		setka::dns_result res2 = setka::dns_result::error;
		std::vector<setka::address::ip> ips2;

		setka::dns_resolver r2;
		r2.set_completed_handler([&](setka::dns_result result, utki::span<const setka::address::ip> a) {
			res2 = result;
			ips2.assign(a.begin(), a.end());
			sema2.signal();
		});

		r2.resolve("fake.tcp.fallback6", 10000, setka::dns_record_type::ipv6, setka::address("127.0.0.1", port));

		if (!sema2.wait(11000)) {
			utki::assert_always(false, SL);
		}

		utki::assert_always(
			res2 == setka::dns_result::ok,
			[&](auto& o) {
				o << "res2 = " << unsigned(res2);
			},
			SL
		);
		utki::assert_always(ips2.size() == 1, SL);
		utki::assert_always(ips2[0].is_v6(), SL);
		utki::assert_always(ips2[0].quad[0] == 0x20010db8, SL);
		utki::assert_always(ips2[0].quad[3] == 0x0a141e28, SL);
	}

	// the tcp connection is closed by the server without a response: the lookup
	// must fail with the dns_error result
	{
		nitki::semaphore sema3;
		setka::dns_result res3 = setka::dns_result::ok;

		setka::dns_resolver r3;
		r3.set_completed_handler([&](setka::dns_result result, utki::span<const setka::address::ip>) {
			res3 = result;
			sema3.signal();
		});

		r3.resolve("close.fake.tcp.fallback", 10000, setka::dns_record_type::ipv4, setka::address("127.0.0.1", port));

		if (!sema3.wait(11000)) {
			utki::assert_always(false, SL);
		}

		utki::assert_always(
			res3 == setka::dns_result::dns_error,
			[&](auto& o) {
				o << "res3 = " << unsigned(res3);
			},
			SL
		);
	}

	srv.quit_flag = true;
	srv.join();
}
}
