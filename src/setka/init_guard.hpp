#pragma once

#include <utki/singleton.hpp>
#include <utki/config.hpp>

namespace setka{

/**
 * @brief Socket library singleton class.
 * This is a Socket library singleton class. Creating an object of this class initializes the library
 * while destroying this object de-initializes it. So, the convenient way of initializing the library
 * is to create an object of this class on the stack. Thus, when the object goes out of scope its
 * destructor will be called and the library will be de-initialized automatically.
 * This is what C++ RAII is all about.
 */
class init_guard : public utki::intrusive_singleton<init_guard>{
	friend class utki::intrusive_singleton<init_guard>;
	static utki::intrusive_singleton<init_guard>::T_Instance instance;
	
public:
	init_guard();

	~init_guard()noexcept;
};

}
