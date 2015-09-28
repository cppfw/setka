#pragma once

#include <utki/Exc.hpp>



namespace ting{
namespace net{



/**
 * @brief Basic exception class.
 * This is a basic exception class for network related errors.
 */
class Exc : public utki::Exc{
public:
	/**
	 * @brief Exception constructor.
	 * Creates exception with empty message.
     */
	inline Exc()noexcept{}
	
	/**
	 * @brief Exception constructor.
	 * @param message - human friendly error description.
	 */
	inline Exc(const std::string& message) :
			utki::Exc(message)
	{}
};



}//~namespace
}//~namespace
