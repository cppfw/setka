/*
The MIT License (MIT)

Copyright (c) 2015-2021 Ivan Gagis <igagis@gmail.com>

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

#include <utki/config.hpp>

#include "init_guard.hpp"
#include "dns_resolver.hpp"

#if M_OS == M_OS_WINDOWS
#	include <winsock2.h>
#	include <utki/windows.hpp>

#elif M_OS == M_OS_LINUX || M_OS == M_OS_UNIX || M_OS == M_OS_MACOSX
#	include <signal.h>

#else
#	error "Unsupported OS"
#endif

using namespace setka;

utki::intrusive_singleton<init_guard>::T_Instance init_guard::instance;

init_guard::init_guard(){
#if M_OS == M_OS_WINDOWS
	WORD versionWanted = MAKEWORD(2,2);
	WSADATA wsaData;
	if(int error = WSAStartup(versionWanted, &wsaData)){
		std::stringstream ss;
		ss << "WSAStartup(): Winsock 2.2 initialization failed, error code = " << error;
		throw std::runtime_error(ss.str());
	}
#elif M_OS == M_OS_LINUX || M_OS == M_OS_UNIX || M_OS == M_OS_MACOSX
	// SIGPIPE is generated when a remote socket is closed
	void (*handler)(int);
	handler = signal(SIGPIPE, SIG_IGN);
	if(handler != SIG_DFL){
		signal(SIGPIPE, handler);
	}
#else
	#error "Unknown OS"
#endif
}

init_guard::~init_guard()noexcept{
	// check that there are no active dns lookups and finish the DNS request thread
	dns_resolver::clean_up();
	
#if M_OS == M_OS_WINDOWS
	// clean up windows networking
	if(WSACleanup() == SOCKET_ERROR){
		if(WSAGetLastError() == WSAEINPROGRESS){
			WSACleanup();
		}
	}
#elif M_OS == M_OS_LINUX || M_OS == M_OS_UNIX || M_OS == M_OS_MACOSX
	// restore the SIGPIPE handler
	void (*handler)(int);
	handler = signal(SIGPIPE, SIG_DFL);
	if(handler != SIG_IGN){
		signal(SIGPIPE, handler);
	}
#else
	#error "Unknown OS"
#endif
}
