#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

/*

Copyright (c) 2026 Misael Díaz-Maldonado
This source file is released under the MIT License.
See LICENSE file in the project root for the full license information.

*/

#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <cerrno>
#include <unistd.h>
#include <time.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/XShm.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/mman.h>

#if DEVBUILD
#define Assert(x)\
	if (!(x)) {\
		fprintf(stderr, "assertion failed %s:%d\n", __FILE__, __LINE__);\
		*((volatile int*) 0) = 0;\
	}
#else
#define Assert(x)
#endif

#define BLUE_FPS_TARGET 30.0f
#define BLUE_MASK_SONIC (1L << 0)
#define Blue(r, g, b) ((((r) >= 0x30) && ((r) < 0x60)) && (((g) >= 0x30) && ((g) < 0x60)) && (((b) >= 0x90) && ((b) <= 0xff)))

#define KBD_ESC XKeysymToKeycode(display, XK_Escape)

// References:
// Xlib's shared-memory extension: https://xorg.freedesktop.org/archive/X11R7.7/doc/xextproto/shm.html

typedef char unsigned byte_t;
typedef int64_t CID;

extern "C" struct cluster {
	int32_t root;
	int32_t node;
	int32_t prev;
	int32_t next;
	int32_t size;
	int32_t super;
	int32_t total;
	int32_t id;
	int32_t mask;
	int32_t x;
	int32_t y;
	int32_t x_min;
	int32_t x_max;
	int32_t y_min;
	int32_t y_max;
	int32_t _pad[1];
};

static_assert(64 == sizeof(struct cluster));

extern "C" void LinuxSetTimeSpec(
	struct timespec * const clock_time,
	int64_t const nsec
) {
	clock_time->tv_sec  = (nsec / 1000000000);
	clock_time->tv_nsec = (nsec % 1000000000);
}

extern "C" void LinuxSetDelayTime(
	struct timespec * const clock_target,
	struct timespec const * const clock_start,
	struct timespec const * const clock_delta
) {
	clock_target->tv_sec = (
		(clock_start->tv_sec + clock_delta->tv_sec) +
		((clock_start->tv_nsec + clock_delta->tv_nsec) / 1000000000)
	);
	clock_target->tv_nsec = (
		((clock_start->tv_nsec + clock_delta->tv_nsec) % 1000000000)
	);
}

extern "C" void LinuxDiffTimeSpec(
	struct timespec * const clock_delta,
	struct timespec const * const clock_start,
	struct timespec const * const clock_end
) {
	int64_t nsec_diff = 0;
	int64_t const nsec_start = 1000000000 * clock_start->tv_sec + clock_start->tv_nsec;
	int64_t const nsec_end   = 1000000000 *   clock_end->tv_sec +   clock_end->tv_nsec;
	if (nsec_end > nsec_start) {
		nsec_diff = (nsec_end - nsec_start);
	} else {
		nsec_diff = (nsec_start - nsec_end);
	}
	clock_delta->tv_sec  = (nsec_diff / 1000000000);
	clock_delta->tv_nsec = (nsec_diff % 1000000000);
}

extern "C" void LinuxCSumTimeSpec(
	struct timespec * const clock_csum,
	struct timespec const * const clock_delta
) {
	int64_t const sec = (
		(clock_csum->tv_sec  + clock_delta->tv_sec) +
		((clock_csum->tv_nsec + clock_delta->tv_nsec) / 1000000000)
	);
	int64_t const nsec = (
		((clock_csum->tv_nsec + clock_delta->tv_nsec) % 1000000000)
	);
	clock_csum->tv_sec = sec;
	clock_csum->tv_nsec = nsec;
}

extern "C" void LinuxDelay(
	clockid_t clock_id,
	struct timespec const * const clock_target
) {
	int rc = 0;
	Assert(CLOCK_MONOTONIC == clock_id);
	do {
		rc = clock_nanosleep(clock_id, TIMER_ABSTIME, clock_target, NULL);
		Assert(EFAULT != rc);
		Assert(EINVAL != rc);
	} while (EINTR == rc);
}


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
					Assert(*(part + root) < 0);
					*(part + y * width + x) = root;
					*(part + root) -= 1;
				}
			}
		}
	}

	return 0;

//err_cluster:
//	{
		// NOTE:
		// If the partition value for a root node is positive that means that
		// there is a logic error because root-nodes only store counts and
		// these are negative values to differentiate them easily from node ids.
//		fprintf(stderr, "%s\n", "error: clustering logic");
//		return -1;
//	}
}

extern "C" void MergeClusters(
		struct cluster * const curr,
		struct cluster * const next,
		struct cluster * const clusters,
		int64_t const super
) {
	struct cluster *iter = &clusters[curr->next];
	while (iter->next != iter->id) {
		Assert(BLUE_MASK_SONIC == iter->mask);
		iter = &clusters[iter->next];
	}
	iter->next = next->id;
	next->prev = iter->id;
	next->super = super;
}

extern "C" void CheckBoundsAndMerge(
	struct cluster * const curr,
	struct cluster * const next,
	struct cluster * const clusters,
	int64_t const super,
	int64_t const x_l,
	int64_t const x_u
) {
	if ((next->x >= x_l) && (next->x <= x_u)) {
		if (curr->next != curr->id) {
			MergeClusters(curr, next, clusters, super);
			struct cluster *iter = &clusters[next->next];
			do {
				iter->super = super;
				iter = &clusters[iter->next];
			} while (iter->next != iter->id);
			return;
		}
		else {
			curr->next = next->id;
			next->prev = curr->id;
			next->super = super;
			struct cluster *iter = &clusters[next->next];
			do {
				iter->super = super;
				iter = &clusters[iter->next];
			} while (iter->next != iter->id);
			return;
		}
	}
	else if (next->size > 1) {
		struct cluster const * const node = &clusters[next->node];
		if ((node->x >= x_l) && (node->x <= x_u)) {
			if (curr->next != curr->id) {
				MergeClusters(curr, next, clusters, super);
				struct cluster *iter = &clusters[next->next];
				do {
					iter->super = super;
					iter = &clusters[iter->next];
				} while (iter->next != iter->id);
				return;
			}
			else {
				curr->next = next->id;
				next->prev = curr->id;
				next->super = super;
				struct cluster *iter = &clusters[next->next];
				do {
					iter->super = super;
					iter = &clusters[iter->next];
				} while (iter->next != iter->id);
				return;
			}
		}
		else if ((next->x < x_l) && (node->x > x_u)) {
			// by continuity one of the nodes satisfies [x_l, x_u]
			if (curr->next != curr->id) {
				MergeClusters(curr, next, clusters, super);
				struct cluster *iter = &clusters[next->next];
				do {
					iter->super = super;
					iter = &clusters[iter->next];
				} while (iter->next != iter->id);
				return;
			}
			else {
				curr->next = next->id;
				next->prev = curr->id;
				next->super = super;
				struct cluster *iter = &clusters[next->next];
				do {
					iter->super = super;
					iter = &clusters[iter->next];
				} while (iter->next != iter->id);
				return;
			}
		}
	}
	else if (next->next == next->id) {
		return;
	}

	int merged = 0;
	struct cluster *iter = &clusters[next->next];
	do {
		if (iter->y != next->y) {
			merged = 0;
			break;
		}

		if ((iter->x >= x_l) && (iter->x <= x_u)) {
			if (curr->next != curr->id) {
				MergeClusters(curr, next, clusters, super);
				merged = 1;
				break;
			}
			else {
				curr->next = next->id;
				next->prev = curr->id;
				next->super = super;
				merged = 1;
				break;
			}
		} else if (iter->size > 1) {
			struct cluster const * const node = &clusters[next->node];
			if ((node->x >= x_l) && (node->x <= x_u)) {
				if (curr->next != curr->id) {
					MergeClusters(curr, next, clusters, super);
					merged = 1;
					break;
				}
				else {
					curr->next = next->id;
					next->prev = curr->id;
					next->super = super;
					merged = 1;
					break;
				}
			}
			else if ((iter->x < x_l) && (node->x > x_u)) {
				if (curr->next != curr->id) {
					MergeClusters(curr, next, clusters, super);
					merged = 1;
					break;
				}
				else {
					curr->next = next->id;
					next->prev = curr->id;
					next->super = super;
					merged = 1;
					break;
				}
			}
		}
		iter = &clusters[iter->next];
	} while (iter->next != iter->id);

	if (!merged) {
		return;
	}

	iter = &clusters[next->next];
	do {
		iter->super = super;
		iter = &clusters[iter->next];
	} while (iter->next != iter->id);

	return;
}

extern "C" void MergeSuperClusters(
		struct cluster * const curr,
		struct cluster * const next,
		struct cluster * const clusters,
		int64_t const superid,
		int64_t const x_l,
		int64_t const x_u
) {
	Assert(-1 != superid);
	Assert(-1 != next->super);
	Assert(superid != next->super);
	Assert(curr->super != next->super);
	Assert(BLUE_MASK_SONIC == curr->mask);
	Assert(BLUE_MASK_SONIC == next->mask);
	if (next->x > x_u) {
		return;
	}
	else if (next->x < x_l) {
		int mergeable = 0;
		struct cluster const *iter = &clusters[next->next];
		do {
			if (iter->y != next->y) {
				break;
			}

			if (iter->x >= x_l) {
				mergeable = 1;
				break;
			}
			iter = &clusters[iter->next];
		} while (iter->next != iter->id);

		if (iter->next == iter->id) {
			if ((iter->size > 1) && (iter->y == next->y)) {
				struct cluster const *node = &clusters[iter->node];
				if ((node->x >= x_l) && (node->x <= x_u)) {
					mergeable = 1;
				}
				else if (node->x > x_u) {
					mergeable = 1;
				}
			}
		}

		if (!mergeable) {
			return;
		}
	}

	struct cluster *super = &clusters[superid];
	Assert(super->prev == super->id);

	struct cluster *merge = &clusters[next->super];
	Assert(merge->prev == merge->id);

	int64_t id_super = -1;
	int64_t id_merge = -1;
	if (super->super < merge->super) {
		id_super = super->super;
		id_merge = merge->super;
	}
	else {
		id_super = merge->super;
		id_merge = super->super;
	}

#if DEVBUILD
	struct cluster * const ref_super = &clusters[id_super];
	struct cluster * const ref_merge = &clusters[id_merge];
	Assert(ref_super->prev == ref_super->id);
	Assert(ref_super->super == ref_super->id);
	Assert(ref_merge->prev == ref_merge->id);
	Assert(ref_merge->super == ref_merge->id);

	// gets the total cluster count prior to the merge to verify the merge code
	int64_t count = 0;
	struct cluster *iter = &clusters[id_super];
	while (iter->next != iter->id) {
		iter = &clusters[iter->next];
		++count;
	}

	iter = &clusters[id_merge];
	while (iter->next != iter->id) {
		iter = &clusters[iter->next];
		++count;
	}

	// while-loops yield the count of the linked-clusters excluding the heads
	int64_t const count_total = 2 + count;
#endif

	super = &clusters[id_super];
	merge = &clusters[id_merge];
	struct cluster *left = super;
	struct cluster *right = merge;
	if (super->id < merge->id) {
		left = super;
		right = merge;
	}
	else {
		left = merge;
		right = super;
	}

	int64_t ref_y = left->y;
	if (left->y < right->y) {
		while (left->y < right->y) {
			left->super = id_super;
			Assert(left->next != left->id);
			left = &clusters[left->next];
		}
		left->super = id_super;
	}
	else if (left->y > right->y) {
		while (left->y > right->y) {
			right->super = id_super;
			Assert(right->next != right->id);
			right = &clusters[right->next];
		}
		right->super = id_super;
	}

	Assert(left->y == right->y);

	if (right->x < left->x) {
		struct cluster *iter = left;
		left = right;
		right = iter;
	}

	// NOTE: clusters are on the same scanline
	ref_y = left->y;
	if (left->prev == left->id) {
		if (right->prev == right->id) {
			struct cluster *prev_left = left;
			while (left->y == ref_y) {
				left->super = id_super;
				if (left->next == left->id) {
					break;
				}
				left = &clusters[left->next];
			}

			left->super = id_super;
			if (left->next == left->id) {
				if (left->y == ref_y) {
					left->next = right->id;
					right->prev = left->id;
					while (right->next != right->id) {
						right->super = id_super;
						right = &clusters[right->next];
					}
					right->super = id_super;
					goto check_merge;
				}
				else {
					prev_left = &clusters[left->prev];
					prev_left->next = right->id;
					right->prev = prev_left->id;
					while (right->y == ref_y) {
						right->super = id_super;
						if (right->next == right->id) {
							break;
						}
						right = &clusters[right->next];
					}

					right->super = id_super;
					if (right->next == right->id) {
						if (right->y == ref_y) {
							right->next = left->id;
							left->prev = right->id;
							goto check_merge;
						}
						else {
							struct cluster *prev_right = &clusters[right->prev];
							prev_right->next = left->id;
							left->prev = prev_right->id;

							left->next = right->id;
							right->prev = left->id;
							goto check_merge;
						}
					}
					else {
						struct cluster *prev_right = &clusters[right->prev];
						prev_right->next = left->id;
						left->prev = prev_right->id;

						left->next = right->id;
						right->prev = left->id;
						while (right->next != right->id) {
							right->super = id_super;
							right = &clusters[right->next];
						}
						right->super = id_super;
						goto check_merge;
					}
				}
			}
			else {
				prev_left = &clusters[left->prev];
				prev_left->next = right->id;
				right->prev = prev_left->id;
				struct cluster *prev_right = right;
				while (right->y == ref_y) {
					right->super = id_super;
					if (right->next == right->id) {
						break;
					}
					right = &clusters[right->next];
				}

				right->super = id_super;
				if (right->next == right->id) {
					if (right->y == ref_y) {
						right->next = left->id;
						left->prev = right->id;
						while (left->next != left->id) {
							left->super = id_super;
							left = &clusters[left->next];
						}
						left->super = id_super;
						goto check_merge;
					}
					else {
						prev_right = &clusters[right->prev];
						prev_right->next = left->id;
						left->prev = prev_right->id;

						prev_left = left;
						while (left->y == prev_left->y) {
							left->super = id_super;
							if (left->next == left->id) {
								break;
							}
							left = &clusters[left->next];
						}

						left->super = id_super;
						if (left->next == left->id) {
							if (left->y == prev_left->y) {
								left->next = right->id;
								right->prev = left->id;
								goto check_merge;
							}
							else {
								prev_left = &clusters[left->prev];
								prev_left->next = right->id;
								right->prev = prev_left->id;
								right->next = left->id;
								left->prev = right->id;
								goto check_merge;
							}
						}
						else {
							prev_left = &clusters[left->prev];
							prev_left->next = right->id;
							right->prev = prev_left->id;

							right->next = left->id;
							left->prev = right->id;
							while (left->next != left->id) {
								left->super = id_super;
								left = &clusters[left->next];
							}
							left->super = id_super;
							goto check_merge;
						}
					}
				}
				else {
					// NOTE: after linking these we are ready to repeat the work on the current scanline so nothing more to do in this codeblock
					prev_right = &clusters[right->prev];
					prev_right->next = left->id;
					left->prev = prev_right->id;
				}
			}
		}
		else {
			// NOTE: in this case the `right` has a preceeding scanline
			struct cluster *prev_right = &clusters[right->prev];
			prev_right->next = left->id;
			left->prev = prev_right->id;

			while (left->y == ref_y) {
				left->super = id_super;
				if (left->next == left->id) {
					break;
				}
				left = &clusters[left->next];
			}

			left->super = id_super;
			if (left->next == left->id) {
				if (left->y == ref_y) {
					left->next = right->id;
					right->prev = left->id;
					while (right->next != right->id) {
						right->super = id_super;
						right = &clusters[right->next];
					}
					right->super = id_super;
					goto check_merge;
				}
				else {
					struct cluster *prev_left = &clusters[left->prev];
					prev_left->next = right->id;
					right->prev = prev_left->id;

					// need to link to left which is on the next scanline
					while (right->y == ref_y) {
						right->super = id_super;
						if (right->next == right->id) {
							break;
						}
						right = &clusters[right->next];
					}

					right->super = id_super;
					if (right->next == right->id) {
						if (right->y == ref_y) {
							right->next = left->id;
							left->prev = right->id;
							goto check_merge;
						}
						else {
							prev_right = &clusters[right->prev];
							prev_right->next = left->id;
							left->prev = prev_right->id;

							left->next = right->id;
							right->prev = left->id;
							goto check_merge;
						}
					}
					else {
						prev_right = &clusters[right->prev];
						prev_right->next = left->id;
						left->prev = prev_right->id;

						left->next = right->id;
						right->prev = left->id;
						while (right->next != right->id) {
							right->super = id_super;
							right= &clusters[right->next];
						}
						right->super = id_super;
						goto check_merge;
					}
				}
			}
			else {
				struct cluster *prev_left = &clusters[left->prev];
				prev_left->next = right->id;
				right->prev = prev_left->id;

				while (right->y == ref_y) {
					right->super = id_super;
					if (right->next == right->id) {
						break;
					}
					right = &clusters[right->next];
				}

				right->super = id_super;
				if (right->next == right->id) {
					if (right->y == ref_y) {
						right->next = left->id;
						left->prev = right->id;
						while (left->next != left->id) {
							left->super = id_super;
							left = &clusters[left->next];
						}
						left->super = id_super;
						goto check_merge;
					}
					else {
						prev_right = &clusters[right->prev];
						prev_right->next = left->id;
						left->prev = prev_right->id;

						// we need to iterate on left while keeping ourselves on the same scanline to link to the last cluster on right

						prev_left = left;
						while (left->y == prev_left->y) {
							left->super = id_super;
							if (left->next == left->id) {
								break;
							}
							left = &clusters[left->next];
						}

						// then we have to update `super` data member of `left` until we reach the end
						left->super = id_super;
						if (left->next == left->id) {
							if (left->y == prev_left->y) {
								left->next = right->id;
								right->prev = left->id;
								goto check_merge;
							}
							else {
								prev_left = &clusters[left->prev];
								prev_left->next = right->id;
								right->prev = prev_left->id;

								right->next = left->id;
								left->prev = right->id;
								goto check_merge;
							}
						}
						else {
							prev_left = &clusters[left->prev];
							prev_left->next = right->id;
							right->prev = prev_left->id;

							right->next = left->id;
							left->prev = right->id;
							while (left->next != left->id) {
								left->super = id_super;
								left = &clusters[left->next];
							}
							left->super = id_super;
							goto check_merge;
						}
					}
				}
				else {
					// after linking we are ready for executing the merge in a loop
					prev_right = &clusters[right->prev];
					prev_right->next = left->id;
					left->prev = prev_right->id;
				}
			}
		}
	}
	else {
		if (right->prev == right->id) {
			// NOTE: we can confidently skip to the merge loop
		}
		else {
			// complains because this execution path should not happen
			// since we explicitly looked for the first instance where
			// both clusters have the same y-coord values.
			// XCloseDisplay(display);
			Assert(0);
		}
	}

	while (1) {
		Assert(left->y == right->y);

		ref_y = left->y;

		while (left->y == ref_y) {
			left->super = id_super;
			if (left->next == left->id) {
				break;
			}
			left = &clusters[left->next];
		}

		left->super = id_super;
		if (left->next == left->id) {
			if (left->y == ref_y) {
				left->next = right->id;
				right->prev = left->id;
				while (right->next != right->id) {
					right->super = id_super;
					right = &clusters[right->next];
				}
				right->super = id_super;
				goto check_merge;
			}
			else {
				struct cluster *prev_left = &clusters[left->prev];
				prev_left->next = right->id;
				right->prev = prev_left->id;

				// `left` is the last and we need to link to it
				while (right->y == ref_y) {
					right->super = id_super;
					if (right->next == right->id) {
						break;
					}
					right = &clusters[right->next];
				}

				right->super = id_super;
				if (right->next == right->id) {
					if (right->y == ref_y) {
						right->next = left->id;
						left->prev = right->id;
						goto check_merge;
					}
					else {
						struct cluster *prev_right = &clusters[right->prev];
						prev_right->next = left->id;
						left->prev = prev_right->id;

						left->next = right->id;
						right->prev = left->id;
						goto check_merge;
					}
				}
				else {
					struct cluster *prev_right = &clusters[right->prev];
					prev_right->next = left->id;
					left->prev = prev_right->id;

					left->next = right->id;
					right->prev = left->id;

					while (right->next != right->id) {
						right->super = id_super;
						right = &clusters[right->next];
					}
					right->super = id_super;
					goto check_merge;
				}
			}
		}
		else {
			// you need to connect to the right and advance to the next scanline on the right if any
			struct cluster *prev_left = &clusters[left->prev];
			prev_left->next = right->id;
			right->prev = prev_left->id;

			while (right->y == ref_y) {
				right->super = id_super;
				if (right->next == right->id) {
					break;
				}
				right = &clusters[right->next];
			}

			right->super = id_super;
			if (right->next == right->id) {
				if (right->y == ref_y) {
					right->next = left->id;
					left->prev = right->id;
					while (left->next != left->id) {
						left->super = id_super;
						left = &clusters[left->next];
					}
					left->super = id_super;
					goto check_merge;
				}
				else {
					struct cluster *prev_right = &clusters[right->prev];
					prev_right->next = left->id;
					left->prev = prev_right->id;

					prev_left = left;
					// still need to connect to right
					while (left->y == prev_left->y) {
						left->super = id_super;
						if (left->next == left->id) {
							break;
						}
						left = &clusters[left->next];
					}

					left->super = id_super;
					if (left->next == left->id) {
						if (left->y == prev_left->y) {
							left->next = right->id;
							right->prev = left->id;
							goto check_merge;
						}
						else {
							prev_left = &clusters[left->prev];
							prev_left->next = right->id;
							right->prev = prev_left->id;

							right->next = left->id;
							left->prev = right->id;
							goto check_merge;
						}
					}
					else {
						prev_left = &clusters[left->prev];
						prev_left->next = right->id;
						right->prev = prev_left->id;

						right->next = left->id;
						left->prev = right->id;

						while (left->next != left->id) {
							left->super = id_super;
							left = &clusters[left->next];
						}
						left->super = id_super;
						goto check_merge;
					}
				}
			}
			else {
				// links clusters and we are ready for the next iteration
				struct cluster *prev_right = &clusters[right->prev];
				prev_right->next = left->id;
				left->prev = prev_right->id;
			}
		}
	}

	Assert(0);
	//fprintf(stderr, "%s\n", "error: should never execute");
	return;
#if DEVBUILD
check_merge: {
		     // checks the cluster count and we have to initialize to 1 to account for the super-cluster itself
		     count = 1;
		     iter = ref_super;
		     while (iter->next != iter->id) {
			     iter = &clusters[iter->next];
			     ++count;
		     }

		     Assert(count_total == count);

		     count = 1;
		     while (iter->prev != iter->id) {
			     iter = &clusters[iter->prev];
			     ++count;
		     }

		     Assert(count_total == count);

		     iter = ref_super;
		     while (iter->next != iter->id) {
			     Assert(iter->super == id_super);
			     iter = &clusters[iter->next];
		     }

		     iter = ref_super;
		     struct cluster *next = &clusters[iter->next];
		     while (next->next != next->id) {
			     Assert(iter->id < next->id);
			     iter = &clusters[iter->next];
			     next = &clusters[next->next];
		     }

		     return;
	     }
#else
check_merge: {
		     return;
	     }
#endif
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

	Window GameWindow = 0;
	// gets the window resource ID from the command-line 
	for (int i = 0; i != argc; ++i) {
		if (!strcmp(argv[i], "--window")) {

			errno = 0;
			char *endptr = NULL;
			GameWindow = strtol(argv[i + 1], &endptr, 10);
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

			if (!GameWindow) {
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

	if (!XShmQueryExtension(display)) {
		XCloseDisplay(display);
		fprintf(stderr, "%s\n", "error: requires X11 shared-memory extension");
		_exit(1);
	}

	XWindowAttributes attributes = {};
	XGetWindowAttributes(display, GameWindow, &attributes);
//	int const x = attributes.x;
//	int const y = attributes.y;
	int64_t const width = attributes.width;
	int64_t const height = attributes.height;
	Screen *screen = attributes.screen;
	int64_t const depth = attributes.depth;
	Visual *visual = attributes.visual;

	// TODO: disable fullscreen toggling because the client might support this but we are enforcing a fixed sized window
	XSizeHints *SizeHintsGameWindow = XAllocSizeHints();
	if (!SizeHintsGameWindow) {
		XCloseDisplay(display);
		_exit(1);
	}

	// NOTES: fixes the game window dimensions so that we can do our work without defensive programming for handling dimension changes; in practice we don't want to change the game dimensions when we call this engine and so this guarantees that
	SizeHintsGameWindow->flags = (PMinSize | PMaxSize);
	SizeHintsGameWindow->min_width = width;
	SizeHintsGameWindow->max_width = width;
	SizeHintsGameWindow->min_height = height;
	SizeHintsGameWindow->max_height = height;
	XSetWMNormalHints(display, GameWindow, SizeHintsGameWindow);
	XSync(display, False);

	XShmSegmentInfo shminfo = {};
	XImage *GameImage = XShmCreateImage(
            display,
	    visual,
            depth,
            ZPixmap,
            NULL,
            &shminfo,
            width,
	    height);

	// NOTE: trying to free XImage GameImage with XDestroyImage could fail becasue it tries to free the underlying data and it has not been allocated so maybe try to call XFree() or free() or let the operating system (Linux Kernel) handle the resource management as Casey Muratori would probably advice on this one. The best way to handle it here is to call XDestroyImage after nullifying the data member of GameImage to err on the safe-side.
	if (!GameImage) {
		XCloseDisplay(display);
		fprintf(stderr, "%s\n", "error: XShmAttach failed");
		_exit(1);
	}

	errno = 0;
	int64_t rc = shmget(
		IPC_PRIVATE,
		GameImage->bytes_per_line * GameImage->height,
		IPC_CREAT | 0777
	);
	if (-1 == rc) {
		fprintf(stderr, "%s\n", "error: failed to get shared-memory identifier");
		if (errno) {
			fprintf(stderr, "%s\n", strerror(errno));
		}
		XCloseDisplay(display);
		_exit(1);
	}

	shminfo.shmid = rc;
	// NOTE: not using the base address of the memory-mapping as I was planning to do because of the remapping issues that this could cause. Probably the XServer expects the memory address to stay fixed and this is not guaranteed when using mremap with MREMAP_MAYMOVE which is exactly what the engine does
	shminfo.shmaddr = GameImage->data = ((char*) shmat(shminfo.shmid, NULL, 0));
	shminfo.readOnly = False;
	if (!XShmAttach(display, &shminfo)) {
		XCloseDisplay(display);
		fprintf(stderr, "%s\n", "error: XShmAttach failed");
		_exit(1);
	}

	// plane mask tells that we care about all the bits that define color RRGGBB
	int const format = ZPixmap;
	int64_t const plane_mask = 0xffffff;
	if (!XShmGetImage(display, GameWindow, GameImage, 0, 0, plane_mask)) {
		XCloseDisplay(display);
		fprintf(stderr, "%s\n", "error: XShmGetImage failed");
		_exit(1);
	}

	XSetWindowAttributes OutputWindowAttributes = {};
	OutputWindowAttributes.background_pixel = BlackPixelOfScreen(screen);
	OutputWindowAttributes.event_mask = (
		ExposureMask |
//		StructureNotifyMask |
		KeyPressMask |
		0
	);

	Window OutputWindow = XCreateWindow(
		display,
		DefaultRootWindow(display),
		0,
		0,
		width,
		height,
		0,
		depth,
		InputOutput,
		DefaultVisualOfScreen(DefaultScreenOfDisplay(display)),
		CWBackPixel | CWEventMask,
		&OutputWindowAttributes
	);

	// TODO: fix the engine window size, this means no more resizing and no need for structure-notify event handling though don't remove the code because it can serve as reference in the future
	XSizeHints *SizeHints = XAllocSizeHints();
	if (!SizeHints) {
		fprintf(stderr, "%s\n", "error; XSizeHints allocation failed");
		XDestroyWindow(display, OutputWindow);
		XCloseDisplay(display);
		_exit(1);
	}

	SizeHints->flags = PMinSize;
	SizeHints->min_width = width;
	SizeHints->min_height = height;
	XSetWMNormalHints(display, OutputWindow, SizeHints);
	XStoreName(display, OutputWindow, "Handcrafted Blue Computer Vision Engine");

	XEvent ev = {};
	XMapWindow(display, OutputWindow);
	XWindowEvent(display, OutputWindow, ExposureMask, &ev);

	int64_t iters = 0;
	int64_t red_shift = 0;
	int64_t green_shift = 0;
	int64_t blue_shift = 0;
	int64_t const rgb_mask = 0xff;
	int64_t const red_mask = GameImage->red_mask;
	int64_t const green_mask = GameImage->green_mask;
	int64_t const blue_mask = GameImage->blue_mask;
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
	fprintf(stdout, "red-mask: %ld\n", red_mask);
	fprintf(stdout, "green-mask: %ld\n", green_mask);
	fprintf(stdout, "blue-mask: %ld\n", blue_mask);
	fprintf(stdout, "red-shift: %ld\n", red_shift);
	fprintf(stdout, "green-shift: %ld\n", green_shift);
	fprintf(stdout, "blue-shift: %ld\n", blue_shift);

	// TODO: do alternate the bytes frame computation to check:
	// 		bytes_per_line * height == bytes_frame
	int64_t pitch = GameImage->bytes_per_line;
	int64_t pixels = (width * height);
	int64_t const bytes_per_pixel = (GameImage->bits_per_pixel >> 3);
	int64_t bytes_frame = bytes_per_pixel * pixels;
	int64_t bytes_partition = bytes_frame;
	// even for 24-bit depth visuals images are usually stored with 32-bit padding
	if (32 != GameImage->bits_per_pixel) {
		fprintf(stderr, "%s\n", "error: unexpected pixel depth");
		XCloseDisplay(display);
		_exit(1);
	}

	errno = 0;
	rc = sysconf(_SC_PAGESIZE);
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
	int64_t bytes_cluster_list = pixels * sizeof(CID);
	int64_t bytes_clusters = pixels * sizeof(*clusp);
	int64_t bytes_required = (
			bytes_partition +
			bytes_clusters +
			bytes_cluster_list +
			0
	);
	int64_t bytes_aligned = (((bytes_required + mask_page) & (~mask_page)) << 1);
	int64_t bytes_mmap = bytes_aligned;

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
	// TODO: consider using MADV_RANDOM
	rc = madvise(base, bytes_mmap, MADV_WILLNEED);
	if (-1 == rc) {
		if (errno) {
			fprintf(stderr, "%s\n", strerror(errno));
		}
		XCloseDisplay(display);
		_exit(1);
	}

	// initializes the partition array for the clustering algorithm
	int64_t offset_frame = 0;
	int64_t offset_partition = (((offset_frame + bytes_partition) + 0x3f) & ~0x3f);
	int64_t offset_clusters = (((offset_partition + bytes_partition) + 0x3f) & ~0x3f);
	int64_t offset_cluster_list = (
		((offset_clusters + bytes_clusters) + 0x3f) & ~0x3f
	);
	int32_t *part = (typeof(part)) (((byte_t*) base) + offset_partition);
	if (((uintptr_t) part) & 63) {
		fprintf(stderr, "%s\n", "error: array 'part' not 64-byte aligned");
		XCloseDisplay(display);
		_exit(1);
	}

	struct cluster *clusters = (typeof(clusters)) (((byte_t*) base) + offset_clusters);
	if (((uintptr_t) clusters) & 63) {
		fprintf(stderr, "%s\n", "error: array 'clusters' not 64-byte aligned");
		XCloseDisplay(display);
		_exit(1);
	}

	char *data = (typeof(data)) (((char*) base) + offset_frame);
	if (((uintptr_t) data) & 63) {
		fprintf(stderr, "%s\n", "error: framebuffer not 64-byte aligned");
		XCloseDisplay(display);
		_exit(1);
	}

	XImage *img = XCreateImage(
		display,
		visual,
		depth,
		format,
		0,
		data,
		width,
		height,
		32,
		0
	);

	if (!img) {
		fprintf(stderr, "%s\n", "error; failed to create XImage for the engine");
		XDestroyWindow(display, OutputWindow);
		XCloseDisplay(display);
		_exit(1);
	}


	float constexpr FPSFloat = BLUE_FPS_TARGET;
	float constexpr FPSInvFloat = 1.0e9f / FPSFloat;
	int64_t constexpr FrameDurationTargetNanoSec = FPSInvFloat;
	struct timespec start_frame = {};
	struct timespec end_frame = {};
	struct timespec etime_frame = {};
	struct timespec delta_frame = {};
	struct timespec sleep_frame = {};
	struct timespec target_frame = {};
	struct timespec start_getimage = {};
	struct timespec end_getimage = {};
	struct timespec etime_getimage = {};
	struct timespec delta_getimage = {};
	struct timespec start_init = {};
	struct timespec end_init = {};
	struct timespec etime_init = {};
	struct timespec delta_init = {};
	struct timespec start_cluster = {};
	struct timespec end_cluster = {};
	struct timespec etime_cluster = {};
	struct timespec delta_cluster = {};
	struct timespec start_merge = {};
	struct timespec end_merge = {};
	struct timespec etime_merge = {};
	struct timespec delta_merge = {};
	struct timespec start_backbuffer = {};
	struct timespec end_backbuffer = {};
	struct timespec etime_backbuffer = {};
	struct timespec delta_backbuffer = {};
	struct timespec start_putimage = {};
	struct timespec end_putimage = {};
	struct timespec etime_putimage = {};
	struct timespec delta_putimage = {};
	LinuxSetTimeSpec(&target_frame, FrameDurationTargetNanoSec);

	int64_t frameno = 0;
	int64_t mergeno = 0;
	int64_t backbufferno = 0;
	while (1) {
		clock_gettime(CLOCK_MONOTONIC, &start_frame);
		if (XCheckTypedWindowEvent(display, OutputWindow, KeyPress, &ev)) {
			switch (ev.type) {
			case ConfigureNotify: {
				Assert(0);
				if (
				      (width != ev.xconfigure.width) ||
				      (height != ev.xconfigure.height)
				   ) {
					int64_t width_new = ev.xconfigure.width;
					int64_t height_new = ev.xconfigure.height;
					pixels = (width_new * height_new);
					bytes_frame = bytes_per_pixel * pixels;
					bytes_partition = bytes_frame;
					bytes_cluster_list = pixels * sizeof(CID);
					bytes_clusters = pixels * sizeof(*clusp);
					bytes_required = (
						bytes_frame +
						bytes_partition +
						bytes_clusters +
						bytes_cluster_list +
						0
					);
					bytes_aligned = (
						(bytes_required + mask_page) &
						(~mask_page)
					);

					if ((bytes_aligned + pagesz) > bytes_mmap) {
						errno = 0;
						base = mremap(base, bytes_mmap, (bytes_mmap << 1), MREMAP_MAYMOVE);
						Assert(MAP_FAILED != base);
						bytes_mmap <<= 1;
					}
					offset_frame = 0;
					offset_partition = (((offset_frame + bytes_frame) + 0x3f) & ~0x3f);
					offset_clusters = (((offset_partition + bytes_partition) + 0x3f) & ~0x3f);
					offset_cluster_list = (
						((offset_clusters + bytes_clusters) + 0x3f) & ~0x3f
					);
				}
				XClearWindow(display, OutputWindow);
			}
			break;
			case KeyPress: {
				// TODO: instead of quitting in place you can now break the engine loop so that the resources are released properly
				if (KBD_ESC == ev.xkey.keycode) {
					fprintf(stdout, "%s\n", "quitting upon user request");
					XFree(SizeHints);
					XFree(SizeHintsGameWindow);
					XDestroyWindow(display, OutputWindow);
					XCloseDisplay(display);
					_exit(0);
				}
			}
			break;
			default: {
				 Assert(0);
			}
			}
		}

		clock_gettime(CLOCK_MONOTONIC, &start_getimage);
		XShmGetImage(display, GameWindow, GameImage, 0, 0, plane_mask);
		clock_gettime(CLOCK_MONOTONIC, &end_getimage);
		LinuxDiffTimeSpec(&delta_getimage, &start_getimage, &end_getimage);
		LinuxCSumTimeSpec(&etime_getimage, &delta_getimage);

		clock_gettime(CLOCK_MONOTONIC, &start_init);
		// PERF: removes performance bottleneck, it's not necessary to clear the entire memory map
		//memset(base, 0, bytes_mmap);

		// NOTE: the base address may change on mremaps (due to the resizing of the engine window) and so we need to update the framebuffer address to avert errors
		img->data = (typeof(img->data)) (((char*) base) + offset_frame);
		data = GameImage->data;
		clusters = (typeof(clusters)) (((byte_t*) base) + offset_clusters);
		Assert(0 == (((uintptr_t) clusters) & 63));
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
				cluster->super = -1;
				cluster->total = 1;
				cluster->size = 1;
				cluster->id = id;
				cluster->x = x;
				cluster->y = y;
			}
			data += pitch;
		}

		data = GameImage->data;
		part = (typeof(part)) (((byte_t*) base) + offset_partition);
		Assert(0 == (((uintptr_t) part) & 63));

		memset(part, 0xff, bytes_partition);
		clock_gettime(CLOCK_MONOTONIC, &end_init);
		LinuxDiffTimeSpec(&delta_init, &start_init, &end_init);
		LinuxCSumTimeSpec(&etime_init, &delta_init);

		clock_gettime(CLOCK_MONOTONIC, &start_cluster);
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
				Assert(-1 != rc);
			}
			data += pitch;
		}

		int64_t clno = 0;
		CID *cl = (typeof(cl)) (((byte_t*) base) + offset_cluster_list);
		Assert(0 == (((uintptr_t) cl) & 63));
		memset(cl, 0, bytes_cluster_list);

		// links nodes of constant y-striped clusters (same scanline)
		for (int64_t i = 0; i != pixels; ++i) {
			struct cluster *cluster = &clusters[i];
			if ((BLUE_MASK_SONIC == cluster->mask) && (part[i] < 0)) {
				cluster->size = -(part[i]);
				int64_t const childno = (cluster->size - 1);
				cluster->node = (i + childno);
				for (int64_t j = 0; j != childno; ++j) {
					int64_t id = ((i + 1) + (childno - 1) - j);
					Assert(part[id] == i);
					struct cluster *child = &clusters[id];
					Assert(BLUE_MASK_SONIC == child->mask);
					child->size = 0;
					child->node = (id - 1);
					child->root = i;
				}
				cl[clno] = i;
				++clno;
			}
		}

		clock_gettime(CLOCK_MONOTONIC, &end_cluster);
		LinuxDiffTimeSpec(&delta_cluster, &start_cluster, &end_cluster);
		LinuxCSumTimeSpec(&etime_cluster, &delta_cluster);

#if DEVBUILD
		// check the links of the constant y-striped clusters
		for (int64_t id = 0; id != pixels; ++id) {
			struct cluster const * const cluster = &clusters[id];
			if (BLUE_MASK_SONIC != cluster->mask) {
				continue;
			}
			else if (cluster->size <= 1) {
				continue;
			}
			int64_t count = 1;
			struct cluster const * child = &clusters[cluster->node];
			int64_t const childno = (cluster->size - 1);
			while (child->node != id) {
				child = &clusters[child->node];
				Assert(child->y == cluster->y);
				Assert(count < childno);
				++count;
			}
			Assert(count == childno);
		}
#endif

		if (clno > 2) {
			clock_gettime(CLOCK_MONOTONIC, &start_merge);
			for (int64_t i = 0; i != (clno - 1); ++i) {
				int64_t const ii = cl[i];
				struct cluster *curr = &clusters[ii];
				Assert(curr->root == curr->id);
				Assert(BLUE_MASK_SONIC == curr->mask);

				int64_t const x_l = curr->x;
				int64_t x_u = curr->x;
				if (
						(curr->next != curr->id) &&
						(curr->y == clusters[curr->next].y)
				   ) {
					struct cluster const *iter = &clusters[curr->next];
					struct cluster const *prev = &clusters[curr->next];
					while ((iter->y == curr->y) && (iter->next != iter->id)) {
						Assert(BLUE_MASK_SONIC == iter->mask);
						prev = iter;
						iter = &clusters[iter->next];
					}

					if (iter->y != curr->y) {
						iter = prev;
					}

					Assert(iter->y == curr->y);

					if (1 == iter->size) {
						x_u = iter->x;
					}
					else {
						iter = &clusters[iter->node];
						Assert(iter->root != iter->id);
						x_u = iter->x;
					}
				}
				else if (1 == curr->size) {
					continue;
				}
				else if (curr->size > 1) {
					// NOTE using the redundant logic expression for readability
					struct cluster const * const node = &clusters[curr->node];
					Assert(BLUE_MASK_SONIC == node->mask);
					x_u = node->x;
				}

				Assert(x_l != x_u);
				Assert(x_l < x_u);

				// initially marks ordinary clusters into super-clusters
				struct cluster *iter = &clusters[curr->id];
				if (-1 == iter->super) {
					while (iter->prev != iter->id) {
						iter = &clusters[iter->prev];
					}
					iter->super = iter->id;
				}
				else {
					iter = &clusters[iter->super];
					Assert(iter->prev == iter->id);
					Assert(iter->super == iter->id);
				}

				int64_t super = iter->super;
				for (int64_t j = (i + 1); j != clno; ++j) {
					int64_t const jj = cl[j];
					struct cluster *next = &clusters[jj];
					Assert(0 != next->size);
					Assert(next->root == next->id);
					if (next->y == curr->y) {
						continue;
					}
					else if ((next->y - curr->y) > 1) {
						break;
					}
					else if (next->root != next->id) {
						continue;
					}
					else if (next->super == super) {
						continue;
					}
					else if ((next->super != -1) && (next->super != super)) {
						MergeSuperClusters(curr, next, clusters, super, x_l, x_u);
						if (super != curr->super) {
							Assert(curr->super == next->super);
							super = curr->super;
						}
						continue;
					}

					CheckBoundsAndMerge(
							curr,
							next,
							clusters,
							super,
							x_l,
							x_u
							);
				}
			}

			int64_t total_max = 1;
			int64_t id_max = -1;
			for (int64_t i = 0; i != clno; ++i) {
				int64_t id = cl[i];
				struct cluster *iter = &clusters[id];
				if (-1 == iter->super) {
					continue;
				}
				struct cluster *super = &clusters[iter->super];
				Assert(super->super == super->id);
				Assert(super->prev == super->id);

				iter = super;
				int64_t count = 0;
				do {
					count += iter->size;
					iter = &clusters[iter->next];
				} while (iter->next != iter->id);
				super->total = count;

				if (super->total > total_max) {
					id_max = super->id;
					total_max = super->total;
				}
			}

			clock_gettime(CLOCK_MONOTONIC, &end_merge);
			LinuxDiffTimeSpec(&delta_merge, &start_merge, &end_merge);
			LinuxCSumTimeSpec(&etime_merge, &delta_merge);
			++mergeno;

			if (-1 != id_max) {
				clock_gettime(CLOCK_MONOTONIC, &start_backbuffer);
				data = (typeof(data)) (((char*) base) + offset_frame);
				memset(data, 0, bytes_per_pixel * width * height);
				int32_t *frame = (typeof(frame)) data;
				struct cluster *c = &clusters[id_max];
				int64_t x_min = width;
				int64_t x_max = 0;
				int64_t y_min = height;
				int64_t y_max = 0;
				struct cluster *iter = c;
				while (iter->next != iter->id) {
					for (int64_t i = 0; i != iter->size; ++i) {
						int64_t const ii = (i + iter->id);
						struct cluster const * const node = &clusters[ii];
						if (node->x < x_min) {
							x_min = node->x;
						}
						if (node->x > x_max) {
							x_max = node->x;
						}
						if (node->y < y_min) {
							y_min = node->y;
						}
						if (node->y > y_max) {
							y_max = node->y;
						}
					}
					iter = &clusters[iter->next];
				}
				c->x_min = x_min;
				c->x_max = x_max;
				c->y_min = y_min;
				c->y_max = y_max;

				iter = c;
				while (iter->next != iter->id) {
					for (int64_t i = 0; i != iter->size; ++i) {
						int64_t const ii = (i + iter->id);
						struct cluster const * const node = &clusters[ii];
						int64_t const x = node->x;
						int64_t const y = node->y;
						int64_t const id = width * y + x;
						int32_t const rgb = (0xff << green_shift);
						frame[id] = rgb;
					}
					iter = &clusters[iter->next];
				}
				clock_gettime(CLOCK_MONOTONIC, &end_backbuffer);
				LinuxDiffTimeSpec(&delta_backbuffer, &start_backbuffer, &end_backbuffer);
				LinuxCSumTimeSpec(&etime_backbuffer, &delta_backbuffer);
				++backbufferno;

				clock_gettime(CLOCK_MONOTONIC, &start_putimage);
				XClearWindow(display, OutputWindow);
				XPutImage(
						display,
						OutputWindow,
						DefaultGCOfScreen(DefaultScreenOfDisplay(display)),
						img,
						c->x_min,
						c->y_min,
						c->x_min,
						c->y_min,
						(c->x_max - c->x_min),
						(c->y_max - c->y_min)
					 );
				XFlush(display);
				clock_gettime(CLOCK_MONOTONIC, &end_putimage);
				LinuxDiffTimeSpec(&delta_putimage, &start_putimage, &end_putimage);
				LinuxCSumTimeSpec(&etime_putimage, &delta_putimage);
			}
			else {
				XClearWindow(display, OutputWindow);
				XFlush(display);
			}
		}
		else {
			XClearWindow(display, OutputWindow);
			XFlush(display);
		}

		if (frameno & 256) {
			// NOTE: guards against displaying infinity on the console
			mergeno = (!mergeno)? 1 : mergeno;
			backbufferno = (!backbufferno)? 1 : backbufferno;
			float invmergeno = 1.0f / ((float) mergeno);
			float invbackbufferno = 1.0f / ((float) backbufferno);
			frameno = 0;
			mergeno = 0;
			backbufferno = 0;
			float invsample = 1.0f / 256.0f;
			// NOTE: elapsed time has data up to the previous frame so 255
			float sec = etime_frame.tv_sec + 1.0e-9 * etime_frame.tv_nsec;
			float ms_frame = (1.0f / 255.0f) * (1.0e+3 * sec);
			float ms_getimage = invsample * (1.0e+3 * etime_getimage.tv_sec + 1.0e-6 * etime_getimage.tv_nsec);
			float ms_init = invsample * (1.0e+3 * etime_init.tv_sec + 1.0e-6 * etime_init.tv_nsec);
			float ms_cluster = invsample * (1.0e+3 * etime_cluster.tv_sec + 1.0e-6 * etime_cluster.tv_nsec);
			float ms_merge = invmergeno * (1.0e+3 * etime_merge.tv_sec + 1.0e-6 * etime_merge.tv_nsec);
			float ms_backbuffer = invbackbufferno * (1.0e+3 * etime_backbuffer.tv_sec + 1.0e-6 * etime_backbuffer.tv_nsec);
			float ms_putimage = invbackbufferno * (1.0e+3 * etime_putimage.tv_sec + 1.0e-6 * etime_putimage.tv_nsec);
			float FPSAvg = 255.0f / sec;
			etime_frame.tv_sec = 0;
			etime_frame.tv_nsec = 0;
			etime_getimage.tv_sec = 0;
			etime_getimage.tv_nsec = 0;
			etime_init.tv_sec = 0;
			etime_init.tv_nsec = 0;
			etime_cluster.tv_sec = 0;
			etime_cluster.tv_nsec = 0;
			etime_merge.tv_sec = 0;
			etime_merge.tv_nsec = 0;
			etime_backbuffer.tv_sec = 0;
			etime_backbuffer.tv_nsec = 0;
			etime_putimage.tv_sec = 0;
			etime_putimage.tv_nsec = 0;
			fprintf(stdout, "\nFPS: %.1f\nXShmGetImage (ms): %.1f\nInit (ms): %.1f\nClustering (ms): %.1f\nMerging (ms): %.1f\nBackbuffer (ms): %.1f\nXPutImage (ms) %.1f\nFrame (ms): %.1f\n", FPSAvg, ms_getimage, ms_init, ms_cluster, ms_merge, ms_backbuffer, ms_putimage, ms_frame);
		}
		else {
			++frameno;
		}
		LinuxSetDelayTime(&sleep_frame, &start_frame, &target_frame);
		LinuxDelay(CLOCK_MONOTONIC, &sleep_frame);
		// NOTE: we are assuming that this timing computations are inexpensive
		clock_gettime(CLOCK_MONOTONIC, &end_frame);
		LinuxDiffTimeSpec(&delta_frame, &start_frame, &end_frame);
		LinuxCSumTimeSpec(&etime_frame, &delta_frame);
	}

	// NOTE: nullifies the Ximage data member to prevent XDestroyImage from trying to free a memory address that's not on the heap
	img->data = NULL;
	XDestroyImage(img);
	XShmDetach(display, &shminfo);
	XDestroyImage(GameImage);
	shmdt(shminfo.shmaddr);
	shmctl(shminfo.shmid, IPC_RMID, 0);
	XFree(SizeHints);
	XFree(SizeHintsGameWindow);
	XDestroyWindow(display, OutputWindow);
	XCloseDisplay(display);
	return 0;
}
