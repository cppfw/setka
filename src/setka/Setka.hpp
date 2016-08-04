#pragma once



#include <utki/Singleton.hpp>
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
class Setka : public utki::IntrusiveSingleton<Setka>{
	friend class utki::IntrusiveSingleton<Setka>;
	static utki::IntrusiveSingleton<Setka>::T_Instance instance;
	
public:
	Setka();

	~Setka()noexcept;
};



}//~namespace


/*
 * @mainpage ting::net library
 *
 * @section sec_about About
 * <b>ting::net</b> is a simple cross platform C++ wrapper above sockets networking API.
 *
 * @section sec_getting_started Getting started
 * @ref page_usage_tutorial "library usage tutorial" - quick start tutorial
 */

/*
 * @page page_usage_tutorial ting::net::Socket usage tutorial
 *
 * TODO: write usage tutorial
 */
