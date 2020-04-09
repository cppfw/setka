#pragma once

#include <string>
#include <functional>

#include <utki/config.hpp>

#include "ip_address.hpp"

namespace setka{

class init_guard;

/**
 * @brief Class for resolving IP-address of the host by its domain name.
 * This class allows asynchronous DNS lookup.
 * One has to derive his/her own class from this class to override the
 * OnCompleted_ts() method which will be called upon the DNS lookup operation has finished.
 */
class dns_resolver{
public:
	dns_resolver(const dns_resolver&) = delete;
	dns_resolver& operator=(const dns_resolver&) = delete;

	dns_resolver(){}
	
	virtual ~dns_resolver();
	
	class too_many_requests : public std::runtime_error{
	public:
		too_many_requests() :
				std::runtime_error("Too many active DNS lookup requests in progress, only 65536 simultaneous active requests allowed")
		{}
	};
	
	/**
	 * @brief Start asynchronous IP-address resolving.
	 * The method is thread-safe.
     * @param hostName - host name to resolve IP-address for. The host name string is case sensitive.
     * @param timeoutMillis - timeout for waiting for DNS server response in milliseconds.
	 * @param dnsIP - IP-address of the DNS to use for host name resolving. The default value is invalid IP-address
	 *                in which case the DNS IP-address will be retrieved from underlying OS.
	 * @throw std::logic_error when supplied for resolution domain name is too long. Must be 253 characters at most.
	 * @throw std::logic_error when DNS lookup operation served by this resolver object is already in progress.
	 * @throw too_many_requests when there are too much active DNS lookup requests are in progress, no resources for another one.
     */
	void resolve(
			const std::string& hostName,
			uint32_t timeoutMillis = 20000,
			const setka::ip_address& dnsIP = setka::ip_address(setka::ip_address::ip(0), 0)
		);
	
	/**
	 * @brief Cancel current DNS lookup operation.
	 * The method is thread-safe.
	 * After this method has returned it is guaranteed that the OnCompleted_ts()
	 * callback will not be called anymore, unless another resolve request has been
	 * started from within the callback if it was called before the Cancel_ts() method returns.
	 * Such case can be caught by checking the return value of the method.
	 * @return true - if the ongoing DNS lookup operation was canceled.
	 * @return false - if there was no ongoing DNS lookup operation to cancel.
	 *                 This means that the DNS lookup operation was not started
	 *                 or has finished before the Cancel_ts() method was called.
     */
	bool cancel()noexcept;
	
	/**
	 * @brief Enumeration of the DNS lookup operation result.
	 */
	enum class result{
		/**
		 * @brief DNS lookup operation completed successfully.
		 */
		ok,
		
		/**
		 * @brief Timeout hit while waiting for the response from DNS server.
		 */
		timeout,
		
		/**
		 * @brief DNS server reported that there is not such domain name found.
		 */
		not_found,
		
		/**
		 * @brief Error occurred while DNS lookup operation.
		 * Error reported by DNS server.
		 */
		dns_error,
		
#if M_OS == M_OS_WINDOWS
#	undef ERROR
#endif
		/**
		 * @brief Local error occurred.
		 */
		error
	};
	
	/**
	 * @brief handler for resolve result.
	 * Called by default implementation of virtual on_completed() function.
	 */
	std::function<void(result, ip_address::ip)noexcept> completed_handler;

	/**
	 * @brief callback method called upon DNS lookup operation has finished.
	 * Note, that the method has to be thread-safe.
	 * Default implementation just calls the completed_handler if it is set.
	 * @param r - the result of DNS lookup operation.
	 * @param ip - resolved IP-address. This value can later be used to create the
	 *             ip_address object.
	 */
	virtual void on_completed(result r, ip_address::ip ip)noexcept;
	
private:
	friend class setka::init_guard;
	static void clean_up();
};
}
