#ifndef UT_THM_ASSERT_DIGRAPH_HG
#define UT_THM_ASSERT_DIGRAPH_HG

#include "test_suite.h"

#include <stdbool.h>

#include "../config.h"
#include "../digraph.h"
#include "../digraph_debug.h"

#define assert_valid_digraph(test_dg, vertices) _assert_valid_digraph(test_dg, vertices, #test_dg, __FILE__, __LINE__)
#define assert_free_digraph(test_dg) _assert_free_digraph(test_dg)
#define assert_sound_digraph(test_dg, vertices) _assert_sound_digraph(test_dg, vertices, #test_dg, __FILE__, __LINE__)
#define assert_equal_digraph(test_dg1, test_dg2) _assert_equal_digraph(test_dg1, test_dg2, #test_dg1, #test_dg2, __FILE__, __LINE__)
#define assert_identical_digraph(test_dg1, test_dg2) _assert_identical_digraph(test_dg1, test_dg2, #test_dg1, #test_dg2, __FILE__, __LINE__)
#define assert_empty_digraph(test_dg, vertices) _assert_empty_digraph(test_dg, vertices, #test_dg, __FILE__, __LINE__)
#define assert_balanced_digraph(test_dg, vertices, arcs_per_vertex) _assert_balanced_digraph(test_dg, vertices, arcs_per_vertex, #test_dg, __FILE__, __LINE__)

void _assert_valid_digraph(const thm_Digraph* test_dg,
						   const thm_Vid vertices,
						   const char* const name_dg,
						   const char* const file,
						   const int line) {
	if (!thm_valid_digraph(test_dg, vertices)) {
		print_error("%s is not valid\n", name_dg);
		_fail(file, line);
	}
}

void _assert_free_digraph(thm_Digraph* test_dg) {
	for (size_t i = 0; i <= test_dg->vertices; ++i) test_dg->tail_ptr[i] = 1;
	for (size_t i = 0; i < test_dg->max_arcs; ++i) test_dg->head[i] = 1;
	thm_free_digraph(test_dg);
}

void _assert_sound_digraph(const thm_Digraph* test_dg,
						   const thm_Vid vertices,
						   const char* const name_dg,
						   const char* const file,
						   const int line) {
	if (!thm_sound_digraph(test_dg, vertices)) {
		print_error("%s is not sound\n", name_dg);
		_fail(file, line);
	}
}

void _assert_equal_digraph(const thm_Digraph* test_dg1,
						   const thm_Digraph* test_dg2,
						   const char* const name_dg1,
						   const char* const name_dg2,
						   const char* const file,
						   const int line) {
	if (!thm_digraphs_equal(test_dg1, test_dg2)) {
		print_error("%s and %s are not equal\n", name_dg1, name_dg2);
		_fail(file, line);
	}
}

void _assert_identical_digraph(const thm_Digraph* test_dg1,
							   const thm_Digraph* test_dg2,
							   const char* const name_dg1,
							   const char* const name_dg2,
							   const char* const file,
							   const int line) {
	bool is_identical = true;
	if (is_identical && test_dg1->max_arcs != test_dg2->max_arcs) is_identical = false;
	if (is_identical && test_dg1->vertices != test_dg2->vertices) is_identical = false;

	if (is_identical) {
		if (!test_dg1->tail_ptr != !test_dg2->tail_ptr) {
			is_identical = false;
		} else if (test_dg1->tail_ptr) {
			for (size_t i = 0; i < test_dg1->vertices + 1; ++i) {
				if (test_dg1->tail_ptr[i] != test_dg2->tail_ptr[i]) {
					is_identical = false;
					break;
				}
			}
		}
	}

	if (is_identical) {
		if (!test_dg1->head != !test_dg2->head) {
			is_identical = false;
		} else if (test_dg1->head) {
			for (size_t i = 0; i < test_dg1->max_arcs; ++i) {
				if (test_dg1->head[i] != test_dg2->head[i]) {
					is_identical = false;
					break;
				}
			}
		}
	}

	if (!is_identical) {
		print_error("%s and %s are not identical\n", name_dg1, name_dg2);
		_fail(file, line);
	}
}

void _assert_empty_digraph(const thm_Digraph* test_dg,
						   const thm_Vid vertices,
						   const char* const name_dg,
						   const char* const file,
						   const int line) {

	if (!thm_sound_digraph(test_dg, vertices)) {
		print_error("%s is not sound\n", name_dg);
		_fail(file, line);
	} else if (test_dg->tail_ptr[vertices] != 0) {
		print_error("%s is not empty\n", name_dg);
		_fail(file, line);
	}
}

void _assert_balanced_digraph(const thm_Digraph* test_dg,
							  const thm_Vid vertices,
							  const thm_Vid arcs_per_vertex,
							  const char* const name_dg,
							  const char* const file,
							  const int line) {
	if (!thm_sound_digraph(test_dg, vertices)) {
		print_error("%s is not sound\n", name_dg);
		_fail(file, line);
	} else if (test_dg->tail_ptr[vertices] != 0) {
		bool is_balanced = true;
		for (size_t i = 0; i <= vertices; ++i) {
			if (test_dg->tail_ptr[i]  !=i * arcs_per_vertex) {
				is_balanced = false;
				break;
			}
		}
		if (!is_balanced) {
			print_error("%s is not packed balanced\n", name_dg);
			_fail(file, line);
		}
	}
}

#endif