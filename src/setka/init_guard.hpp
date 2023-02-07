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

#include <utki/config.hpp>
#include <utki/singleton.hpp>

namespace setka {

/**
 * @brief Socket library singleton class.
 * This is a Socket library singleton class. Creating an object of this class initializes the library
 * while destroying this object de-initializes it. So, the convenient way of initializing the library
 * is to create an object of this class on the stack. Thus, when the object goes out of scope its
 * destructor will be called and the library will be de-initialized automatically.
 * This is what C++ RAII is all about.
 */
class init_guard : public utki::intrusive_singleton<init_guard>
{
	friend class utki::intrusive_singleton<init_guard>;
	static utki::intrusive_singleton<init_guard>::instance_type instance;

public:
	init_guard();

	~init_guard() noexcept;
};

} // namespace setka
