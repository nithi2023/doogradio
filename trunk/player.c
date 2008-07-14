#include <stdlib.h>  //added newly
#include <string.h> //also added
#include <unistd.h> //also

#include <stdio.h>
#include <sys/io.h>
#include <ffmpeg/avcodec.h>
#include <ffmpeg/avformat.h>
#include "lame/lame.h"
#include "./bassmod.h"

#include <ffmpeg/avcodec.h>
#include <ffmpeg/avformat.h>

#ifdef __MINGW32__
/* Required header file */
#include <fcntl.h>
#endif



FILE *announce_file = 0;
short *announce_data = 0;
int announce_len = 0;
double announce_pos = 0;
int input_samplerate = 44100;

// here is the worst .wav reading code ever
// oh by the way i hope your wav file is 16bit 44khz mono!
void load_announce_file() {
	int c;
	int want = 'd';
	int len = 0;
	while((c = fgetc(announce_file)) != EOF) {
		long int pos = ftell(announce_file);
		if(c == 'd' && fgetc(announce_file) == 'a' && fgetc(announce_file) == 't' && fgetc(announce_file) == 'a') {
			len =  fgetc(announce_file) |
				(fgetc(announce_file) << 8) |
				(fgetc(announce_file) << 16) |
				(fgetc(announce_file) << 24);
			break;
		}
		else {
			fseek(announce_file, pos, SEEK_SET);
		}
	}
	if(len > 90*1024*1024) { // don't allocate huge buffer in case it's broken
		announce_data = 0;
		announce_file = 0;
		announce_len = 0;	
		return;
	}
	announce_len = len;
	announce_data = malloc(announce_len);
	if(fread(announce_data, 1, len, announce_file) != len) {
		announce_data = 0;
		announce_file = 0;
		announce_len = 0;
	}
}

// This reencodes a file lavc can handle to 41000hz 2channels 96kbit mp3
int load_lavc( char* file_name ) {
	// Prepare lavc.
	av_register_all();
	
	// Prepare lame
	lame_global_flags* gfp;
	gfp = lame_init();
	lame_set_num_channels( gfp, 2 );
	lame_set_out_samplerate( gfp, 44100 );
	lame_set_brate( gfp, 96 );
	lame_set_mode( gfp, 1 );
	lame_set_bWriteVbrTag( gfp, 0 );
	
	// Open the file, grab the right stream. 
	AVFormatContext* file_info = NULL;
	if( av_open_input_file( &file_info, file_name, NULL, 0, NULL ) != 0 ) {
		fprintf( stderr, "Failed to open %s.\n", file_name );
		return( 0 );
	}
	if( av_find_stream_info( file_info ) < 0 ) {
		fprintf( stderr, "That's not a nice file.\n" );
		return( 0 );
	}
	// Could we just assume that in a normal audio file, we 
	// need stream 0? Probably...
	int stream_id = -1;
	int i = 0;
	for( i = 0; i < file_info->nb_streams; i++ ) {
		if( file_info->streams[ i ]->codec->codec_type == CODEC_TYPE_AUDIO ) {
		stream_id = i;
		break;
		}
	}
	if( stream_id == -1 ) {
		return( 0 );
	}
	AVCodecContext* audio_file = NULL;
	audio_file = file_info->streams[ stream_id ]->codec;
	
	// Open the appropriate codec.
	AVCodec* codec = NULL;
	codec = avcodec_find_decoder( audio_file->codec_id );
	if( codec == NULL ) {
		fprintf( stderr, "I have no idea what to do with this.\n" );
		return( 0 );
	}
	if( avcodec_open( audio_file, codec ) < 0 ) {
		fprintf( stderr, "Something blew.\n" );
		
		return( 0 );
	}
	
	// Set lame up for encoding
	lame_set_in_samplerate( gfp, audio_file->sample_rate );
	if( lame_init_params( gfp ) < 0 ) {
		return( 0 );
	}
	
	// Prepare a frame.
	AVFrame* frame = NULL;
	frame = avcodec_alloc_frame();
	if( frame == NULL ) {
		fprintf( stderr, "This should not have happened.\n" );
		return( 0 );
	}
	
	// Read one packet.
	AVPacket packet;
	int last_packet = 0;
	do {
		do{
		if( av_read_frame( file_info, &packet ) < 0 ) {
			last_packet = 1;
		}
		if( packet.stream_index != stream_id ) {
			av_free_packet( &packet );
		}
		} while( packet.stream_index != stream_id );
		
		// We now have a packet. Grab the frames.
		int bytes_remaining = packet.size;
		int decoded_size = AVCODEC_MAX_AUDIO_FRAME_SIZE;
		
		uint8_t* decoded_data = NULL;
		decoded_data = malloc( AVCODEC_MAX_AUDIO_FRAME_SIZE );
		
		uint8_t* encoded_data = NULL;
		encoded_data = packet.data;
		
		char mp3buffer[ 8192 ];
		
		// Decode, reencode frame
		while( bytes_remaining > 0 ) {
			int frame_length = -1;
			decoded_size = AVCODEC_MAX_AUDIO_FRAME_SIZE;
			frame_length = avcodec_decode_audio(
				audio_file,
				(int16_t*)decoded_data,
				&decoded_size,
				encoded_data,
				bytes_remaining
			);
			bytes_remaining -= frame_length;
			encoded_data += frame_length;
			
			int mp3len = 0;
			if( audio_file->channels == 2 ) {
				mp3len = lame_encode_buffer_interleaved(
					gfp,
					(short*)decoded_data,
					decoded_size / ( sizeof(int16_t) * 2 ),
					mp3buffer,
					4096
				);
			}
			else {
				mp3len = lame_encode_buffer(
					gfp,
					(short*)decoded_data,
					(short*)decoded_data,
					decoded_size / ( sizeof(int16_t) ),
					mp3buffer,
					4096
				);
			}
			fwrite( mp3buffer, 1, mp3len, stdout );
		}
		
		free( decoded_data );
		av_free_packet( &packet );
	} while( !last_packet );
	
	lame_close(gfp);

	fprintf( stderr, "channels: %i",  audio_file->channels  );

	return( 1 );
}

int load(char *filename) {
	char mp3buffer[8192];
	lame_global_flags *gfp;
	if(!BASSMOD_Init(-3, 44100, 0)) return 1;
	if(!BASSMOD_MusicLoad(0, filename, 0, 0, BASS_MUSIC_RAMPS | BASS_MUSIC_NONINTER | BASS_MUSIC_STOPBACK)) {
		return( load_lavc( filename ) );
		
	}
	if(isatty(fileno(stdout))) return 1;
#ifdef __MINGW32__
	/* Switch to binary mode */
	_setmode(_fileno(stdout),_O_BINARY);
#endif	
	BASSMOD_MusicPlay();
	gfp = lame_init();
	lame_set_num_channels(gfp,2);
	
	lame_set_in_samplerate(gfp,44100);
	lame_set_out_samplerate(gfp,44100);
	lame_set_brate(gfp,96);
	lame_set_mode(gfp,1);
	lame_set_bWriteVbrTag(gfp,0);
	if(lame_init_params(gfp) < 0)
		return 1;
	
	while(BASSMOD_MusicIsActive() == BASS_ACTIVE_PLAYING) {
		int c; int i; short bassbuffer[300*2];
		short leftbuffer[300], rightbuffer[300];
		// bass is interlaced, lame isn't
		BASSMOD_MusicDecode(bassbuffer, 300*2*2);
		for(i=0;i<600;i++) {
			if(i & 1)
				rightbuffer[i >> 1] = bassbuffer[i];
			else
				leftbuffer[i >> 1] = bassbuffer[i];
		}
		c = lame_encode_buffer(gfp, leftbuffer, rightbuffer, 300, mp3buffer, 4096);
		if(c != fwrite(mp3buffer, 1, c, stdout))
			goto DONE;
	}
	{ int c;
	c = lame_encode_flush(gfp, mp3buffer, 8192);
	fwrite(mp3buffer, 1, c, stdout);
	}
	DONE:
	lame_close(gfp);
}

int usage(char* appname) {
	printf("usage: %s filename [announce]", appname);
	return 1;
}

int main(int argc, char *argv[]) {
	if(argc == 1)
		return usage(argv[0]);
	if(argc == 2)
		return load(argv[1]);
	if(argc == 3) {
		announce_file = fopen(argv[2], "rb");
		if(!announce_file) return 1;
		load_announce_file();
		return load(argv[1]);
	}
	return usage(argv[0]);
}
