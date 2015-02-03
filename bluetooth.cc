/*
 *
 *  libattgatt - Implementation of the Generic ATTribute Protocol
 *
 *  Copyright (C) 2013, 2014 Edward Rosten
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */


#include <iostream>
#include <sstream>
#include <iomanip>
#include <libattgatt/blestatemachine.h>
#include <libattgatt/float.h>
#include <deque>
#include <sys/time.h>
#include <unistd.h>
#include "cxxgplot.h"  //lolzworthy plotting program
using namespace std;

void bin(uint8_t i)
{
	for(int b=7; b >= 0; b--)
		cout << !!(i & (1<<b));

}

//ASCII throbber
string throbber(int i)
{
	string base = " (--------------------)";
	
	i = i%40;
	if(i >= 20)
		i = 19-(i-20);
	base[i + 2] = 'O';

	return base + string(base.size(), '\b');
}


double get_time_of_day()
{
        struct timeval tv;
        gettimeofday(&tv,NULL);
        return tv.tv_sec+tv.tv_usec * 1e-6;
}
////////////////////////////////////////////////////////////////////////////////
//
// This program demonstrates the use of the library
// 
int main(int argc, char **argv)
{
	if(argc != 2 && argc != 3)
	{	
		cerr << "Please supply address.\n";
		cerr << "Usage:\n";
		cerr << "prog address [nonblocking]\n";
		exit(1);
	}

	log_level = Info;

	
	//This is the interface to the BLW protocol.
	BLEGATTStateMachine gatt;


	//This is a cheap and cheerful plotting system using gnuplot.
	//Ignore this if you don't care about plotting.
	cplot::Plotter plot;
	plot.range = " [ ] [0:1000] ";
	deque<int> points;
	

	int prev_count=-1;
	////////////////////////////////////////////////////////////////////////////////	
	//
	// This is important! This is an example of a callback which responds to 
	// notifications or indications. Currently, BLEGATTStateMachine responds 
	// automatically to indications. Maybe that will change.
	//
	//Function that reads an indication and formats it for plotting.
	int c=0;
	double prev_t = get_time_of_day();
	std::function<void(const PDUNotificationOrIndication&)> notify_cb = [&](const PDUNotificationOrIndication& n)
	{
		//This particular device sends 16 bit integers.
		//Extract them and both print them in binary and send them to the plotting program
		const uint8_t* d = n.value().first;
		int val = ((0+d[1] *256 + d[0])>>0) ;

		cout << "Hello: "  << dec  << setfill('0') << setw(6) << val << dec << " ";
		bin(d[1]);
		cout << " ";
		bin(d[0]);
	
		//After the 16 bit int, it sends a 32 bit int
		int32_t count, adc_count, adc_resp;
		count = d[2] | (d[3]<<8) | (d[4]<<16) | (d[5]<<24);
		adc_count = d[6] | (d[7]<<8) | (d[8]<<16) | (d[9]<<24);
		adc_resp = d[10] | (d[11]<<8) | (d[12]<<16) | (d[13]<<24);


		cout << " count: " << count << " adccount: " << adc_count/14 << " " << adc_count << " " << adc_resp;

		if(count != prev_count+1)
			cout << " MISSED PACKET!!";

		prev_count = count;
		
		c++;
		if(c> 20)
		{
			double t = get_time_of_day();
			cout << " " << 1/((t - prev_t)/c) << " packets / s";

			prev_t = t;
			c=0;
		}

		cout << endl;

		//Format the points and send the results to the plotting program.
		points.push_back(val);
		if(points.size() > 100)
			points.pop_front();
		
		plot.newline("line lw 10 lt 0 title \"" + static_cast<ostringstream&>(ostringstream().flush()<<count).str() + "\"" );
		plot.addpts(points);

		plot.draw();
	};
	

	////////////////////////////////////////////////////////////////////////////////
	//
	// This is important! This is an example of a callback which is run when the 
	// client characteristic configuration is retreived. Essentially this is when
	// all the most useful device information has been received and the device can
	// now be used.
	//
	// At this point you need to search for things to activate. The code here activates
	// notifications on a device I have. You will need to modify this!
	//
	// Search for the service and attribute and set up notifications and the appropriate callback.
	std::function<void()> cb = [&gatt, &notify_cb](){

		pretty_print_tree(gatt);

		for(auto& service: gatt.primary_services)
			for(auto& characteristic: service.characteristics)
				if(service.uuid == UUID("7309203e-349d-4c11-ac6b-baedd1819764") && characteristic.uuid == UUID("53f72b8c-ff27-4177-9eee-30ace844f8f2"))
				{
					characteristic.cb_notify_or_indicate = notify_cb;
					characteristic.set_notify_and_indicate(true, false);
				}
	};
	
	////////////////////////////////////////////////////////////////////////////////
	//
	// This is somewhat important.  Set up callback for disconnection
	//
	// All reasonable errors are handled by a disconnect. The BLE spec specifies that
	// if the device sends invalid data, then the client must disconnect.
	//
	// Failure to connect also comes here.
	gatt.cb_disconnected = [](BLEGATTStateMachine::Disconnect d)
	{
		cerr << "Disconnect for reason " << BLEGATTStateMachine::get_disconnect_string(d) << endl;
		exit(1);
	};
	

	////////////////////////////////////////////////////////////////////////////////
	//
	// You almost always want to query the tree of things on the entire device
	// So, there is a function to do this automatically. This is a helper which
	// sets up all the callbacks necessary to automate the scanning. You could 
	// reduce connection times a little bit by only scanning for soma attributes.
	gatt.setup_standard_scan(cb);

	
	
	////////////////////////////////////////////////////////////////////////////////
	//
	// There are two modes, blocking and nonblocking.
	//
	// Blocking is useful for simple commandline programs which just log a bunch of 
	// data from a BLE device.
	//
	// Nonblocking is useful for everything else.
	// 
	
	// A few errors are handled by exceptions. std::runtime errors happen if nearly fatal
	// but recoverable-with-effort errors happen, such as a failure in socket allocation.
	// It is very unlikely you will encounter a runtime error.
	//
	// std::logic_error happens if you abuse the BLEGATTStateMachine. For example trying
	// to issue a new instruction before the callback indicating the in progress one has 
	// finished has been called. These errors mean the program is incorrect.
	try
	{ 
		if(argc >2 && argv[2] == string("nonblocking"))
		{
			
			//This is how to use the blocking interface. It is very simple.
			gatt.connect_blocking(argv[1]);
			for(;;)
			{
				gatt.read_and_process_next();
			}
		}
		else
		{
			//Connect as a non blocking call
			gatt.connect_nonblocking(argv[1]);


			
			//Example of how to use the state machine with select()
			//
			//This just demonstrates the capability and should be easily
			//transferrable to poll(), epoll(), libevent and so on.
			fd_set write_set, read_set;

			for(int i=0;;i++)
			{
				FD_ZERO(&read_set);
				FD_ZERO(&write_set);

				//Reads are always a possibility due to asynchronus notifications.
				FD_SET(gatt.socket(), &read_set);
				
				//Writes are usually available, so only check for them when the 
				//state machine wants to write.
				if(gatt.wait_on_write())
					FD_SET(gatt.socket(), &write_set);
					
				
				struct timeval tv;
				tv.tv_sec = 0;
				tv.tv_usec = 10000;
				int result = select(gatt.socket() + 1, &read_set, &write_set, NULL, & tv);

				if(FD_ISSET(gatt.socket(), &write_set))
					gatt.write_and_process_next();

				if(FD_ISSET(gatt.socket(), &read_set))
					gatt.read_and_process_next();

				cout << throbber(i) << flush;

			}
		}
	}
	catch(std::runtime_error e)
	{
		cerr << "Something's stopping bluetooth working: " << e.what() << endl;
	}
	catch(std::logic_error e)
	{
		cerr << "Oops, someone fouled up: " << e.what() << endl;
	}

}
