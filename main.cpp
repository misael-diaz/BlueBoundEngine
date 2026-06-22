#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <cerrno>
#include <unistd.h>
#include <time.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <sys/mman.h>

#define BLUE_MASK_SONIC (1L << 0)
// TODO: bound sonic at a fixed framerate
// TODO: consider working with a packed RGB parameter instead
#define Blue(r, g, b) ((((r) >= 0x30) && ((r) < 0x60)) && (((g) >= 0x30) && ((g) < 0x60)) && (((b) >= 0x90) && ((b) <= 0xff)))

#define KBD_ESC XKeysymToKeycode(display, XK_Escape)

typedef char unsigned byte_t;
typedef int64_t CID;

extern "C" struct cluster {
	int64_t mask;
	int64_t root;
	int64_t node;
	int64_t prev;
	int64_t next;
	int64_t size;
	int64_t super;
	int64_t total;
	int64_t id;
	int64_t x;
	int64_t y;
	int64_t _pad[5];
};

static_assert(128 == sizeof(struct cluster));

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

extern "C" void LinuxDelay(
	clockid_t clock_id,
	struct timespec const * const clock_target
) {
	int rc = 0;
	if (CLOCK_MONOTONIC != clock_id) {
		fprintf(stderr, "%s", "unsupported clock_id\n");
		_exit(1);
	}
	do {
		rc = clock_nanosleep(clock_id, TIMER_ABSTIME, clock_target, NULL);
		if (EFAULT == rc) {
			fprintf(stderr, "%s\n", "invalid address for nanosleep");
			_exit(1);
		} else if (EINVAL == rc) {
			fprintf(stderr, "%s\n", "invalid timespec target value");
			_exit(1);
		}
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

extern "C" void MergeClusters(
		struct cluster * const curr,
		struct cluster * const next,
		struct cluster * const clusters,
		int64_t const super
) {
	struct cluster *iter = &clusters[curr->next];
	while (iter->next != iter->id) {
		if (BLUE_MASK_SONIC != iter->mask) {
			fprintf(stderr, "%s\n", "error: mask");
			// NOTE: not going to agonize about exiting without closing the display since the window resource ID is not owned by this client X11 applicationa anyways
			//XCloseDisplay(display);
			_exit(1);
		}
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
	if (-1 == superid) {
		fprintf(stderr, "%s\n", "error: invalid input argument `superid`");
		//XCloseDisplay(display);
		_exit(1);
	}
	else if (-1 == next->super) {
		fprintf(stderr, "%s\n", "error: invalid input argument `next`");
		//XCloseDisplay(display);
		_exit(1);
	}
	else if (superid == next->super) {
		fprintf(stderr, "%s\n", "error: invalid input arguments");
		//XCloseDisplay(display);
		_exit(1);
	}
	else if (curr->super == next->super) {
		fprintf(stderr, "%s\n", "error: already merged yet we got called");
		//XCloseDisplay(display);
		_exit(1);
	}
	else if (BLUE_MASK_SONIC != curr->mask) {
		fprintf(stderr, "%s\n", "error: 'curr' not a cluster, maskbit unset");
		//XCloseDisplay(display);
		_exit(1);
	}
	else if (BLUE_MASK_SONIC != next->mask) {
		fprintf(stderr, "%s\n", "error: 'next' not a cluster, maskbit unset");
		//XCloseDisplay(display);
		_exit(1);
	}
	else if (next->x > x_u) {
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
	if (super->prev != super->id) {
		fprintf(stderr, "%s\n", "error: super-cluster `super` assignment failed previously");
		// XCloseDisplay(display);
		_exit(1);
	}

	struct cluster *merge = &clusters[next->super];
	if (merge->prev != merge->id) {
		fprintf(stderr, "%s\n", "error: super-cluster `merge` assignment failed previously");
		// XCloseDisplay(display);
		_exit(1);
	}

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

	struct cluster * const ref_super = &clusters[id_super];
	struct cluster * const ref_merge = &clusters[id_merge];
	if (ref_super->prev != ref_super->id) {
		fprintf(stderr, "%s\n", "error: 'ref_super' not a super cluster");
		//XCloseDisplay(display);
		_exit(1);
	}
	else if (ref_super->super != ref_super->id) {
		fprintf(stderr, "%s\n", "error: unset super data member");
		//XCloseDisplay(display);
		_exit(1);
	}
	else if (ref_merge->prev != ref_merge->id) {
		fprintf(stderr, "%s\n", "error: 'ref_merge' not a super cluster");
		//XCloseDisplay(display);
		_exit(1);
	}
	else if (ref_merge->super != ref_merge->id) {
		fprintf(stderr, "%s\n", "error: unset super data member");
		//XCloseDisplay(display);
		_exit(1);
	}

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

	// FIXME: assert that the cluster to be linked satisfies the id-ordering
	int64_t ref_y = left->y;
	if (left->y < right->y) {
		while (left->y < right->y) {
			left->super = id_super;
			if (left->next == left->id) {
				fprintf(stderr, "%s\n", "error: early end of 'left' super-cluster");
				// XCloseDisplay(display);
				_exit(1);
			}
			left = &clusters[left->next];
		}
		left->super = id_super;
	}
	else if (left->y > right->y) {
		while (left->y > right->y) {
			right->super = id_super;
			if (right->next == right->id) {
				fprintf(stderr, "%s\n", "error: early end of super-cluster");
				// XCloseDisplay(display);
				_exit(1);
			}
			right = &clusters[right->next];
		}
		right->super = id_super;
	}

	if (left->y != right->y) {
		fprintf(stderr, "%s\n", "error: not on the same scanline traverse error");
		// XCloseDisplay(display);
		_exit(1);
	}

	if (right->x < left->x) {
		iter = left;
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
			fprintf(stderr, "%s\n", "error: merge impl error");
			// XCloseDisplay(display);
			_exit(1);
		}
	}

	while (1) {
		if (left->y != right->y) {
			fprintf(stderr, "%s\n", "error: scanline mismatch error");
			// XCloseDisplay(display);
			_exit(1);
		}

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

	fprintf(stderr, "%s\n", "error: should never execute");
	return;
check_merge: {
		     // checks the cluster count and we have to initialize to 1 to account for the super-cluster itself
		     count = 1;
		     iter = ref_super;
		     while (iter->next != iter->id) {
			     iter = &clusters[iter->next];
			     ++count;
		     }

		     if (count_total != count) {
			     fprintf(stderr, "%s\n", "error: forward merge count");
			     fprintf(stderr, "total: %ld count: %ld\n", count_total, count);
			     // XCloseDisplay(display);
			     _exit(1);
		     }

		     count = 1;
		     while (iter->prev != iter->id) {
			     iter = &clusters[iter->prev];
			     ++count;
		     }

		     if (count_total != count) {
			     fprintf(stderr, "%s\n", "error: backward merge count");
			     fprintf(stderr, "total: %ld count: %ld\n", count_total, count);
			     // XCloseDisplay(display);
			     _exit(1);
		     }

		     iter = ref_super;
		     while (iter->next != iter->id) {
			     if (iter->super != id_super) {
				     fprintf(stderr, "error: missed updating super data member of cluster with id %ld\n", iter->id);
				     // XCloseDisplay(display);
				     _exit(1);
			     }
			     iter = &clusters[iter->next];
		     }

		     iter = ref_super;
		     struct cluster *next = &clusters[iter->next];
		     while (next->next != next->id) {
			     if (iter->id >= next->id) {
				     fprintf(stderr, "error: wrong merge order curr: %ld next:%ld\n", iter->id, next->id);
				     // XCloseDisplay(display);
				     _exit(1);
			     }
			     iter = &clusters[iter->next];
			     next = &clusters[next->next];
		     }

		     return;
	     }
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

	XWindowAttributes attributes = {};
	XGetWindowAttributes(display, GameWindow, &attributes);
//	int const x = attributes.x;
//	int const y = attributes.y;
	int64_t width = attributes.width;
	int64_t height = attributes.height;
//	Screen *screen = attributes.screen;

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

	// plane mask tells that we care about all the bits that define color RRGGBB
	int const format = ZPixmap;
	int64_t const plane_mask = 0xffffff;
	XImage *img = XGetImage(display, GameWindow, 0, 0, width, height, plane_mask, format);
	int64_t const depth = img->depth;

	XSetWindowAttributes OutputWindowAttributes = {};
	OutputWindowAttributes.event_mask = (
		ExposureMask |
		StructureNotifyMask |
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
		CWEventMask,
		&OutputWindowAttributes
	);

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

	XEvent ev = {};
	XMapWindow(display, OutputWindow);
	XWindowEvent(display, OutputWindow, ExposureMask, &ev);

	int64_t iters = 0;
	int64_t red_shift = 0;
	int64_t green_shift = 0;
	int64_t blue_shift = 0;
	int64_t const rgb_mask = 0xff;
	int64_t const red_mask = img->red_mask;
	int64_t const green_mask = img->green_mask;
	int64_t const blue_mask = img->blue_mask;
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

	char *data = img->data;
	int64_t pitch = img->bytes_per_line;
	int64_t pixels = (width * height);
	int64_t bytes_frame = (img->bits_per_pixel >> 3) * pixels;
	int64_t bytes_partition = bytes_frame;
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
	rc = madvise(base, bytes_mmap, MADV_WILLNEED);
	if (-1 == rc) {
		if (errno) {
			fprintf(stderr, "%s\n", strerror(errno));
		}
		XCloseDisplay(display);
		_exit(1);
	}

	// TODO: the original window dimensions are subject to change and so the offets must be dynamic: call signature: mremap(base, bytes_mmap, (bytes_mmap << 1), MREMAP_MAYMOVE);

	// initializes the partition array for the clustering algorithm
	int64_t const offset_partition = 0;
	int64_t const offset_clusters = ((bytes_partition + 0x3f) & ~0x3f);
	int64_t const offset_cluster_list = (
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

	while (1) {
		while (XPending(display)) {
			XNextEvent(display, &ev);
			switch (ev.type) {
			case ConfigureNotify: {
			      if (
				      (width != ev.xconfigure.width) ||
				      (height != ev.xconfigure.height)
				 ) {
					int64_t width_new = ev.xconfigure.width;
					int64_t height_new = ev.xconfigure.height;
					pixels = (width_new * height_new);
					bytes_frame = (img->bits_per_pixel >> 3) * pixels;
					bytes_partition = bytes_frame;
					bytes_cluster_list = pixels * sizeof(CID);
					bytes_clusters = pixels * sizeof(*clusp);
					bytes_required = (
						bytes_partition +
						bytes_clusters +
						bytes_cluster_list +
						0
					);
					bytes_aligned = (
						(bytes_required + mask_page) &
						(~mask_page)
					);

					if (bytes_aligned > bytes_mmap) {
					}
					// TODO: NO MATTER WHAT WE MUST RECALCULATE OFFSETS
			      }
			}
			break;
			case KeyPress: {
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
			}
			}
		}
	}

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
			cluster->super = -1;
			cluster->total = 1;
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
	if (((uintptr_t) cl) & 63) {
		fprintf(stderr, "%s\n", "error: array 'cl' not 64-byte aligned");
		XCloseDisplay(display);
		_exit(1);
	}
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
				child->size = 0;
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
		else if (cluster->size <= 1) {
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

	int64_t merges = 0;
	if (clno < 2) {
		fprintf(stdout, "merged-clusters-count: %ld\n", merges);
		XCloseDisplay(display);
		_exit(0);
	}
	fprintf(stdout, "%s\n", "merging clusters");

	for (int64_t i = 0; i != (clno - 1); ++i) {
		int64_t const ii = cl[i];
		struct cluster *curr = &clusters[ii];
		if (curr->root != curr->id) {
			fprintf(stderr, "%s\n", "error: not a cluster");
			XCloseDisplay(display);
			_exit(1);
		}
		else if (BLUE_MASK_SONIC != curr->mask) {
			fprintf(stderr, "%s\n", "error: mask");
			XCloseDisplay(display);
			_exit(1);
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
				if (iter->root == iter->id) {
					fprintf(stderr, "%s\n", "error: not a node");
					XCloseDisplay(display);
					_exit(1);
				}
				x_u = iter->x;
			}
		}
		else if (1 == curr->size) {
			continue;
		}
		else if (curr->size > 1) {
			// NOTE using the redundant logic expression for readability
			struct cluster const * const node = &clusters[curr->node];
			if (BLUE_MASK_SONIC != node->mask) {
				fprintf(stderr, "%s\n", "error: mask");
				XCloseDisplay(display);
				_exit(1);
			}
			x_u = node->x;
		}

		if (x_l == x_u) {
			fprintf(stderr, "%s\n", "error: traversal logic");
			XCloseDisplay(display);
			_exit(1);
		}
		else if (x_l >= x_u) {
			fprintf(stderr, "%s\n", "error: serious implementation flaw");
			XCloseDisplay(display);
			_exit(1);
		}

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
			if (iter->prev != iter->id) {
				fprintf(stderr, "%s\n", "error: unexpected error not a super cluster");
				XCloseDisplay(display);
				_exit(1);
			}
			else if (iter->super != iter->id) {
				fprintf(stderr, "%s\n", "error: unexpected error not a super cluster by id");
				XCloseDisplay(display);
				_exit(1);
			}
		}

		int64_t super = iter->super;
		for (int64_t j = (i + 1); j != clno; ++j) {
			int64_t const jj = cl[j];
			struct cluster *next = &clusters[jj];
			if (0 == next->size) {
				fprintf(stderr, "%s\n", "surprising landing on node");
				XCloseDisplay(display);
				_exit(1);
			}
			else if (next->root != next->id) {
				fprintf(stderr, "%s\n", "error: not a cluster");
				XCloseDisplay(display);
				_exit(1);
			}
			else if (BLUE_MASK_SONIC != next->mask) {
				fprintf(stderr, "%s\n", "error: mask");
				XCloseDisplay(display);
				_exit(1);
			}
			else if (next->y == curr->y) {
				continue;
			}
			else if ((next->y - curr->y) > 1) {
				break;
			}
			else if (next->root != next->id) {
				continue;
			}
			else if (BLUE_MASK_SONIC != next->mask) {
				fprintf(stderr, "%s\n", "error: surprising mask");
				XCloseDisplay(display);
				_exit(1);
			}
			else if (next->super == super) {
				continue;
			}
			else if ((next->super != -1) && (next->super != super)) {
				MergeSuperClusters(curr, next, clusters, super, x_l, x_u);
				if (super != curr->super) {
					if (curr->super != next->super) {
						fprintf(stderr, "%s\n", "error: surprising merge logic flaw");
						XCloseDisplay(display);
						_exit(1);
					}
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

	int64_t nodes = 0;
	for (int64_t i = 0; i != pixels; ++i) {
		struct cluster const * const c = &clusters[i];
		if (c->mask) {
			++nodes;
		}
	}

	fprintf(stdout, "nodes: %ld\n", nodes);

	int64_t total_max = 1;
	int64_t id_max = -1;
	for (int64_t i = 0; i != clno; ++i) {
		int64_t id = cl[i];
		struct cluster *iter = &clusters[id];
		if (-1 == iter->super) {
			continue;
		}
		struct cluster *super = &clusters[iter->super];
		if (super->super != super->id) {
			fprintf(stderr, "%s\n", "error: surprising not a super-cluster");
			XCloseDisplay(display);
			_exit(1);
		}
		else if (super->prev != super->id) {
			fprintf(stderr, "%s\n", "error: surprising not a super-cluster");
			XCloseDisplay(display);
			_exit(1);
		}

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

	// TODO: if there's nothing to render we just show a black window
	if (-1 == id_max) {
		fprintf(stderr, "%s\n", "error: nonsensical super-clusters found");
		XCloseDisplay(display);
		_exit(1);
	}

	{
		data = img->data;
		memset(data, 0, bytes_frame);
		int32_t *frame = (typeof(frame)) data;
		struct cluster const * const c = &clusters[id_max];
		if (c->next == c->id) {
			fprintf(stderr, "%s\n", "error: wrong super-cluster");
			XCloseDisplay(display);
			_exit(1);
		}
		struct cluster const *iter = &clusters[c->next];
		while (iter->next != iter->id) {
			int64_t const x = iter->x;
			int64_t const y = iter->y;
			int64_t const id = width * y + x;
			int32_t const rgb = (0xff << green_shift);
			frame[id] = rgb;
			struct cluster *node = &clusters[iter->node];
			while (node->node != node->root) {
				int64_t const x = node->x;
				int64_t const y = node->y;
				int64_t const id = width * y + x;
				int32_t const rgb = (0xff << green_shift);
				frame[id] = rgb;
				node = &clusters[node->node];
			}
			iter = &clusters[iter->next];
		}
		XPutImage(
				display,
				OutputWindow,
				DefaultGCOfScreen(DefaultScreenOfDisplay(display)),
				img,
				0,
				0,
				0,
				0,
				width,
				height
			 );
		XSync(display, True);
		char buff = 0;
		fprintf(stdout, "%s\n", "press any key to continue");
		fread(&buff, sizeof(buff), 1, stdin);
	}

	XFree(SizeHints);
	XFree(SizeHintsGameWindow);
	XDestroyWindow(display, OutputWindow);
	XCloseDisplay(display);
	return 0;
}
