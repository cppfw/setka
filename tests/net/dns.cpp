#include "dns.hpp"

#include "../../src/setka/dns_resolver.hpp"

#include <atomic>
#include <nitki/thread.hpp>
#include <nitki/semaphore.hpp>

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
