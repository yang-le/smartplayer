#include <stdio.h>
#include <unistd.h>
#include <libavformat/avformat.h>
#include "debug.h"

#define ARG_REQ(x) #x":"
#define ARG_OPT(x) #x"::"

static AVFormatContext *fmt_ctx = NULL;

static char* parse_args(int argc, char *argv[])
{
	int opt = 0;
	char *infile = NULL;
	
	while ((opt = getopt(argc, argv, ARG_REQ(i)"hv")) != -1) {
		switch (opt) {
		case 'i':
			infile = optarg;
			break;
		case 'h':
			printf("show help info here\n");
			break;
		case 'v':
			printf("show version info here\n");
			break;
		default:
			break;
		}
	}

	if (!infile) {
		infile = argv[optind];
	}

	return infile;
}

int main(int argc, char *argv[])
{
	int ret = 0;
	char *infile = NULL;
	
	debug_info("====smart player====\n");

	infile = parse_args(argc, argv);

	if (!infile) {
		fprintf(stderr, "you must provide a input file\n");
		return 1;
	}

	debug_info("the input file is %s\n", infile);

	/* register all formats and codecs */
	av_register_all();
	avformat_network_init();

	/* open input file, and allocate format context */
	if (avformat_open_input(&fmt_ctx, infile, NULL, NULL) < 0) {
		fprintf(stderr, "Could not open source file %s\n", infile);
		return 1;
	}

	/* retrieve stream information */
	if (avformat_find_stream_info(fmt_ctx, NULL) < 0) {
		fprintf(stderr, "Could not find stream information\n");
		ret = 1;
		goto end;
	}

	/* dump input information to stderr */
	av_dump_format(fmt_ctx, 0, infile, 0);

end:
	avformat_close_input(&fmt_ctx);

	return ret;
}

