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

#pragma once

#include <functional>
#include <stdexcept>
#include <string>

#include <utki/config.hpp>

#include "address.hpp"

namespace setka {

class init_guard;

/**
 * @brief Enumeration of the DNS lookup operation result.
 */
enum class dns_result {
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

	/**
	 * @brief Local error occurred.
	 */
	error
};

/**
 * @brief Class for resolving IP-address of the host by its domain name.
 * This class allows asynchronous DNS lookup.
 * One has to derive his/her own class from this class to override the
 * on_completed() method which will be called upon the DNS lookup operation has finished.
 */
class dns_resolver
{
public:
	dns_resolver(const dns_resolver&) = delete;
	dns_resolver& operator=(const dns_resolver&) = delete;

	dns_resolver() {}

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

	/**
	 * @brief Start asynchronous IP-address resolving.
	 * The method is thread-safe.
	 * @param host_name - host name to resolve IP-address for. The host name string is case sensitive.
	 * @param timeout_ms - timeout for waiting for DNS server response in milliseconds.
	 * @param dns_ip - IP-address of the DNS to use for host name resolving. The default value is invalid IP-address
	 *                in which case the DNS IP-address will be retrieved from underlying OS.
	 * @throw std::logic_error when supplied for resolution domain name is too long. Must be 253 characters at most.
	 * @throw std::logic_error when DNS lookup operation served by this resolver object is already in progress.
	 * @throw too_many_requests when there are too many active DNS lookup requests in progress, no resources for another
	 * one.
	 */
	void resolve(
		const std::string& host_name,
		uint32_t timeout_ms = 20000,
		const setka::address& dns_ip = setka::address(setka::address::ip(0), 0)
	);

	/**
	 * @brief Cancel current DNS lookup operation.
	 * The method is thread-safe.
	 * After this method has returned it is guaranteed that the on_completed()
	 * callback will not be called anymore, unless another resolve request has been
	 * started from within the callback if it was called before the cancel() method returns.
	 * Such case can be caught by checking the return value of the method.
	 * @return true - if the ongoing DNS lookup operation was canceled.
	 * @return false - if there was no ongoing DNS lookup operation to cancel.
	 *                 This means that the DNS lookup operation was not started
	 *                 or has finished before the cancel() method was called.
	 */
	bool cancel() noexcept;

	/**
	 * @brief handler for resolve result.
	 * Called by default implementation of virtual on_completed() function.
	 */
	std::function<void(setka::dns_result, setka::address::ip)> completed_handler;

	/**
	 * @brief callback method called upon DNS lookup operation has finished.
	 * Note, that the method has to be thread-safe.
	 * Default implementation just calls the completed_handler if it is set.
	 * @param r - the result of DNS lookup operation.
	 * @param ip - resolved IP-address. This value can later be used to create the
	 *             address object.
	 */
	virtual void on_completed(dns_result r, address::ip ip) noexcept;

private:
	friend class setka::init_guard;
	static void clean_up();
};
} // namespace setka
