#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <cerrno>
#include <unistd.h>
#include <X11/Xlib.h>
#include <sys/mman.h>

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

	fprintf(stdout, "width: %d\n", width);
	fprintf(stdout, "height: %d\n", height);
	fprintf(stdout, "depth: %d\n", depth);
	fprintf(stdout, "red-mask: %ld\n", visual->red_mask);
	fprintf(stdout, "green-mask: %ld\n", visual->green_mask);
	fprintf(stdout, "blue-mask: %ld\n", visual->blue_mask);
	fprintf(stdout, "red-shift: %ld\n", red_shift);
	fprintf(stdout, "green-shift: %ld\n", green_shift);
	fprintf(stdout, "blue-shift: %ld\n", blue_shift);

	// TODO: bound sonic at a fixed framerate

	// plane mask tells that we care about all the bits that define color RRGGBB
	int const format = ZPixmap;
	uint64_t const plane_mask = 0xffffff;
	XImage *img = XGetImage(display, window, 0, 0, width, height, plane_mask, format);

	char *data = img->data;
	uint64_t const pitch = img->bytes_per_line;
	uint64_t const pixels = (width * height);
	uint64_t const bytes = (img->bits_per_pixel >> 3) * pixels;
	// even for 24-bit depth visuals images are usually stored with 32-bit padding
	if (32 != img->bits_per_pixel) {
		fprintf(stderr, "%s\n", "error: unexpected pixel depth");
		XCloseDisplay(display);
		_exit(1);
	}

	errno = 0;
	void *vpart = mmap(NULL, bytes, PROT_WRITE | PROT_READ, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
	if (MAP_FAILED == vpart) {
		if (errno) {
			fprintf(stderr, "%s\n", strerror(errno));
		}
		XCloseDisplay(display);
		_exit(1);
	}

	errno = 0;
	int rc = madvise(vpart, bytes, MADV_WILLNEED);
	if (-1 == rc) {
		if (errno) {
			fprintf(stderr, "%s\n", strerror(errno));
		}
		XCloseDisplay(display);
		_exit(1);
	}

	// initializes the partition array for the clustering algorithm
	int32_t *part = (int32_t*) vpart;
	memset(part, 0xff, bytes);
	for (int y = 0; y != height; ++y) {
		uint32_t *frame = (uint32_t*) data;
		for (int x = 0; x != width; ++x) {
			uint32_t rgb = frame[x];
			uint64_t r = ((visual->red_mask & rgb) >> red_shift);
			uint64_t g = ((visual->green_mask & rgb) >> green_shift);
			uint64_t b = ((visual->blue_mask & rgb) >> blue_shift);
			// shows coordinates and pixel values that probably belong to sonic
			if ((r == 0x00) && (g == 0x00) && (b >= 0x80 && b < 0xf0)) {
				if (y > 0) {
					uint32_t rgb = ((uint32_t*)(data - pitch))[x];
					uint64_t r = ((visual->red_mask & rgb) >> red_shift);
					uint64_t g = ((visual->green_mask & rgb) >> green_shift);
					uint64_t b = ((visual->blue_mask & rgb) >> blue_shift);
					if ((r == 0x00) && (g == 0x00) && (b >= 0x80 && b < 0xf0)) {
						int32_t id = ((y - 1) * height + x);
						if (*(part + id) < 0) {
							*(part + y * height + x) = id;
							*(part + id) -= 1;
						}
						else {
							int32_t root = *(part + id);
							if (*(part + root) >= 0) {
								fprintf(stderr, "%s\n", "error: clustering logic");
								XCloseDisplay(display);
								_exit(1);
							}
							*(part + y * height + x) = root;
							*(part + root) -= 1;
						}
					}
					else if (x > 0) {
						uint32_t rgb = frame[x - 1];
						uint64_t r = ((visual->red_mask & rgb) >> red_shift);
						uint64_t g = ((visual->green_mask & rgb) >> green_shift);
						uint64_t b = ((visual->blue_mask & rgb) >> blue_shift);
						if ((r == 0x00) && (g == 0x00) && (b >= 0x80 && b < 0xf0)) {
							int32_t id = (y * height + (x - 1));
							if (*(part + id) < 0) {
								*(part + id) -= 1;
								*(part + y * height + x) = id;
							}
							else {
								int32_t root = *(part + id);
								if (*(part + root) >= 0) {
									fprintf(stderr, "%s\n", "error: clustering logic");
									XCloseDisplay(display);
									_exit(1);
								}
								*(part + y * height + x) = root;
								*(part + root) -= 1;
							}
						}
					}
				}
				else if (x > 0) {
					uint32_t rgb = frame[x - 1];
					uint64_t r = ((visual->red_mask & rgb) >> red_shift);
					uint64_t g = ((visual->green_mask & rgb) >> green_shift);
					uint64_t b = ((visual->blue_mask & rgb) >> blue_shift);
					if ((r == 0x00) && (g == 0x00) && (b >= 0x80 && b < 0xf0)) {
						int32_t id = (y * height + (x - 1));
						if (*(part + id) < 0) {
							*(part + id) -= 1;
							*(part + y * height + x) = id;
						}
						else {
							int32_t root = *(part + id);
							if (*(part + root) >= 0) {
								fprintf(stderr, "%s\n", "error: clustering logic");
								XCloseDisplay(display);
								_exit(1);
							}
							*(part + y * height + x) = root;
							*(part + root) -= 1;
						}
					}
				}
			}
		}
		data += pitch;
	}

	uint32_t clusters = 0;
	// TODO: look back in both x and y to merge nearby clusters, you will have to
	//       identify a suitable distance for merging by experimentation. Note that
	//       by looking back you make sure that on merge you use the id of the
	//       preceeding cluster (higher rank). If you get a single large cluster
	//       for your game you have nailed it for that case. For real sonic games
	//       you will probably have to define a cluster size as well to discard
	//       for example the sky on some levels.
	for (int y = 0; y != height; ++y) {
		for (int x = 0; x != width; ++x) {
			int id = height * y + x;
			if (part[id] < -1) {
				// shows coordinates that probably belong to sonic
				fprintf(stdout, "count: %d x: %d y: %d\n", -part[id], x, y);
				++clusters;
			}
		}
	}
	fprintf(stdout, "clusters: %d\n", clusters);

	XCloseDisplay(display);
	return 0;
}
