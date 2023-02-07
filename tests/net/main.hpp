#include <utki/debug.hpp>
#include "../../src/setka/init_guard.hpp"

#include "dns.hpp"
#include "socket.hpp"


inline void test_setka_socket(){
	setka::init_guard net_lib;
	
	basic_ip_address_test::run();
	test_ip_address::run();
		
	basic_client_server_test::run();
	basic_udp_sockets_test::run();
	test_udp_socket_wait_for_writing::run();
	send_data_continuously_with_wait_set::run();
	send_data_continuously::run();

	test_simple_dns_lookup::run();
	test_request_from_callback::run();
	test_cancel_dns_lookup::run();

	utki::log([&](auto&o){o << "[PASSED]: Socket test" << std::endl;});
}
