#include <utki/config.hpp>

#include "Setka.hpp"
#include "Exc.hpp"
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



utki::intrusive_singleton<Setka>::T_Instance Setka::instance;



Setka::Setka(){
#if M_OS == M_OS_WINDOWS
	WORD versionWanted = MAKEWORD(2,2);
	WSADATA wsaData;
	if(WSAStartup(versionWanted, &wsaData) != 0 ){
		throw setka::Exc("SocketLib::SocketLib(): Winsock 2.2 initialization failed");
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



Setka::~Setka()noexcept{
	// check that there are no active dns lookups and finish the DNS request thread
	dns_resolver::clean_up();
	
#if M_OS == M_OS_WINDOWS
	// clean up windows networking
	if(WSACleanup() == SOCKET_ERROR){
		if(WSAGetLastError() == WSAEINPROGRESS){
			WSACancelBlockingCall();
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
