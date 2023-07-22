/**
 * Author: Hendrik van Arragon, 2022
 * SPDX-License-Identifier: MIT
 */

#include "main.h"
#include "mbed.h"

#include <abmt/os.h>
#include <abmt/io/buffer.h>
#include <abmt/rt/model.h>

#include "src/model_adapter_std.h"
#include "src/com_device.h"

model_adatper_std adapter;

void abmt::log(std::string s){
	adapter.log(s);
}
void abmt::log_err(std::string s){
	adapter.log_err(s);
}
void abmt::die(std::string s){
	abmt::log_err("Fatal Error:");
	abmt::log_err(s);
	abmt::log_err("Reset!");
	ThisThread::sleep_for(std::chrono::milliseconds(5));
	NVIC_SystemReset();
}

void abmt::die_if(bool condition, std::string msg){
	if(condition){
		abmt::die(msg);
	}
}

auto start_time = Kernel::Clock::now();
abmt::time abmt::time::now(){
	return {std::chrono::duration_cast<std::chrono::nanoseconds>(Kernel::Clock::now() - start_time).count()};
}

struct single_thread_raster{
	abmt::rt::raster* raster;
	uint32_t raster_index;
	abmt::time next_run;
	bool init_tick_executed = false;

	single_thread_raster(abmt::rt::raster* r, uint32_t r_idx): raster(r), raster_index(r_idx){
		next_run = abmt::time::now();
		raster->init();
	}

	// returns true if the raster was executed
	void run(){
		if(raster->is_sync){
			next_run = abmt::time::now() + raster->interval;
		}else{
			abmt::time sleep_time = raster->poll();
			if(sleep_time > 0){
				next_run = abmt::time::now() + sleep_time;
				return ;
			}
		}
		if(init_tick_executed){
			raster->tick();
		}else{
			raster->init_tick();
			init_tick_executed = true;
		}
		
		adapter.send_daq(raster_index);
		raster->n_ticks++;
	}
};

abmt::io::buffer in(128);
abmt::io::buffer out(280);
uint32_t bytes_read = 0;
uint32_t bytes_send = 0;

int main() 
{  
	ThisThread::sleep_for(std::chrono::milliseconds(5));
	com_device* com = get_com_device();
	auto mdl_ptr = abmt::rt::get_model();

	// feel free to change this, when you have large signalnames or huge object definitions
	adapter.max_def_size = 265;
	adapter.set_model(mdl_ptr);
	adapter.connection.set_source(&in);
	adapter.connection.set_sink(&out);


	single_thread_raster** raster_array = new single_thread_raster*[mdl_ptr->rasters.length];
	for(size_t raster_index = 0; raster_index < mdl_ptr->rasters.length; raster_index++){
		raster_array[raster_index] = new single_thread_raster(mdl_ptr->rasters[raster_index], raster_index);
	}

	bool buffer_error_send = false;
	bool nothing_done = false;
	abmt::time last_online_send = abmt::time::now();
	abmt::time sleep_until = abmt::time::now();

	while(1) {

		if(nothing_done){
			if(adapter.connected){
				ThisThread::sleep_for(std::chrono::milliseconds(1));
			}else{
				int ms_to_sleep = (int) (sleep_until - abmt::time::now()).ms();
				if(ms_to_sleep > 0){
					ThisThread::sleep_for(std::chrono::milliseconds(ms_to_sleep));
				}
			}
		}
		nothing_done = true;

		sleep_until = abmt::time::now() + abmt::time::sec(1);
		for(size_t raster_index = 0; raster_index < mdl_ptr->rasters.length; raster_index++){
			auto next_run = raster_array[raster_index]->next_run;
			if(next_run <= abmt::time::now()){
				nothing_done = false;
				raster_array[raster_index]->run();
			}
			if(sleep_until > next_run){
				sleep_until = next_run;
			}
		}

		if(com->ready() == false){
			out.flush();
			continue;
		}

		com->rcv(in.data + in.bytes_used, in.size - in.bytes_used, &bytes_read);
		if(bytes_read > 0){
			in.bytes_used += bytes_read;
			in.send();
			nothing_done = false;
		}

		if( (abmt::time::now() - last_online_send) > abmt::time::sec(1) ){
			adapter.send_model_online();
			last_online_send = abmt::time::now();
			buffer_error_send = false;
		}

		if(out.bytes_used > 0){
			com->snd(out.data,out.bytes_used,&bytes_send);
			out.pop_front(bytes_send);
			nothing_done = false;
		}

		if(out.bytes_used > SERIAL_BAUDRATE/8/10){
			if(buffer_error_send == false){
				buffer_error_send = true;
				nothing_done = false;
				out.flush();
				adapter.clear_daq_lists();
				abmt::log_err("Error: Data in outbuffer can't be send in 0.1s.");
				abmt::log_err("Datatransmission stopped. Reduce viewed signals...");
			}
		}
    }
}
