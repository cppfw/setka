#pragma once

#include <functional>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>

#include <utki/span.hpp>

#include "address.hpp"
#include "init_guard.hpp"

namespace setka {

class init_guard;

/**
 * @brief DNS lookup operation result.
 */
enum class dns_result {
	/**
	 * @brief DNS lookup operation completed successfully.
	 */
	ok,

	/**
	 * @brief timeout waiting for response from DNS server was hit.
	 */
	timeout,

	/**
	 * @brief DNS server reported that the domain does not exist
	 * or no records of the requested type were found.
	 */
	not_found,

	/**
	 * @brief an error occurred during the DNS lookup operation.
	 * An error was reported by the DNS server (e.g. SERVFAIL).
	 */
	dns_error,

	/**
	 * @brief a local error occurred.
	 */
	error
};

/**
 * @brief The DNS record type to resolve.
 */
enum class dns_record_type {
	/**
	 * @brief Resolve IPv4 (A) records only.
	 */
	ipv4,

	/**
	 * @brief Resolve IPv6 (AAAA) records only.
	 */
	ipv6,

	/**
	 * @brief Resolve both IPv4 and IPv6 records.
	 * Both queries are sent in parallel, and the lookup completes
	 * with the first record type that returned one or more addresses.
	 */
	both
};

/**
 * @brief a class for resolving a host's IP address from its domain name.
 * This class enables asynchronous DNS lookups.
 * One has to derive their own class from this class and override the
 * on_completed() method, which is called when the DNS lookup operation finishes.
 *
 * The on_completed() callback is always invoked from the library's background thread.
 * It must not destroy the resolver object from within the callback
 * and it should be thread-safe.
 *
 * The destructor cancels the in-progress DNS lookup operation (if any) and waits for
 * the in-flight completion callback (if any) to finish, so the object can be safely
 * destroyed from any thread.
 *
 * Constructing a dns_resolver object automatically acquires a reference to the library's
 * initialization guard (see setka::get_init_guard_reference()) and releases it when the
 * object is destroyed, so the library remains initialized for as long as at least one
 * resolver object is alive. The constructor therefore performs the library's one-time
 * initialization, which can fail (e.g. WSAStartup() error on Windows).
 */
class dns_resolver
{
public:
	dns_resolver(const dns_resolver&) = delete;
	dns_resolver& operator=(const dns_resolver&) = delete;

	dns_resolver(dns_resolver&&) = delete;
	dns_resolver& operator=(dns_resolver&&) = delete;

	dns_resolver() = default;

	virtual ~dns_resolver();

	class too_many_requests : public std::runtime_error
	{
	public:
		too_many_requests() :
			std::runtime_error(
				"Too many active DNS lookup requests in progress, only 65536 simultaneous active requests allowed"
			)
		{}
	};

	constexpr static const auto default_timeout_ms = 20000;

	/**
	 * @brief the type of the handler for the resolve result.
	 */
	using completed_handler_type = std::function<void(setka::dns_result, utki::span<const setka::address::ip>)>;

	/**
	 * @brief set the handler for the resolve result.
	 * The handler is invoked by the default implementation of the virtual
	 * on_completed() function.
	 *
	 * This method is thread-safe, the handler can be set from any thread
	 * at any time.
	 */
	void set_completed_handler(completed_handler_type handler);

	/**
	 * @brief starts an asynchronous IP address resolve.
	 * The method is thread-safe.
	 *
	 * @param host_name - the hostname to resolve the IP address for.
	 * @param timeout_ms - timeout in milliseconds to wait for the DNS server's response.
	 * @param record_type - the type of DNS record to resolve.
	 * @param dns_ip - the IP address of the DNS server to use for the hostname resolve.
	 *                The default value is an invalid IP address, in which case the DNS
	 *                server IP address is retrieved from the underlying OS.
	 *
	 * @throw std::logic_error if the supplied domain name is too long. It must be at most 253 characters.
	 * @throw std::logic_error if the supplied domain name is malformed.
	 * @throw std::logic_error if a DNS lookup operation is already in progress on this resolver object.
	 * @throw std::logic_error if the requested record type is not supported by the underlying OS.
	 * @throw std::runtime_error if no DNS server is available to use (the dns_ip argument is invalid
	 *                and no DNS server can be discovered from the OS).
	 * @throw too_many_requests if there are too many active DNS lookup requests in progress
	 *                and there is no resource for another one.
	 */
	void resolve(
		const std::string& host_name,
		uint32_t timeout_ms = default_timeout_ms,
		dns_record_type record_type = dns_record_type::both,
		const setka::address& dns_ip = setka::address(setka::address::ip(0), 0)
	);

	/**
	 * @brief cancels the current DNS lookup operation.
	 * The method is thread-safe and may also be safely called from within the
	 * on_completed() callback.
	 *
	 * After this method returns it is guaranteed that the on_completed() callback
	 * will not be called for the cancelled DNS lookup operation. Starting a new DNS
	 * lookup operation (e.g. from within the on_completed() callback) is not affected
	 * by the cancellation, the on_completed() callback of the new operation will be
	 * invoked as usual.
	 *
	 * @return true - if the in-progress DNS lookup operation was cancelled.
	 * @return false - if there was no in-progress DNS lookup operation to cancel.
	 *                 That is, the DNS lookup operation was either not started, or it had
	 *                 already finished before the cancel() method was called.
	 */
	bool cancel() noexcept;

	/**
	 * @brief the callback method invoked when the DNS lookup operation finishes.
	 *
	 * Note that the method is invoked from the library's background thread and it
	 * should be thread-safe.
	 *
	 * The default implementation simply invokes the completed_handler if it is set.
	 *
	 * @param r - the result of the DNS lookup operation.
	 * @param addresses - the resolved IP addresses (empty if the result is not ok).
	 *                    For dns_record_type::both, the addresses are of a single type
	 *                    (either all IPv4 or all IPv6) - the type of the first response
	 *                    that contained one or more addresses.
	 */
	virtual void on_completed(dns_result r, utki::span<const address::ip> addresses)
	{
		std::lock_guard<std::mutex> guard(this->completed_handler_mutex);
		if (this->completed_handler) {
			this->completed_handler(r, addresses);
		}
	}

private:
	friend class setka::init_guard;
	static void clean_up();

	// Deregisters the in-progress DNS lookup (if any) from the background thread
	// and waits for any in-flight completion callback to finish. After this method
	// returns, the on_completed() callback will no longer be invoked on this object.
	bool deregister() noexcept;

	// holds a reference to the library's initialization guard for the whole lifetime of
	// this object, so that the library cannot be de-initialized (last guard reference
	// dropped) while any resolver object is still alive. Active resolvers depend on the
	// initialized state (the background DNS lookup thread, Winsock, etc.). The reference
	// is deliberately released from the object's destructor and never from within the
	// on_completed() callback: dropping the last reference there would run clean_up()
	// on the DNS thread itself and join the thread from within the thread.
	std::shared_ptr<utki::destructable> setka_init_guard = get_init_guard_reference();

	std::mutex completed_handler_mutex;
	completed_handler_type completed_handler;
};
} // namespace setka
