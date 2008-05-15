/*

	silentjack.c
	Silence/dead air detector for JACK
	Copyright (C) 2006  Nicholas J. Humfrey
	
	This program is free software; you can redistribute it and/or
	modify it under the terms of the GNU General Public License
	as published by the Free Software Foundation; either version 2
	of the License, or (at your option) any later version.
	
	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.
	
	You should have received a copy of the GNU General Public License
	along with this program; if not, write to the Free Software
	Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/

#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <jack/jack.h>
#include <getopt.h>
#include "config.h"
#include "db.h"


#define DEFAULT_CLIENT_NAME		"silentjack"


// *** Globals ***
jack_port_t *input_port = NULL;		// Our single jack input port
float peak = 0.0f;					// Current peak signal level (linear)
int running = 1;					// SilentJack keeps running while true
int quiet = 0;						// If true, don't send messages to stdout
int verbose = 0;					// If true, send more messages to stdout



/* Read and reset the recent peak sample */
static
float read_peak()
{
	float peakdb = lin2db(peak);
	peak = 0.0f;

	return peakdb;
}


/* Callback called by JACK when audio is available.
   Stores value of peak sample */
static
int process_peak(jack_nframes_t nframes, void *arg)
{
	jack_default_audio_sample_t *in;
	unsigned int i;

	/* just incase the port isn't registered yet */
	if (input_port == NULL) {
		return 0;
	}

	/* get the audio samples, and find the peak sample */
	in = (jack_default_audio_sample_t *) jack_port_get_buffer(input_port, nframes);
	for (i = 0; i < nframes; i++) {
		const float s = fabs(in[i]);
		if (s > peak) {
			peak = s;
		}
	}


	return 0;
}




/* Connect the chosen port to ours */
static
void connect_jack_port( jack_client_t *client, jack_port_t *port, const char* out )
{
	const char* in = jack_port_name( port );
	int err;
		
	if (!quiet) printf("Connecting %s to %s\n", out, in);
	
	if ((err = jack_connect(client, out, in)) != 0) {
		fprintf(stderr, "connect_jack_port(): failed to jack_connect() ports: %d\n",err);
		exit(1);
	}
}


static
void shutdown_callback_jack(void *arg)
{
	running = 0;
}

static
jack_client_t* init_jack( const char * client_name, const char* connect_port ) 
{
	jack_status_t status;
	jack_options_t options = JackNoStartServer;
	jack_client_t *client = NULL;

	// Register with Jack
	if ((client = jack_client_open(client_name, options, &status)) == 0) {
		fprintf(stderr, "Failed to start jack client: %d\n", status);
		exit(1);
	}
	if (!quiet) printf("JACK client registered as '%s'.\n", jack_get_client_name( client ) );

	// Create our pair of output ports
	if (!(input_port = jack_port_register(client, "in", JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput, 0))) {
		fprintf(stderr, "Cannot register input port 'in'.\n");
		exit(1);
	}
	
	// Register shutdown callback
	jack_on_shutdown (client, shutdown_callback_jack, NULL );

	// Register the peak audio callback
	jack_set_process_callback(client, process_peak, 0);

	// Activate the client
	if (jack_activate(client)) {
		fprintf(stderr, "Cannot activate client.\n");
		exit(1);
	}
	
	// Connect up our input port ?
	if (connect_port) {
		connect_jack_port( client, input_port, connect_port );
	}
	
	return client;
}


static
void finish_jack( jack_client_t *client )
{
	// Leave the Jack graph
	jack_client_close(client);
}


static
void run_command( int argc, char* argv[] )
{
	pid_t child;
	int status;
	
	// No command to execute
	if (argc<1) return;
	
	// Exit successfully if command is called "exit"
	if (argc==1 && strcmp(argv[0], "exit")==0) exit(0);

	// Fork new process
	child = fork();
	if (child==0) {
		// Child process here
		if (execvp( argv[0], argv )) {
			perror("execvp failed");
			exit(-1);
		}
	} else if (child==-1) {
		// Fork failed
		perror("fork failed");
		exit(-1);
	}
	
	// Wait for process to end
	if (waitpid( child, &status, 0)==-1) {
		perror("waitpid failed");
	}
}


/* Display how to use this program */
static
void usage()
{
	printf("%s version %s\n\n", PACKAGE_NAME, PACKAGE_VERSION);
	printf("Usage: silentjack [options] [COMMAND [ARG]...]\n");
	printf("Options:  -c <port>   Connect to this port\n");
	printf("          -n <name>   Name of this client (default 'silentjack')\n");
	printf("          -l <db>     Trigger level (default -40 decibels)\n");
	printf("          -p <secs>   Period of silence required (default 1 second)\n");
	printf("          -d <db>     No-dynamic trigger level (default disabled)\n");
	printf("          -P <secs>   No-dynamic period (default 10 seconds)\n");
	printf("          -g <secs>   Grace period (default 0 seconds)\n");
	printf("          -v          Enable verbose mode\n");
	printf("          -q          Enable quiet mode\n");
	exit(1);
}



int main(int argc, char *argv[])
{
	jack_client_t *client = NULL;
	const char* client_name = DEFAULT_CLIENT_NAME;
	const char* connect_port = NULL;
	float peakdb = 0.0f;			// The current peak signal level (in dB)
	float last_peakdb = 0.0f;		// The previous peak signal level (in dB)
	int silence_period = 1;			// Required period of silence for trigger
	int nodynamic_period = 10;		// Required period of no-dynamic for trigger
	int grace_period = 0;			// Period to wait before triggering again
	float silence_theshold = -40;	// Level considered silent (in dB)
	float nodynamic_theshold = 0;	// Minimum allowed delta between peaks (in dB)
	int silence_count = 0;			// Number of seconds of silence detected
	int nodynamic_count = 0;		// Number of seconds of no-dynamic detected
	int in_grace = 0;				// Number of seconds left in grace
	int opt;

	// Make STDOUT unbuffered
	setbuf(stdout, NULL);

	// Parse command line arguments
	while ((opt = getopt(argc, argv, "c:n:l:p:P:d:g:vqh")) != -1) {
		switch (opt) {
			case 'c': connect_port = optarg; break;
			case 'n': client_name = optarg; break;
			case 'l': silence_theshold = atof(optarg); break;
			case 'p': silence_period = fabs(atoi(optarg)); break;
			case 'd': nodynamic_theshold = atof(optarg); break;
			case 'P': nodynamic_period = atof(optarg); break;
			case 'g': grace_period = fabs(atoi(optarg)); break;
			case 'v': verbose = 1; break;
			case 'q': quiet = 1; break;
			case 'h':
			default:
				/* Show usage information */
				usage();
				break;
		}
	}
    argc -= optind;
    argv += optind;

	
	// Validate parameters
	if (quiet && verbose) {
    	fprintf(stderr, "Can't be quiet and verbose at the same time.\n");
    	usage();
	}

	// Initialise Jack
	client = init_jack( client_name, connect_port );
	
	
	// Main loop
	while (running) {
	
		// Sleep for 1 second
		usleep( 1000000 );
		
		// Are we in grace period ?
		if (in_grace) {
			in_grace--;
			if (verbose) printf("%d seconds left in grace period.\n", in_grace);
			continue;
		}

		// Check we are connected to something
		if (jack_port_connected(input_port)==0) {
			if (verbose) printf("Input port isn't connected to anything.\n");
			continue;
		}
	
	
		// Read the recent peak (in decibels)
		last_peakdb = peakdb;
		peakdb = read_peak();
		
		
		// Do silence detection?
		if (silence_theshold) {
			if (verbose) printf("peak: %2.2fdB", peakdb);
		
			// Is peak too low?
			if (peakdb < silence_theshold) {
				silence_count++;
				if (verbose) printf(" (%d seconds of silence)\n", silence_count);
			} else {
				if (verbose) printf(" (not silent)\n");
				silence_count=0;
			}
	
			// Have we had enough seconds of silence?
			if (silence_count >= silence_period) {
				if (!quiet) printf("**SILENCE**\n");
				run_command( argc, argv );
				silence_count = 0;
				in_grace = grace_period;
			}
			
		}
		
		
		// Do no-dynamic detection
		if (nodynamic_theshold) {
			
			if (verbose) printf("delta: %2.2fdB", fabs(last_peakdb-peakdb));
			
			// Check the dynamic/delta between peaks
			if (fabs(last_peakdb-peakdb) < nodynamic_theshold) {
				nodynamic_count++;
				if (verbose) printf(" (%d seconds of no dynamic)\n", nodynamic_count);
			} else {
				if (verbose) printf(" (dynamic)\n");
				nodynamic_count=0;
			}
	
			// Have we had enough seconds of no dynamic?
			if (nodynamic_count >= nodynamic_period) {
				if (!quiet) printf("**NO DYNAMIC**\n");
				run_command( argc, argv );
				nodynamic_count = 0;
				in_grace = grace_period;
			}
	
		}



		
	}


	// Clean up
	finish_jack( client );


	return 0;
}

