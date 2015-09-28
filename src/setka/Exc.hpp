#pragma once

#include <utki/Exc.hpp>



namespace setka{



/**
 * @brief Basic exception class.
 * This is a basic exception class for network related errors.
 */
class Exc : public utki::Exc{
public:
	/**
	 * @brief Exception constructor.
	 * @param message - human friendly error description.
	 */
	Exc(const std::string& message = std::string()) :
			utki::Exc(message)
	{}
};



}//~namespace
