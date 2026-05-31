#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <cerrno>
#include <unistd.h>
#include <X11/Xlib.h>

int main(int argc, char *argv[])
{
	// Complains if the user does not invoke the code with the window resource id
	// of course we could do better by having the user click on the window to get
	// its resource id; however, now it is not the time to write fancy code but
	// functional code for prototyping
	if (3 != argc) {
		fprintf(stderr, "usage: %s %s\n", argv[0], "--window XID");
		_exit(1);
	}

	Window window = 0;
	// gets the window resource ID from the command-line 
	for (int i = 0; i != argc; ++i) {
		if (!strcmp(argv[i], "--window")) {

			errno = 0;
			char *endptr = NULL;
			window = strtol(argv[i + 1], &endptr, 10);
			if (!endptr) {
				fprintf(stderr, "%s\n", "error: unexpected conversion error");
				if (errno) {
					fprintf(stderr, "%s\n", strerror(errno));
				}
				_exit(1);
			}
			else if (endptr[0]) {
				fprintf(stderr, "%s\n", "error: invalid character found in Window ID (XID)");
				if (errno) {
					fprintf(stderr, "%s\n", strerror(errno));
				}
				_exit(1);
			}

			if (!window) {
				fprintf(stderr, "%s\n", "error: invalid window id");
				_exit(1);
			}
		}
	}

	// open a connection to the X11 server to get the attributes of the window where
	// the sonic game is running
	Display *display = XOpenDisplay(NULL);
	if (!display) {
		fprintf(stderr, "usage: %s\n", "error: failed to open display");
		_exit(1);
	}

	XWindowAttributes attributes = {};
	XGetWindowAttributes(display, window, &attributes);
//	int const x = attributes.x;
//	int const y = attributes.y;
	int const width = attributes.width;
	int const height = attributes.height;
	int const depth = attributes.depth;
	Visual *visual = attributes.visual;

	uint64_t iters = 0;
	uint64_t red_shift = 0;
	uint64_t green_shift = 0;
	uint64_t blue_shift = 0;
	uint64_t const rgb_mask = 0xff;
	while ((rgb_mask << red_shift) != visual->red_mask) {
		red_shift += 8LU;
		if (iters > 2) {
			fprintf(stderr, "%s\n", "error: unexpected visual endianess");
			XCloseDisplay(display);
			_exit(1);
		}
		++iters;
	}

	iters = 0;
	while ((rgb_mask << green_shift) != visual->green_mask) {
		green_shift += 8LU;
		if (iters > 2) {
			fprintf(stderr, "%s\n", "error: unexpected visual endianess");
			XCloseDisplay(display);
			_exit(1);
		}
		++iters;
	}

	iters = 0;
	while ((rgb_mask << blue_shift) != visual->blue_mask) {
		blue_shift += 8LU;
		if (iters > 2) {
			fprintf(stderr, "%s\n", "error: unexpected visual endianess");
			XCloseDisplay(display);
			_exit(1);
		}
		++iters;
	}

//	fprintf(stdout, "x: %d\n", x);
//	fprintf(stdout, "y: %d\n", y);
	fprintf(stdout, "width: %d\n", width);
	fprintf(stdout, "height: %d\n", height);
	fprintf(stdout, "depth: %d\n", depth);
	fprintf(stdout, "red-mask: %ld\n", visual->red_mask);
	fprintf(stdout, "green-mask: %ld\n", visual->green_mask);
	fprintf(stdout, "blue-mask: %ld\n", visual->blue_mask);
	fprintf(stdout, "red-shift: %ld\n", red_shift);
	fprintf(stdout, "green-shift: %ld\n", green_shift);
	fprintf(stdout, "blue-shift: %ld\n", blue_shift);

	// TODO: get the current frame of the game at a fixed framerate

	// plane mask tells that we care about all the bits that define color RRGGBB
	int const format = ZPixmap;
	uint64_t const plane_mask = 0xffffff;
	XImage *img = XGetImage(display, window, 0, 0, width, height, plane_mask, format);

	char *data = img->data;
	uint64_t const pitch = img->bytes_per_line;
	for (int y = 0; y != height; ++y) {
		uint32_t *frame = (uint32_t*) data;
		for (int x = 0; x != width; ++x) {
			uint32_t rgb = frame[x];
			uint64_t r = ((visual->red_mask & rgb) >> red_shift);
			uint64_t g = ((visual->green_mask & rgb) >> green_shift);
			uint64_t b = ((visual->blue_mask & rgb) >> blue_shift);
			// shows coordinates and pixel values that probably belong to sonic
			if ((r == 0x00) && (g < 0x80) && (b >= 0x80)) {
				fprintf(stdout, "x: %d y: %d red: %lx green: %lx blue: %lx\n", x, y, r, g, b);
			}
		}
		data += pitch;
	}

	XCloseDisplay(display);
	return 0;
}
