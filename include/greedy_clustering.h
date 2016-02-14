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

 /** @file
 *
 *  ...
 */

#ifndef SCC_GREEDY_HG
#define SCC_GREEDY_HG

#include <stdbool.h>
#include <stddef.h>
#include "config.h"
#include "clustering.h"


bool scc_greedy_break_clustering(scc_Clustering* cl,
                                 scc_DataSetObject* data_set_object,
                                 size_t size_constraint,
                                 bool batch_assign);

scc_Clustering scc_get_greedy_clustering(scc_DataSetObject* data_set_object,
                                         size_t size_constraint,
                                         bool batch_assign,
                                         scc_Clabel external_cluster_label[]);


#endif
