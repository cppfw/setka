/**
 * @author Ivan Gagis <igagis@gmail.com>
 */

#pragma once


#include <string>

#include <utki/config.hpp>

#include "Exc.hpp"
#include "IPAddress.hpp"



namespace setka{



//forward declarations
class Lib;



/**
 * @brief Class for resolving IP-address of the host by its domain name.
 * This class allows asynchronous DNS lookup.
 * One has to derive his/her own class from this class to override the
 * OnCompleted_ts() method which will be called upon the DNS lookup operation has finished.
 */
class DLLEXPORT HostNameResolver{
public:
	HostNameResolver(const HostNameResolver&) = delete;
	HostNameResolver& operator=(const HostNameResolver&) = delete;

	HostNameResolver(){}
	
	virtual ~HostNameResolver();
	
	/**
	 * @brief Basic DNS lookup exception.
     * @param message - human friendly error description.
     */
	class Exc : public setka::Exc{
	public:
		Exc(const std::string& message = std::string()) :
				setka::Exc(message)
		{}
	};
	
	class DomainNameTooLongExc : public Exc{
	public:
		DomainNameTooLongExc() :
				Exc("Too long domain name, it should not exceed 253 characters according to RFC 2181")
		{}
	};
	
	class TooMuchRequestsExc : public Exc{
	public:
		TooMuchRequestsExc() :
				Exc("Too much active DNS lookup requests in progress, only 65536 simultaneous active requests are allowed")
		{}
	};
	
	class AlreadyInProgressExc : public Exc{
	public:
		AlreadyInProgressExc() :
				Exc("DNS lookup operation is already in progress")
		{}
	};
	
	/**
	 * @brief Start asynchronous IP-address resolving.
	 * The method is thread-safe.
     * @param hostName - host name to resolve IP-address for. The host name string is case sensitive.
     * @param timeoutMillis - timeout for waiting for DNS server response in milliseconds.
	 * @param dnsIP - IP-address of the DNS to use for host name resolving. The default value is invalid IP-address
	 *                in which case the DNS IP-address will be retrieved from underlying OS.
	 * @throw DomainNameTooLongExc when supplied for resolution domain name is too long.
	 * @throw TooMuchRequestsExc when there are too much active DNS lookup requests are in progress, no resources for another one.
	 * @throw AlreadyInProgressExc when DNS lookup operation served by this resolver object is already in progress.
     */
	void resolve_ts(
			const std::string& hostName,
			std::uint32_t timeoutMillis = 20000,
			const setka::IPAddress& dnsIP = setka::IPAddress(setka::IPAddress::Host(0), 0)
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
	bool cancel_ts()noexcept;
	
	/**
	 * @brief Enumeration of the DNS lookup operation result.
	 */
	enum class E_Result{
		/**
		 * @brief DNS lookup operation completed successfully.
		 */
		OK,
		
		/**
		 * @brief Timeout hit while waiting for the response from DNS server.
		 */
		TIMEOUT,
		
		/**
		 * @brief DNS server reported that there is not such host.
		 */
		NO_SUCH_HOST,
		
		/**
		 * @brief Error occurred while DNS lookup operation.
		 * Error reported by DNS server.
		 */
		DNS_ERROR,
		
#if M_OS == M_OS_WINDOWS
#	undef ERROR
#endif
		/**
		 * @brief Local error occurred.
		 */
		ERROR
	};
	
	/**
	 * @brief callback method called upon DNS lookup operation has finished.
	 * Note, that the method has to be thread-safe.
	 * @param result - the result of DNS lookup operation.
	 * @param ip - resolved IP-address. This value can later be used to create the
	 *             ting::net::IPAddress object.
	 */
	virtual void onCompleted_ts(E_Result result, IPAddress::Host ip)noexcept = 0;
	
private:
	friend class setka::Lib;
	static void cleanUp();
};



}//~namespace
