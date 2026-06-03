#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <cerrno>
#include <unistd.h>
#include <X11/Xlib.h>
#include <sys/mman.h>

#define BLUE_DIST_MERGE 256L
#define BLUE_MASK_SONIC 1L
// TODO: consider working with a packed RGB parameter instead
#define Blue(r, g, b) (((r) == 0x00) && ((g) == 0x00) && (((b) >= 0x80) && ((b) < 0xf0)))
#define Dist(x1, x2) (((x1) - (x2)) * ((x1) - (x2)))
#define Merged(d) ((d) < BLUE_DIST_MERGE)

typedef char unsigned byte_t;
typedef int64_t CID;

extern "C" struct cluster {
	int64_t mask;
	int64_t root;
	int64_t node;
	int64_t prev;
	int64_t next;
	int64_t size;
	int64_t id;
	int64_t x;
	int64_t y;
	int64_t _pad[7];
};

static_assert(128 == sizeof(struct cluster));

// clusters (or groups) nodes that belong to Sonic
extern "C" int Clustering(
		int32_t * const part,
		int32_t const * const frame,
		int64_t const red_mask,
		int64_t const green_mask,
		int64_t const blue_mask,
		int64_t const red_shift,
		int64_t const green_shift,
		int64_t const blue_shift,
		int64_t const width,
		int64_t const x,
		int64_t const y
) {
	int32_t const rgb = frame[x];
	int64_t const r = ((red_mask & rgb) >> red_shift);
	int64_t const g = ((green_mask & rgb) >> green_shift);
	int64_t const b = ((blue_mask & rgb) >> blue_shift);
	if (Blue(r, g, b)) {
		if (x > 0) {
			int32_t const rgb = frame[x - 1];
			int64_t const r = ((red_mask & rgb) >> red_shift);
			int64_t const g = ((green_mask & rgb) >> green_shift);
			int64_t const b = ((blue_mask & rgb) >> blue_shift);
			if (Blue(r, g, b)) {
				int32_t const id = (y * width + (x - 1));
				if (*(part + id) < 0) {
					*(part + id) -= 1;
					*(part + y * width + x) = id;
				}
				else {
					int32_t const root = *(part + id);
					if (*(part + root) >= 0) {
						goto err_cluster;
					}
					*(part + y * width + x) = root;
					*(part + root) -= 1;
				}
			}
		}
	}

	return 0;

err_cluster:
	{
		// NOTE:
		// If the partition value for a root node is positive that means that
		// there is a logic error because root-nodes only store counts and
		// these are negative values to differentiate them easily from node ids.
		fprintf(stderr, "%s\n", "error: clustering logic");
		return -1;
	}
}

extern "C" void Merge(
		struct cluster * const curr,
		struct cluster * const next,
		struct cluster * const clusters
) {
	struct cluster *iter = &clusters[curr->next];
	while (iter->next != iter->id) {
		if (BLUE_MASK_SONIC != iter->mask) {
			fprintf(stderr, "%s\n", "error: mask");
			//XCloseDisplay(display);
			_exit(1);
		}
		iter = &clusters[iter->next];
	}
	iter->next = next->id;
	next->prev = iter->id;
}

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
	int64_t const width = attributes.width;
	int64_t const height = attributes.height;
	int64_t const depth = attributes.depth;
	Visual *visual = attributes.visual;

	int64_t iters = 0;
	int64_t red_shift = 0;
	int64_t green_shift = 0;
	int64_t blue_shift = 0;
	int64_t const rgb_mask = 0xff;
	int64_t const red_mask = visual->red_mask;
	int64_t const green_mask = visual->green_mask;
	int64_t const blue_mask = visual->blue_mask;
	while ((rgb_mask << red_shift) != red_mask) {
		red_shift += 8LU;
		if (iters > 2) {
			fprintf(stderr, "%s\n", "error: unexpected visual endianess");
			XCloseDisplay(display);
			_exit(1);
		}
		++iters;
	}

	iters = 0;
	while ((rgb_mask << green_shift) != green_mask) {
		green_shift += 8LU;
		if (iters > 2) {
			fprintf(stderr, "%s\n", "error: unexpected visual endianess");
			XCloseDisplay(display);
			_exit(1);
		}
		++iters;
	}

	iters = 0;
	while ((rgb_mask << blue_shift) != blue_mask) {
		blue_shift += 8LU;
		if (iters > 2) {
			fprintf(stderr, "%s\n", "error: unexpected visual endianess");
			XCloseDisplay(display);
			_exit(1);
		}
		++iters;
	}

	fprintf(stdout, "width: %ld\n", width);
	fprintf(stdout, "height: %ld\n", height);
	fprintf(stdout, "depth: %ld\n", depth);
	fprintf(stdout, "red-mask: %ld\n", visual->red_mask);
	fprintf(stdout, "green-mask: %ld\n", visual->green_mask);
	fprintf(stdout, "blue-mask: %ld\n", visual->blue_mask);
	fprintf(stdout, "red-shift: %ld\n", red_shift);
	fprintf(stdout, "green-shift: %ld\n", green_shift);
	fprintf(stdout, "blue-shift: %ld\n", blue_shift);

	// TODO: bound sonic at a fixed framerate

	// plane mask tells that we care about all the bits that define color RRGGBB
	int const format = ZPixmap;
	int64_t const plane_mask = 0xffffff;
	XImage *img = XGetImage(display, window, 0, 0, width, height, plane_mask, format);

	char *data = img->data;
	int64_t const pitch = img->bytes_per_line;
	int64_t const pixels = (width * height);
	int64_t const bytes_frame = (img->bits_per_pixel >> 3) * pixels;
	int64_t const bytes_partition = bytes_frame;
	// even for 24-bit depth visuals images are usually stored with 32-bit padding
	if (32 != img->bits_per_pixel) {
		fprintf(stderr, "%s\n", "error: unexpected pixel depth");
		XCloseDisplay(display);
		_exit(1);
	}

	errno = 0;
	int64_t rc = sysconf(_SC_PAGESIZE);
	if (-1 == rc) {
		if (errno) {
			fprintf(stderr, "%s\n", strerror(errno));
		}
		XCloseDisplay(display);
		_exit(1);
	}

	int64_t const pagesz = rc;
	int64_t const mask_page = (pagesz - 1);
	struct cluster clust = {};
	struct cluster *clusp = &clust;
	int64_t const bytes_cluster_list = pixels * sizeof(CID);
	int64_t const bytes_clusters = pixels * sizeof(*clusp);
	int64_t const bytes_required = (
			bytes_partition +
			bytes_clusters +
			bytes_cluster_list +
			0
	);
	int64_t const bytes_mmap = (((bytes_required + mask_page) & (~mask_page)) << 1);

	errno = 0;
	void *base = mmap(NULL, bytes_mmap, PROT_WRITE | PROT_READ, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
	if (MAP_FAILED == base) {
		if (errno) {
			fprintf(stderr, "%s\n", strerror(errno));
		}
		XCloseDisplay(display);
		_exit(1);
	}

	errno = 0;
	rc = madvise(base, bytes_mmap, MADV_WILLNEED);
	if (-1 == rc) {
		if (errno) {
			fprintf(stderr, "%s\n", strerror(errno));
		}
		XCloseDisplay(display);
		_exit(1);
	}

	// initializes the partition array for the clustering algorithm
	int64_t const offset_partition = 0;
	int64_t const offset_clusters = ((bytes_partition + 0x3f) & ~0x3f);
	int64_t const offset_cluster_list = (
		((offset_clusters + bytes_clusters) + 0x3f) & ~0x3f
	);
	int32_t *part = (typeof(part)) (((byte_t*) base) + offset_partition);
	struct cluster *clusters = (typeof(clusters)) (((byte_t*) base) + offset_clusters);
	data = img->data;
	// PERF: don't mind doing multiple passes on the framebuffer while writing experimental code so bear it with me
	for (int64_t y = 0; y != height; ++y) {
		int32_t *frame = (int32_t*) data;
		for (int64_t x = 0; x != width; ++x) {
			int64_t id = width * y + x;
			struct cluster *cluster = &clusters[id];
			int32_t const rgb = frame[x];
			int64_t const r = ((red_mask & rgb) >> red_shift);
			int64_t const g = ((green_mask & rgb) >> green_shift);
			int64_t const b = ((blue_mask & rgb) >> blue_shift);
			cluster->mask = ((Blue(r, g, b))? BLUE_MASK_SONIC : 0);
			cluster->root = id;
			cluster->node = id;
			cluster->prev = id;
			cluster->next = id;
			cluster->size = 1;
			cluster->id = id;
			cluster->x = x;
			cluster->y = y;
		}
		data += pitch;
	}

	data = img->data;
	memset(part, 0xff, bytes_partition);
	for (int64_t y = 0; y != height; ++y) {
		int32_t *frame = (int32_t*) data;
		for (int64_t x = 0; x != width; ++x) {
			rc = Clustering(
					part,
					frame,
					red_mask,
					green_mask,
					blue_mask,
					red_shift,
					green_shift,
					blue_shift,
					width,
					x,
					y
			);
			if (-1 == rc) {
				XCloseDisplay(display);
				_exit(1);
			}
		}
		data += pitch;
	}

	int64_t clno = 0;
	CID *cl = (typeof(cl)) (((byte_t*) base) + offset_cluster_list);
	memset(cl, 0, bytes_cluster_list);
	// links nodes of constant y-striped clusters
	for (int64_t i = 0; i != pixels; ++i) {
		struct cluster *cluster = &clusters[i];
		if ((BLUE_MASK_SONIC == cluster->mask) && (part[i] < 0)) {
			cluster->size = -(part[i]);
			int64_t const childno = (cluster->size - 1);
			cluster->node = (i + childno);
			for (int64_t j = 0; j != childno; ++j) {
				int64_t id = ((i + 1) + (childno - 1) - j);
				if (part[id] != i) {
					fprintf(stderr, "%s\n", "error: part");
					XCloseDisplay(display);
					_exit(1);
				}
				struct cluster *child = &clusters[id];
				if (BLUE_MASK_SONIC != child->mask) {
					fprintf(stderr, "%s\n", "error: unexpected partition error");
					XCloseDisplay(display);
					_exit(1);
				}
				child->node = (id - 1);
				child->root = i;
			}
			if ((1 == cluster->size) && (cluster->node != cluster->id)) {
				fprintf(stderr, "%s\n", "error: unexpected clustering error");
				XCloseDisplay(display);
				_exit(1);
			}
			cl[clno] = i;
			++clno;
		}
	}

	// check the links of the constant y-striped clusters
	for (int64_t id = 0; id != pixels; ++id) {
		struct cluster const * const cluster = &clusters[id];
		if (BLUE_MASK_SONIC != cluster->mask) {
			continue;
		}
		else if (cluster->size == 1) {
			continue;
		}
		int64_t count = 1;
		struct cluster const * child = &clusters[cluster->node];
		int64_t const childno = (cluster->size - 1);
		while (child->node != id) {
			child = &clusters[child->node];
			if (child->y != cluster->y) {
				fprintf(stderr, "%s\n", "error: striping");
				XCloseDisplay(display);
				_exit(1);
			}
			if (count >= childno) {
				fprintf(stderr, "%s\n", "error: clustering");
				XCloseDisplay(display);
				_exit(1);
			}
			++count;
		}
		if (count != childno) {
			fprintf(stderr, "%s\n", "error: cluster-list");
			XCloseDisplay(display);
			_exit(1);
		}
	}

	// TODO: check alignments of clusters, cluster_list, etc. (expect 64-byte align)

	int64_t merges = 0;
	if (clno < 2) {
		fprintf(stdout, "merged-clusters-count: %ld\n", merges);
		XCloseDisplay(display);
		_exit(0);
	}
	fprintf(stdout, "%s\n", "merging clusters");

	for (int64_t y = 0; y != height; ++y) {
		for (int64_t x = 0; x != width; ++x) {
			int quit = 0;
			int64_t const id = width * y + x;
			struct cluster *curr = &clusters[id];
			if (BLUE_MASK_SONIC != curr->mask) {
				continue;
			}
			else if (curr->root != curr->id) {
				continue;
			}
			int64_t const beg = 1 + curr->node;
			int64_t const end = width * (y + 1);
			if (beg > end) {
				fprintf(stderr, "%s\n", "error: ux out-of-bounds");
				XCloseDisplay(display);
				_exit(1);
			}
			for (int64_t ii = beg; ii != end; ++ii) {
				struct cluster *next = &clusters[ii];
				// TODO: you can safely delete this later
				if (curr->y != next->y) {
					fprintf(stderr, "%s\n", "error: ux count");
					XCloseDisplay(display);
					_exit(1);
				}
				else if (next->root == curr->id) {
					// this loop is not meant for traversing the nodes of the current cluster
					fprintf(stderr, "%s\n", "error: owned node");
					XCloseDisplay(display);
					_exit(1);
				}
				else if (BLUE_MASK_SONIC != next->mask) {
					continue;
				}
				else if (next->root != next->id) {
					// we should never reach a node of a cluster that has the sonic mask SET because we merge that cluster right away if it is in range (and to do that we don't even have to look at the nodes); now if we are here that means that we kept looking when we should have stopped because A: as before we don't need to look at the nodes to know if the cluster can be merged and because B: if we were too far we should not have kept looking beyond the defined range and because C: if not because of A or B then we have a problem in the core clustering or partitioning algorithm
					fprintf(stderr, "%s\n", "error: ux found node");
					XCloseDisplay(display);
					_exit(1);
				}
				else if (next->prev == curr->id) {
					// again we should not be here again because the algorithm is expected to move to the next cluster to try a new merge not repeat its work
					fprintf(stderr, "%s\n", "error: ux merge");
					XCloseDisplay(display);
					_exit(1);
				}

				if (1 == curr->size) {
					int64_t const x1 = curr->x;
					int64_t const x2 = next->x;
					int64_t const d2 = Dist(x1, x2);
					if (Merged(d2)) {
						next->prev = curr->id;
						curr->next = next->id;
						++merges;
						break;
					}
					else {
						quit = 1;
						break;
					}
				}
				else {
					int64_t const last = curr->node;
					struct cluster const * const child = &clusters[last];
					int64_t const x1 = child->x;
					int64_t const x2 = next->x;
					int64_t const d2 = Dist(x1, x2);
					if (Merged(d2)) {
						next->prev = curr->id;
						curr->next = next->id;
						++merges;
						break;
					}
					else {
						quit = 1;
						break;
					}
				}
			}
			if (quit) {
				break;
			}
		}
	}

	// check join of clusters on the same scanline
	for (int64_t i = 0; i != clno; ++i) {
		int64_t const id = cl[i];
		struct cluster const * const curr = &clusters[id];
		if (BLUE_MASK_SONIC != curr->mask) {
			fprintf(stderr, "%s\n", "error: cluster mask");
			XCloseDisplay(display);
			_exit(1);
		}

		if (curr->next != curr->id) {
			// TODO RENAME right, left -> count_forward, count_backward
			int64_t right = 0;
			struct cluster const * next = &clusters[curr->next];
			while (next->next != next->id) {
				if (BLUE_MASK_SONIC != next->mask) {
					fprintf(stderr, "%s\n", "error: mask");
					XCloseDisplay(display);
					_exit(1);
				}
				next = &clusters[next->next];
				++right;
			}
			struct cluster const * prev = &clusters[next->prev];
			if (prev->id != curr->id) {
				int64_t left = 1;
				while (prev->prev != curr->id) {
					prev = &clusters[prev->prev];
					++left;
				}
				if (left != right) {
					fprintf(stderr, "%s\n", "error: traversal");
					XCloseDisplay(display);
					_exit(1);
				}
			}
		}
	}

	// TODO: assert that cluster-iterators point to actual clusters and only point to nodes on edge-cases
	for (int64_t i = 0; i != (clno - 1); ++i) {
		int64_t const ii = cl[i];
		struct cluster *curr = &clusters[ii];
		if (curr->root != curr->id) {
			continue;
		}
		else if (BLUE_MASK_SONIC != curr->mask) {
			continue;
		}

		int64_t const x_l = curr->x;
		int64_t x_u = curr->x;
		if (
			(curr->next != curr->id) &&
			(curr->y == clusters[curr->next].y)
		   ) {
			struct cluster const *iter = &clusters[curr->next];
			struct cluster const *prev = &clusters[curr->next];
			while ((iter->y == curr->y) && (iter->next != iter->id)) {
				if (BLUE_MASK_SONIC != iter->mask) {
					fprintf(stderr, "%s\n", "error: mask");
					XCloseDisplay(display);
					_exit(1);
				}
				prev = iter;
				iter = &clusters[iter->next];
			}

			if (iter->y != curr->y) {
				iter = prev;
			}

			if (iter->y != curr->y) {
				fprintf(stderr, "%s\n", "error: not on the same scanline");
				XCloseDisplay(display);
				_exit(1);
			}

			if (1 == iter->size) {
				x_u = iter->x;
			}
			else {
				iter = &clusters[iter->node];
				x_u = iter->x;
			}
		}
		else if (1 == curr->size) {
			continue;
		}
		else if (curr->size > 1) {
			// NOTE using the redundant logic expression for readability
			struct cluster const * const iter = &clusters[curr->node];
			if (BLUE_MASK_SONIC != iter->mask) {
				fprintf(stderr, "%s\n", "error: mask");
				XCloseDisplay(display);
				_exit(1);
			}
			x_u = iter->x;
		}

		if (x_l == x_u) {
			fprintf(stderr, "%s\n", "error: traversal logic");
			XCloseDisplay(display);
			_exit(1);
		}

		for (int64_t j = (i + 1); j != clno; ++j) {
			int64_t const jj = cl[j];
			struct cluster *next = &clusters[jj];
			if (next->y == curr->y) {
				continue;
			}
			else if ((next->y - curr->y) > 1) {
				break;
			}
			else if (next->root != next->id) {
				// FIXME: by skipping the nodes (strictly looking at the clusters) the algorithm misses merging opportunities
				continue;
			}
			else if (BLUE_MASK_SONIC != next->mask) {
				continue;
			}
			else if (next->prev == curr->id) {
				// NOTE: taken by whom this might be a keypoint for
				// detecting merges between super clusters
				continue;
			}

			if ((next->x >= x_l) && (next->x <= x_u)) {
				if (curr->next != curr->id) {
					Merge(curr, next, clusters);
					continue;
				}
				else {
					curr->next = next->id;
					next->prev = curr->id;
					continue;
				}
			}
			else if (next->size > 1) {
				struct cluster const * const it = &clusters[next->node];
				if ((it->x >= x_l) && (it->x <= x_u)) {
					if (curr->next != curr->id) {
						Merge(curr, next, clusters);
						continue;
					}
					else {
						curr->next = next->id;
						next->prev = curr->id;
						continue;
					}
				}
			}
			else if (next->next != next->id) {
				struct cluster const *it = &clusters[next->next];
				if (it->y != next->y) {
					fprintf(stderr, "%s\n", "error: surprising not same scaline");
					XCloseDisplay(display);
					_exit(1);
				}

				// checks the cluster coordinates
				if ((it->x >= x_l) && (it->x <= x_u)) {
					if (curr->next != curr->id) {
						Merge(curr, next, clusters);
						continue;
					}
					else {
						curr->next = next->id;
						next->prev = curr->id;
						continue;
					}
				}
				else if (it->size > 1) {
					it = &clusters[it->node];
					if ((it->x >= x_l) && (it->x <= x_u)) {
						if (curr->next != curr->id) {
							Merge(curr, next, clusters);
							continue;
						}
						else {
							curr->next = next->id;
							next->prev = curr->id;
							continue;
						}
					}
				}
				else if (it->next != it->id) {

					while (it->next != it->id) {
						if (BLUE_MASK_SONIC != it->mask) {
							fprintf(stderr, "%s\n", "error: mask");
							XCloseDisplay(display);
							_exit(1);
						}

						it = &clusters[it->next];

						if (it->y != next->y) {
							fprintf(stderr, "%s\n", "error: surprising not same scaline");
							XCloseDisplay(display);
							_exit(1);
						}

						if ((it->x >= x_l) && (it->x <= x_u)) {
							if (curr->next != curr->id) {
								Merge(curr, next, clusters);
								continue;
							}
							else {
								curr->next = next->id;
								next->prev = curr->id;
								continue;
							}
						}
					}

					// NOTE the last hope, check the last node in the cluster
					if (it->size > 1) {
						it = &clusters[it->node];
						if ((it->x >= x_l) && (it->x <= x_u)) {
							if (curr->next != curr->id) {
								Merge(curr, next, clusters);
								continue;
							}
							else {
								curr->next = next->id;
								next->prev = curr->id;
								continue;
							}
						}
					}
				}
			}
		}
	}

	int64_t nodes = 0;
	for (int64_t i = 0; i != pixels; ++i) {
		struct cluster const * const c = &clusters[i];
		if (c->mask) {
			++nodes;
		}
	}

	fprintf(stdout, "nodes: %ld\n", nodes);

	{
		int64_t count = 0;
		int64_t const id = cl[0];
		struct cluster const * const c = &clusters[id];
		count += c->size;
		if (c->next != c->id) {
			struct cluster const *iter = &clusters[c->next];
			while (iter->next != iter->id) {
				count += iter->size;
				iter = &clusters[iter->next];
			}
			count += iter->size;
		}
		fprintf(stdout, "count: %ld\n", count);
	}


/*
	// merges clusters lying on the same scanline
	for (int64_t idx = 0; idx != (clno - 1); ++idx) {
		int64_t const curr = cl[idx];
		int64_t const next = cl[idx + 1];
		if (BLUE_MASK_SONIC != clusters[curr].mask) {
			continue;
		}
		else if (BLUE_MASK_SONIC != clusters[next].mask) {
			continue;
		}
		else if (clusters[curr].y != clusters[next].y) {
			continue;
		}
		int merged = 0;
		for (int64_t i = 0; i != clusters[curr].size; ++i) {
			int64_t const x1 = clusters[curr + i].x;
			int64_t const y1 = clusters[curr + i].y;
			for (int64_t j = 0; j != clusters[next].size; ++j) {
				int64_t const x2 = clusters[next + i].x;
				int64_t const y2 = clusters[next + i].y;
				int64_t const d2 = (
					(x2 - x1) * (x2 - x1) +
					(y2 - y1) * (y2 - y1)
				);
				if (d2 <= 64) {
					clusters[curr].next = clusters[next].id;
					clusters[next].prev = clusters[curr].id;
					merged = 1;
					++merges;
					break;
				}
			}
			if (merged) {
				fprintf(stdout, "merged clusters on same scanline: %ld and %ld\n", curr, next);
				break;
			}
		}
	}

	// merges clusters lying on contiguous scanlines
	for (int64_t idx = 0; idx != (clno - 1); ++idx) {
		int64_t const curr = cl[idx];
		int64_t const next = cl[idx + 1];
		if (BLUE_MASK_SONIC != clusters[curr].mask) {
			continue;
		}
		else if (BLUE_MASK_SONIC != clusters[next].mask) {
			continue;
		}
		else if (clusters[curr].x != clusters[next].x) {
			continue;
		}
		int merged = 0;
		for (int64_t i = 0; i != clusters[curr].size; ++i) {
			int64_t const x1 = clusters[curr + i].x;
			int64_t const y1 = clusters[curr + i].y;
			for (int64_t j = 0; j != clusters[next].size; ++j) {
				int64_t const x2 = clusters[next + i].x;
				int64_t const y2 = clusters[next + i].y;
				int64_t const d2 = (
					(x2 - x1) * (x2 - x1) +
					(y2 - y1) * (y2 - y1)
				);

				if (d2 == 1) {
					int64_t iter = curr;
					while (clusters[iter].next != clusters[iter].id) {
						iter = clusters[iter].next;
						if (clusters[iter].id == clusters[next].id) {
							fprintf(stderr, "%s\n", "error: surprising would create closed-loop");
							XCloseDisplay(display);
							_exit(1);
						}
					}
					if (clusters[iter].id >= clusters[next].id) {
						fprintf(stderr, "%s\n", "error: surprising would merge into lower-ranking cluster or create a closed-loop");
						XCloseDisplay(display);
						_exit(1);
					}
					clusters[iter].next = clusters[next].id;
					clusters[next].prev = clusters[iter].id;
					merged = 1;
					++merges;
					break;
				}
			}
			if (merged) {
				//fprintf(stdout, "merged clusters on contiguous scanlines: %ld and %ld\n", curr, next);
				break;
			}
		}
	}
*/

	fprintf(stdout, "clusters-count: %ld\n", clno);
	fprintf(stdout, "merged-clusters-count: %ld\n", merges);
	XCloseDisplay(display);
	return 0;
}
