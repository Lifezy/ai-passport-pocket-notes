#include <assert.h>
#include <string.h>
#include "pocket_setup_guard.h"
int main(void) {
    const char *key = "0123456789abcdef0123456789abcdef";
    assert(pocket_setup_authorized(key,key,"192.168.4.1","192.168.4.1",NULL,false));
    assert(pocket_setup_authorized(key,key,"192.168.4.1:80","192.168.4.1","http://192.168.4.1:80",true));
    assert(!pocket_setup_authorized(key,"wrong","192.168.4.1","192.168.4.1",NULL,false));
    assert(!pocket_setup_authorized(key,key,"evil.invalid","192.168.4.1","http://evil.invalid",true));
    assert(!pocket_setup_authorized(key,key,"192.168.4.1","192.168.4.1","http://evil.invalid",true));
    assert(!pocket_setup_authorized(key,key,"192.168.4.1","192.168.4.1",NULL,true));
    assert(!pocket_setup_authorized(key,key,"192.168.4.1:81","192.168.4.1",NULL,false));
    assert(pocket_setup_wifi_valid("", ""));
    assert(pocket_setup_wifi_valid("home", "test-password"));
    assert(!pocket_setup_wifi_valid("home", "short"));
    assert(!pocket_setup_wifi_valid("home\n", "test-password"));
    char password[65]; memset(password,'a',64); password[64]=0;
    assert(pocket_setup_wifi_valid("home",password));
    password[0]='z'; assert(!pocket_setup_wifi_valid("home",password));
    return 0;
}
