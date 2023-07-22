/**
 * Author: Hendrik van Arragon, 2023
 * SPDX-License-Identifier: MIT
 */
#include <abmt/mutex.h>
#include "mbed.h"

using namespace abmt;

mutex::mutex(){

}
void mutex::lock(){

}
void mutex::unlock(){

}

mutex::~mutex(){

}

scope_lock mutex::get_scope_lock(){
	return scope_lock(*this);
}

scope_lock::scope_lock(mutex& m):m(m){

}

scope_lock::~scope_lock(){

}
