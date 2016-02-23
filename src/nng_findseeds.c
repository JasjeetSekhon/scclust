/* ==============================================================================
 * scclust -- A C library for size constrained clustering
 * https://github.com/fsavje/scclust
 *
 * Copyright (C) 2015-2016  Fredrik Savje -- http://fredriksavje.com
 * 
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 * 
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 * 
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 * ============================================================================== */


#include "nng_findseeds.h"

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include "../include/scclust.h"
#include "error.h"
#include "digraph_core.h"
#include "digraph_operations.h"


// ==============================================================================
// Internal structs
// ==============================================================================

typedef struct iscc_fs_SortResult iscc_fs_SortResult;
struct iscc_fs_SortResult {
	scc_Dpid* inwards_count;
	scc_Dpid* sorted_vertices;
	scc_Dpid** vertex_index;
	scc_Dpid** bucket_index;
};


// ==============================================================================
// Internal function prototypes
// ==============================================================================

static scc_ErrorCode iscc_findseeds_lexical(const iscc_Digraph* nng,
                                            iscc_SeedResult* out_seeds);

static scc_ErrorCode iscc_findseeds_inwards(const iscc_Digraph* nng,
                                            bool updating,
                                            iscc_SeedResult* out_seeds);

static scc_ErrorCode iscc_findseeds_exclusion(const iscc_Digraph* nng,
                                              bool updating,
                                              iscc_SeedResult* out_seeds);

//iscc_findseeds_simulated_annealing();

//iscc_findseeds_approximation();

static scc_ErrorCode iscc_exclusion_graph(const iscc_Digraph* nng,
                                          const bool* not_excluded,
                                          iscc_Digraph* out_dg);

static inline scc_ErrorCode iscc_fs_add_seed(scc_Dpid s,
                                             iscc_SeedResult* out_seeds);

static inline bool iscc_fs_check_neighbors_marks(scc_Dpid cv,
                                                 const iscc_Digraph* nng,
                                                 const bool marks[static nng->vertices]);

static inline void iscc_fs_mark_seed_neighbors(scc_Dpid s,
                                               const iscc_Digraph* nng,
                                               bool marks[static nng->vertices]);

static scc_ErrorCode iscc_fs_sort_by_inwards(const iscc_Digraph* nng,
                                             bool make_indices,
                                             iscc_fs_SortResult* out_sort);

static void iscc_fs_free_sort_result(iscc_fs_SortResult* sr);

static inline void iscc_fs_decrease_v_in_sort(scc_Dpid v_to_decrease,
                                              scc_Dpid inwards_count[restrict],
                                              scc_Dpid* vertex_index[restrict],
                                              scc_Dpid* bucket_index[restrict],
                                              scc_Dpid* current_pos);

#ifdef SCC_STABLE_CLUSTERING

	static inline void iscc_fs_debug_bucket_sort(const scc_Dpid* bucket_start,
	                                             scc_Dpid* pos,
	                                             const scc_Dpid inwards_count[],
	                                             scc_Dpid* vertex_index[]);

	static inline void iscc_fs_debug_check_sort(const scc_Dpid* current_pos,
	                                            const scc_Dpid* last_pos,
	                                            const scc_Dpid inwards_count[]);

#endif


// ==============================================================================
// External function implementations
// ==============================================================================

scc_ErrorCode iscc_find_seeds(const iscc_Digraph* const nng,
                              const scc_SeedMethod sm,
                              iscc_SeedResult* const out_seeds)
{
	assert(iscc_digraph_is_initialized(nng));
	assert(out_seeds != NULL);
	assert(out_seeds->count == 0);
	assert(out_seeds->seeds == NULL);

	if (out_seeds->capacity < 128) out_seeds->capacity = 128;

	scc_ErrorCode ec;
	switch(sm) {
		case SCC_SM_LEXICAL:
			ec = iscc_findseeds_lexical(nng, out_seeds);
			break;

		case SCC_SM_INWARDS_ORDER:
			ec = iscc_findseeds_inwards(nng, false, out_seeds);
			break;

		case SCC_SM_INWARDS_UPDATING:
			ec = iscc_findseeds_inwards(nng, true, out_seeds);
			break;

		case SCC_SM_EXCLUSION_ORDER:
			ec = iscc_findseeds_exclusion(nng, false, out_seeds);
			break;

		case SCC_SM_EXCLUSION_UPDATING:
			ec = iscc_findseeds_exclusion(nng, true, out_seeds);
			break;

		default:
			assert(false);
			ec = iscc_make_error(SCC_ER_NOT_IMPLEMENTED);
			break;
	}

	if (ec == SCC_ER_OK) {
		assert(out_seeds->seeds != NULL);
		if ((out_seeds->count < out_seeds->capacity) && (out_seeds->count > 0)) {
			scc_Dpid* const tmp_seed_ptr = realloc(out_seeds->seeds, sizeof(scc_Dpid[out_seeds->count]));
			if (tmp_seed_ptr != NULL) {
				out_seeds->seeds = tmp_seed_ptr;
				out_seeds->capacity = out_seeds->count;
			}
		}
	} else {
		*out_seeds = ISCC_NULL_SEED_RESULT;
	}

	return ec;
}


// ==============================================================================
// Internal function implementations 
// ==============================================================================

static scc_ErrorCode iscc_findseeds_lexical(const iscc_Digraph* const nng,
                                            iscc_SeedResult* const out_seeds)
{
	assert(iscc_digraph_is_initialized(nng));
	assert(out_seeds != NULL);
	assert(out_seeds->capacity >= 128);
	assert(out_seeds->count == 0);
	assert(out_seeds->seeds == NULL);

	bool* const marks = calloc(nng->vertices, sizeof(bool));
	out_seeds->seeds = malloc(sizeof(scc_Dpid[out_seeds->capacity]));
	if ((marks == NULL) || (out_seeds->seeds == NULL)) {
		free(marks);
		free(out_seeds->seeds);
		return iscc_make_error(SCC_ER_NO_MEMORY);
	}

	scc_ErrorCode ec;
	assert(nng->vertices < SCC_DPID_MAX);
	const scc_Dpid vertices = (scc_Dpid) nng->vertices; // If `scc_Dpid` is signed
	for (scc_Dpid cv = 0; cv < vertices; ++cv) {
		if (iscc_fs_check_neighbors_marks(cv, nng, marks)) {
			assert(nng->tail_ptr[cv] != nng->tail_ptr[cv + 1]);

			if ((ec = iscc_fs_add_seed(cv, out_seeds)) != SCC_ER_OK) {
				free(marks);
				free(out_seeds->seeds);
				return ec;
			}

			iscc_fs_mark_seed_neighbors(cv, nng, marks);
		}
	}

	free(marks);

	return iscc_no_error();
}


static scc_ErrorCode iscc_findseeds_inwards(const iscc_Digraph* const nng,
                                            const bool updating,
                                            iscc_SeedResult* const out_seeds)
{
	assert(iscc_digraph_is_initialized(nng));
	assert(out_seeds != NULL);
	assert(out_seeds->capacity >= 128);
	assert(out_seeds->count == 0);
	assert(out_seeds->seeds == NULL);

	scc_ErrorCode ec;
	iscc_fs_SortResult sort;
	if ((ec = iscc_fs_sort_by_inwards(nng, updating, &sort)) != SCC_ER_OK) {
		return ec;
	}

	bool* const marks = calloc(nng->vertices, sizeof(bool));
	out_seeds->seeds = malloc(sizeof(scc_Dpid[out_seeds->capacity]));
	if ((marks == NULL) || (out_seeds->seeds == NULL)) {
		iscc_fs_free_sort_result(&sort);
		free(marks);
		free(out_seeds->seeds);
		return iscc_make_error(SCC_ER_NO_MEMORY);
	}

	const scc_Dpid* const sorted_v_stop = sort.sorted_vertices + nng->vertices;
	for (scc_Dpid* sorted_v = sort.sorted_vertices;
	        sorted_v != sorted_v_stop; ++sorted_v) {

		#if defined(SCC_STABLE_CLUSTERING) && !defined(NDEBUG)
			iscc_fs_debug_check_sort(sorted_v, sorted_v_stop - 1, sort.inwards_count);
		#endif

		if (iscc_fs_check_neighbors_marks(*sorted_v, nng, marks)) {
			assert(nng->tail_ptr[*sorted_v] != nng->tail_ptr[*sorted_v + 1]);

			if ((ec = iscc_fs_add_seed(*sorted_v, out_seeds)) != SCC_ER_OK) {
				iscc_fs_free_sort_result(&sort);
				free(marks);
				free(out_seeds->seeds);
				return ec;
			}

			iscc_fs_mark_seed_neighbors(*sorted_v, nng, marks);

			if (updating) {
				const scc_Dpid* const v_arc_stop = nng->head + nng->tail_ptr[*sorted_v + 1];
				for (const scc_Dpid* v_arc = nng->head + nng->tail_ptr[*sorted_v];
				        v_arc != v_arc_stop; ++v_arc) {
					const scc_Dpid* const v_arc_arc_stop = nng->head + nng->tail_ptr[*v_arc + 1];
					for (scc_Dpid* v_arc_arc = nng->head + nng->tail_ptr[*v_arc];
					        v_arc_arc != v_arc_arc_stop; ++v_arc_arc) {
						// Only decrease if vertex can be seed (i.e., not already assigned and not already considered)
						if (!marks[*v_arc_arc] && sorted_v < sort.vertex_index[*v_arc_arc]) {
							iscc_fs_decrease_v_in_sort(*v_arc_arc, sort.inwards_count, sort.vertex_index, sort.bucket_index, sorted_v);
						}
					}
				}
			}
		}
	}

	iscc_fs_free_sort_result(&sort);
	free(marks);

	return iscc_no_error();
}


static scc_ErrorCode iscc_findseeds_exclusion(const iscc_Digraph* const nng,
                                              const bool updating,
                                              iscc_SeedResult* const out_seeds)
{
	assert(iscc_digraph_is_initialized(nng));
	assert(out_seeds != NULL);
	assert(out_seeds->capacity >= 128);
	assert(out_seeds->count == 0);
	assert(out_seeds->seeds == NULL);

	bool* const not_excluded = malloc(sizeof(bool[nng->vertices]));
	if (not_excluded == NULL) {
		return iscc_make_error(SCC_ER_NO_MEMORY);
	}

	for (size_t v = 0; v < nng->vertices; ++v) {
		not_excluded[v] = (nng->tail_ptr[v] != nng->tail_ptr[v + 1]);
	}

	scc_ErrorCode ec;
	iscc_Digraph exclusion_graph;
	if ((ec = iscc_exclusion_graph(nng, not_excluded, &exclusion_graph)) != SCC_ER_OK) {
		free(not_excluded);
		return ec;
	}

	iscc_fs_SortResult sort;
	if ((ec = iscc_fs_sort_by_inwards(&exclusion_graph, updating, &sort)) != SCC_ER_OK) {
		free(not_excluded);
		iscc_free_digraph(&exclusion_graph);
		return ec;
	}

	out_seeds->seeds = malloc(sizeof(scc_Dpid[out_seeds->capacity]));
	if (out_seeds->seeds == NULL) {
		free(not_excluded);
		iscc_free_digraph(&exclusion_graph);
		iscc_fs_free_sort_result(&sort);
		return iscc_make_error(SCC_ER_NO_MEMORY);
	}

	const scc_Dpid* const sorted_v_stop = sort.sorted_vertices + nng->vertices;
	for (scc_Dpid* sorted_v = sort.sorted_vertices;
	        sorted_v != sorted_v_stop; ++sorted_v) {

		#if defined(SCC_STABLE_CLUSTERING) && !defined(NDEBUG)
			iscc_fs_debug_check_sort(sorted_v, sorted_v_stop - 1, sort.inwards_count);
		#endif

		if (not_excluded[*sorted_v]) {
			assert(nng->tail_ptr[*sorted_v] != nng->tail_ptr[*sorted_v + 1]);

			if ((ec = iscc_fs_add_seed(*sorted_v, out_seeds)) != SCC_ER_OK) {
				free(not_excluded);
				iscc_free_digraph(&exclusion_graph);
				iscc_fs_free_sort_result(&sort);
				free(out_seeds->seeds);
				return ec;
			}

			not_excluded[*sorted_v] = false;
				
			const scc_Dpid* const ex_arc_stop = exclusion_graph.head + exclusion_graph.tail_ptr[*sorted_v + 1];
			for (const scc_Dpid* ex_arc = exclusion_graph.head + exclusion_graph.tail_ptr[*sorted_v];
			        ex_arc != ex_arc_stop; ++ex_arc) {
				if (not_excluded[*ex_arc]) {
					not_excluded[*ex_arc] = false;

					if (updating) {
						const scc_Dpid* const ex_arc_arc_stop = exclusion_graph.head + exclusion_graph.tail_ptr[*ex_arc + 1];
						for (scc_Dpid* ex_arc_arc = exclusion_graph.head + exclusion_graph.tail_ptr[*ex_arc];
						        ex_arc_arc != ex_arc_arc_stop; ++ex_arc_arc) {
							if (not_excluded[*ex_arc_arc]) {
								iscc_fs_decrease_v_in_sort(*ex_arc_arc, sort.inwards_count, sort.vertex_index, sort.bucket_index, sorted_v);
							}
						}
					}
				}
			}
		}
	}

	free(not_excluded);
	iscc_free_digraph(&exclusion_graph);
	iscc_fs_free_sort_result(&sort);

	return iscc_no_error();
}


/*
Exclusion graph does not give one arc optimality

     *            *
     |            |
     v            v
  *->*->*->*<->*<-*<-*<-*
     ^            ^
     |            |
     *            *

bool iscc_findseeds_onearc_updating(const scc_Digraph* const nng, ...) {
	//Among those with 0 inwards arcs, sort on exclusion graph 
}
*/


static scc_ErrorCode iscc_exclusion_graph(const iscc_Digraph* const nng,
                                          const bool* const not_excluded,
                                          iscc_Digraph* const out_dg)
{
	assert(iscc_digraph_is_initialized(nng));
	assert(not_excluded != NULL);

	scc_ErrorCode ec;

	iscc_Digraph nng_transpose;
	ec = iscc_digraph_transpose(nng, &nng_transpose);
	if (ec != SCC_ER_OK) return ec;

	iscc_Digraph nng_nng_transpose;
	ec = iscc_adjacency_product(nng, &nng_transpose, true, false, &nng_nng_transpose);
	iscc_free_digraph(&nng_transpose);
	if (ec != SCC_ER_OK) return ec;

	/* In `out_dg`, all vertices with zero outwards arcs in `nng` will have
	 * arcs pointing to vertices that points to them in `nng`. These vertices are
	 * excluded from the the beginning (due to zero arcs), thus their outwards arcs
	 * are not necessary. In fact, to keep these arcs leads to that the sorting on
	 * inwards arcs by `iscc_fs_sort_by_inwards()` becomes wrong. Remove
	 * by calling `iscc_digraph_union_and_delete` with `not_excluded`.
	 */
	const iscc_Digraph nng_sum[2] = { *nng, nng_nng_transpose };
	ec = iscc_digraph_union_and_delete(2, nng_sum, not_excluded, out_dg);
	iscc_free_digraph(&nng_nng_transpose);
	if (ec != SCC_ER_OK) return ec;

	return iscc_no_error();
}


static inline scc_ErrorCode iscc_fs_add_seed(const scc_Dpid s,
                                             iscc_SeedResult* const out_seeds)
{
	assert(out_seeds != NULL);
	assert(out_seeds->seeds == NULL);
	assert(out_seeds->capacity >= 128);
	assert(out_seeds->count <= out_seeds->capacity);
	
	if (out_seeds->count == out_seeds->capacity) {
		out_seeds->capacity = out_seeds->capacity + (out_seeds->capacity >> 3) + 1024;
		if (out_seeds->capacity > SCC_CLABEL_MAX) out_seeds->capacity = SCC_CLABEL_MAX;
		scc_Dpid* const seeds_tmp_ptr = realloc(out_seeds->seeds, sizeof(scc_Dpid[out_seeds->capacity]));
		if (seeds_tmp_ptr == NULL) return iscc_make_error(SCC_ER_NO_MEMORY);
		out_seeds->seeds = seeds_tmp_ptr;
	}
	out_seeds->seeds[out_seeds->count] = s;
	++(out_seeds->count);
	if (out_seeds->count == SCC_CLABEL_MAX) return iscc_make_error(SCC_ER_TOO_LARGE_PROBLEM);

	return iscc_no_error();
}


static inline bool iscc_fs_check_neighbors_marks(const scc_Dpid cv,
                                                 const iscc_Digraph* const nng,
                                                 const bool marks[const static nng->vertices])
{
	if (marks[cv]) return false;

	const scc_Dpid* cv_arc = nng->head + nng->tail_ptr[cv];
	const scc_Dpid* const cv_arc_stop = nng->head + nng->tail_ptr[cv + 1];
	if (cv_arc == cv_arc_stop) return false;

	for (; cv_arc != cv_arc_stop; ++cv_arc) { 
		if (marks[*cv_arc]) return false;
	}

	return true;
}


static inline void iscc_fs_mark_seed_neighbors(const scc_Dpid s,
                                               const iscc_Digraph* const nng,
                                               bool marks[const static nng->vertices])
{
	assert(!marks[s]);
	marks[s] = true;

	const scc_Dpid* const s_arc_stop = nng->head + nng->tail_ptr[s + 1];
	for (const scc_Dpid* s_arc = nng->head + nng->tail_ptr[s];
	        s_arc != s_arc_stop; ++s_arc) {
		assert(!marks[*s_arc] || (*s_arc == s));
		marks[*s_arc] = true;
	}
}


static scc_ErrorCode iscc_fs_sort_by_inwards(const iscc_Digraph* const nng,
                                             const bool make_indices,
                                             iscc_fs_SortResult* const out_sort)
{
	assert(iscc_digraph_is_initialized(nng));
	assert(out_sort != NULL);

	const size_t vertices = nng->vertices;

	*out_sort = (iscc_fs_SortResult) {
		.inwards_count = calloc(vertices, sizeof(scc_Dpid)),
		.sorted_vertices = malloc(sizeof(scc_Dpid[vertices])),
		.vertex_index = NULL,
		.bucket_index = NULL,
	};

	if ((out_sort->inwards_count == NULL) || (out_sort->sorted_vertices == NULL)) {
		iscc_fs_free_sort_result(out_sort);
		return iscc_make_error(SCC_ER_NO_MEMORY);
	}

	const scc_Dpid* const arc_stop = nng->head + nng->tail_ptr[vertices];
	for (const scc_Dpid* arc = nng->head; arc != arc_stop; ++arc) {
		++out_sort->inwards_count[*arc];
	}

	// Dynamic alloc is slightly faster but more error-prone
	// Add if turns out to be bottleneck
	scc_Dpid max_inwards_tmp = 0;
	for (size_t v = 0; v < vertices; ++v) {
		if (max_inwards_tmp < out_sort->inwards_count[v]) max_inwards_tmp = out_sort->inwards_count[v];
	}
	assert(max_inwards_tmp >= 0);
	const size_t max_inwards = (size_t) max_inwards_tmp; // If `scc_Dpid` is signed

	size_t* bucket_count = calloc(max_inwards + 1, sizeof(size_t));
	out_sort->bucket_index = malloc(sizeof(scc_Dpid*[max_inwards + 1]));
	if ((bucket_count == NULL) || (out_sort->bucket_index == NULL)) {
		free(bucket_count);
		iscc_fs_free_sort_result(out_sort);
		return iscc_make_error(SCC_ER_NO_MEMORY);
	}

	for (size_t v = 0; v < vertices; ++v) {
		++bucket_count[out_sort->inwards_count[v]];
	}

	size_t bucket_cumsum = 0;
	for (size_t b = 0; b <= max_inwards; ++b) {
		bucket_cumsum += bucket_count[b];
		out_sort->bucket_index[b] = out_sort->sorted_vertices + bucket_cumsum;
	}
	free(bucket_count);

	assert(vertices < SCC_DPID_MAX);
	if (make_indices) {
		out_sort->vertex_index = malloc(sizeof(scc_Dpid*[vertices]));
		if (out_sort->vertex_index == NULL) {
			iscc_fs_free_sort_result(out_sort);
			return iscc_make_error(SCC_ER_NO_MEMORY);
		}
		for (scc_Dpid v = (scc_Dpid) vertices; v > 0; ) {
			--v;
			--out_sort->bucket_index[out_sort->inwards_count[v]];
			*out_sort->bucket_index[out_sort->inwards_count[v]] = v;
			out_sort->vertex_index[v] = out_sort->bucket_index[out_sort->inwards_count[v]];
		}
	} else {
		for (scc_Dpid v = (scc_Dpid) vertices; v > 0; ) {
			--v;
			--(out_sort->bucket_index[out_sort->inwards_count[v]]);
			*out_sort->bucket_index[out_sort->inwards_count[v]] = v;
		}

		free(out_sort->inwards_count);
		free(out_sort->bucket_index);
		out_sort->inwards_count = NULL;
		out_sort->bucket_index = NULL;
	}

	return iscc_no_error();
}


static void iscc_fs_free_sort_result(iscc_fs_SortResult* const sr)
{
	if (sr != NULL) {
		free(sr->inwards_count);
		free(sr->sorted_vertices);
		free(sr->vertex_index);
		free(sr->bucket_index);
		*sr = (iscc_fs_SortResult) { NULL, NULL, NULL, NULL };
	}
}


static inline void iscc_fs_decrease_v_in_sort(const scc_Dpid v_to_decrease,
                                              scc_Dpid inwards_count[restrict const],
                                              scc_Dpid* vertex_index[restrict const],
                                              scc_Dpid* bucket_index[restrict const],
                                              scc_Dpid* const current_pos)
{
	// Assert that vertex index is correct
	assert(v_to_decrease == *vertex_index[v_to_decrease]);

	// Find vertices to move
	scc_Dpid* const move_from = vertex_index[v_to_decrease];
	scc_Dpid* move_to = bucket_index[inwards_count[v_to_decrease]];
	if (move_to <= current_pos) {
		move_to = current_pos + 1;
		bucket_index[inwards_count[v_to_decrease] - 1] = move_to;
	} 

	// Assert that swap vertices have the same count
	assert(inwards_count[*move_from] == inwards_count[*move_to]);

	// Update bucket index
	bucket_index[inwards_count[v_to_decrease]] = move_to + 1;

	// Decrease count on vertex
	--inwards_count[v_to_decrease];

	// Check so list not already sorted
	if (move_from != move_to) {
		// Do swap
		*move_from = *move_to;
		*move_to = v_to_decrease;

		// Update vertex index
		vertex_index[*move_to] = move_to;
		vertex_index[*move_from] = move_from;

		#ifdef SCC_STABLE_CLUSTERING
			// Sort old bucket by vertex ID
			iscc_fs_debug_bucket_sort(move_to + 1, move_from, inwards_count, vertex_index);
		#endif
	}

	#ifdef SCC_STABLE_CLUSTERING
		// If new bucket start on or before current_pos in the sorted vertices, move it to next in line
		if (bucket_index[inwards_count[v_to_decrease]] <= current_pos) {
			bucket_index[inwards_count[v_to_decrease]] = current_pos + 1;
		}
		// Sort new bucket by vertex ID
		iscc_fs_debug_bucket_sort(bucket_index[inwards_count[v_to_decrease]],
		                          move_to, inwards_count, vertex_index);
	#endif
}


#ifdef SCC_STABLE_CLUSTERING

	static inline void iscc_fs_debug_bucket_sort(const scc_Dpid* const bucket_start,
	                                             scc_Dpid* pos,
	                                             const scc_Dpid inwards_count[const],
	                                             scc_Dpid* vertex_index[const])
	{
		scc_Dpid tmp_v = *pos;
		for (; pos != bucket_start; --pos) {
			assert(inwards_count[tmp_v] == inwards_count[*(pos - 1)]);
			if (tmp_v >= *(pos - 1)) break;
			*pos = *(pos - 1);
			vertex_index[*pos] = pos;
		}
		*pos = tmp_v;
		vertex_index[*pos] = pos;
	}


	static inline void iscc_fs_debug_check_sort(const scc_Dpid* current_pos,
	                                            const scc_Dpid* const last_pos,
	                                            const scc_Dpid inwards_count[const])
	{
		for (; current_pos != last_pos; ++current_pos) {
			assert(inwards_count[*(current_pos)] <= inwards_count[*(current_pos + 1)]);
			if (inwards_count[*(current_pos)] == inwards_count[*(current_pos + 1)]) {
				assert(*(current_pos) < *(current_pos + 1));
			}
		}
	}

#endif