/* -*- Mode: C; c-basic-offset:2 ; indent-tabs-mode:nil -*- */
/*
 * Copyright (c) 2014-2016 Los Alamos National Security, LLC.  All rights
 *                         reserved. 
 * $COPYRIGHT$
 * 
 * Additional copyrights may follow
 * 
 * $HEADER$
 */

#include "hio_internal.h"

#include <stdlib.h>
#include <string.h>
#include <assert.h>

static hio_var_enum_value_t hioi_dataset_fs_type_enum_values[] = {
  {.string_value = "default", .value = HIO_FS_TYPE_DEFAULT},
  {.string_value = "lustre", .value = HIO_FS_TYPE_LUSTRE},
  {.string_value = "gpfs", .value = HIO_FS_TYPE_GPFS},
  {.string_value = "datawarp", .value = HIO_FS_TYPE_DATAWARP},
};

static hio_var_enum_t hioi_dataset_fs_type_enum = {
  .count  = 4,
  .values = hioi_dataset_fs_type_enum_values,
};

static int hioi_dataset_data_lookup (hio_context_t context, const char *name, hio_dataset_data_t **data) {
  hio_dataset_data_t *ds_data;

  /* look for existing persistent data */
  hioi_object_lock (&context->c_object);
  hioi_list_foreach (ds_data, context->c_ds_data, hio_dataset_data_t, dd_list) {
    if (0 == strcmp (ds_data->dd_name, name)) {
      hioi_object_unlock (&context->c_object);
      *data = ds_data;
      return HIO_SUCCESS;
    }
  }

  /* allocate new persistent dataset data and add it to the context */
  ds_data = (hio_dataset_data_t *) calloc (1, sizeof (*ds_data));
  if (NULL == ds_data) {
    hioi_object_unlock (&context->c_object);
    return HIO_ERR_OUT_OF_RESOURCE;
  }

  ds_data->dd_name = strdup (name);
  if (NULL == ds_data->dd_name) {
    hioi_object_unlock (&context->c_object);
    free (ds_data);
    return HIO_ERR_OUT_OF_RESOURCE;
  }

  ds_data->dd_last_id = -1;

  hioi_list_init (ds_data->dd_backend_data);

  hioi_list_append (ds_data, context->c_ds_data, dd_list);
  hioi_object_unlock (&context->c_object);

  *data = ds_data;

  return HIO_SUCCESS;
}

static hio_return_t hioi_dataset_element_open_stub (hio_dataset_t dataset, hio_element_t element) {
  return HIO_ERR_BAD_PARAM;
}

static hio_return_t hioi_dataset_close_stub (hio_dataset_t dataset) {
  return HIO_ERR_BAD_PARAM;
}

static void hioi_dataset_release (hio_object_t object) {
  hio_dataset_t dataset = (hio_dataset_t) object;
  hio_element_t element, next;

  hioi_list_foreach_safe(element, next, dataset->ds_elist, struct hio_element, e_list) {
    hioi_list_remove(element, e_list);
    hioi_object_release (&element->e_object);
  }
}

hio_dataset_t hioi_dataset_alloc (hio_context_t context, const char *name, int64_t id,
                                  int flags, hio_dataset_mode_t mode) {
  size_t dataset_size = context->c_ds_size;
  hio_dataset_t new_dataset;
  int rc;

  /* bozo check for invalid dataset object size */
  assert (dataset_size >= sizeof (*new_dataset));

  /* allocate new dataset object */
  new_dataset = (hio_dataset_t) hioi_object_alloc (name, HIO_OBJECT_TYPE_DATASET,&context->c_object,
                                                   dataset_size, hioi_dataset_release);
  if (NULL == new_dataset) {
    return NULL;
  }

  /* initialize counters */
  atomic_init (&new_dataset->ds_stat.s_wcount, 0);
  atomic_init (&new_dataset->ds_stat.s_rcount, 0);

  /* lookup/allocate persistent dataset data. this data will keep track of per-dataset
   * statistics (average write time, last successful checkpoint, etc) */
  rc = hioi_dataset_data_lookup (context, name, &new_dataset->ds_data);
  if (HIO_SUCCESS != rc) {
    hioi_object_release (&new_dataset->ds_object);
    return NULL;
  }

  new_dataset->ds_id = id;
  new_dataset->ds_id_requested = id;
  new_dataset->ds_flags = flags;
  new_dataset->ds_mode = mode;
  new_dataset->ds_close = hioi_dataset_close_stub;
  new_dataset->ds_element_open = hioi_dataset_element_open_stub;
#if HIO_MPI_HAVE(3)
  new_dataset->ds_shared_win = MPI_WIN_NULL;
  new_dataset->ds_map.map_elements.md_win = MPI_WIN_NULL;
  new_dataset->ds_map.map_segments.md_win = MPI_WIN_NULL;
#endif

  new_dataset->ds_fsattr.fs_type = HIO_FS_TYPE_DEFAULT;
  hioi_config_add (context, &new_dataset->ds_object, &new_dataset->ds_fsattr.fs_type,
                   "dataset_filesystem_type", HIO_CONFIG_TYPE_INT32, &hioi_dataset_fs_type_enum,
                   "Type of filesystem this dataset resides on", HIO_VAR_FLAG_READONLY);

  hioi_config_add (context, &new_dataset->ds_object, &new_dataset->ds_data->dd_average_size,
                   "dataset_expected_size", HIO_CONFIG_TYPE_INT64, NULL,
                   "Expected global size of this dataset", 0);

  /* default to a megabyte for the buffer size */
  new_dataset->ds_buffer_size = 1 << 20;
  hioi_config_add (context, &new_dataset->ds_object, &new_dataset->ds_buffer_size,
                   "dataset_buffer_size", HIO_CONFIG_TYPE_INT64, NULL,
                   "Buffer size to use for aggregating read and write operations", 0);

  /* set up performance variables */
  hioi_perf_add (context, &new_dataset->ds_object, &new_dataset->ds_stat.s_bread, "bytes_read",
                 HIO_CONFIG_TYPE_UINT64, NULL, "Total number of bytes read in this dataset instance", 0);

  hioi_perf_add (context, &new_dataset->ds_object, &new_dataset->ds_stat.s_bwritten, "bytes_written",
                 HIO_CONFIG_TYPE_UINT64, NULL, "Total number of bytes written in this dataset instance", 0);

  hioi_list_init (new_dataset->ds_elist);

  return new_dataset;
}

void hioi_dataset_add_element (hio_dataset_t dataset, hio_element_t element) {
  hioi_list_append (element, dataset->ds_elist, e_list);
}

hio_dataset_backend_data_t *hioi_dbd_alloc (hio_dataset_data_t *data, const char *backend_name, size_t size) {
  hio_dataset_backend_data_t *new_backend_data;

  assert (size >= sizeof (*new_backend_data));

  new_backend_data = calloc (1, size);
  if (NULL == new_backend_data) {
    return NULL;
  }

  new_backend_data->dbd_backend_name = strdup (backend_name);
  if (NULL == new_backend_data->dbd_backend_name) {
    free (new_backend_data);
    return NULL;
  }

  hioi_list_append (new_backend_data, data->dd_backend_data, dbd_list);

  return new_backend_data;
}

/**
 * Retrieve stored backend data
 *
 * @param[in] data         dataset persistent data structure
 * @param[in] backend_name name of the requesting backend
 */
hio_dataset_backend_data_t *hioi_dbd_lookup_backend_data (hio_dataset_data_t *data, const char *backend_name) {
  hio_dataset_backend_data_t *dbd_data;

  hioi_list_foreach (dbd_data, data->dd_backend_data, hio_dataset_backend_data_t, dbd_list) {
    if (0 == strcmp (dbd_data->dbd_backend_name, backend_name)) {
      return dbd_data;
    }
  }

  return NULL;
}

#if HIO_MPI_HAVE(1)
int hioi_dataset_gather_manifest_comm (hio_dataset_t dataset, MPI_Comm comm, unsigned char **data_out, size_t *data_size_out,
                                       bool compress_data, bool simple) {
  hio_context_t context = (hio_context_t) dataset->ds_object.parent;
  long int recv_size_left = 0, recv_size_right = 0, send_size, alloc_size;
  int left, right, parent, c_rank, c_size, rc, nreqs = 0;
  unsigned char *remote_data;
  MPI_Request reqs[2];

  hioi_timed_call(rc = hioi_manifest_serialize (dataset, data_out, data_size_out, compress_data, simple));
  if (HIO_SUCCESS != rc) {
    return rc;
  }

  if (!hioi_context_using_mpi (context)) {
    return HIO_SUCCESS;
  }

  MPI_Comm_size (comm, &c_size);
  MPI_Comm_rank (comm, &c_rank);

  parent = (c_rank - 1) >> 1;
  left = c_rank * 2 + 1;
  right = left + 1;

  if (1 == c_size) {
    return HIO_SUCCESS;
  }

  /* the needs of this routine are a little more complicated than MPI_Reduce. the data size may
   * grow as the results are reduced. this function implements a basic reduction algorithm on
   * the hio dataset */

  if (right < c_size) {
    MPI_Irecv (&recv_size_right, 1, MPI_LONG, right, 1001, comm, reqs + 1);
    ++nreqs;
  }

  if (left < c_size) {
    MPI_Irecv (&recv_size_left, 1, MPI_LONG, left, 1001, comm, reqs);
    ++nreqs;
  }

  if (nreqs) {
    hioi_log (context, HIO_VERBOSE_DEBUG_LOW, "waiting on %d requests", nreqs);

    hioi_timed_call(MPI_Waitall (nreqs, reqs, MPI_STATUSES_IGNORE));

    alloc_size = recv_size_right > recv_size_left ? recv_size_right : recv_size_left;
    if (0 >= alloc_size) {
      /* internal error for now */
      return HIO_ERROR;
    }

    remote_data = malloc (alloc_size);
    if (NULL == remote_data) {
      return HIO_ERR_OUT_OF_RESOURCE;
    }

    if (right < c_size) {
      hioi_log (context, HIO_VERBOSE_DEBUG_LOW, "receiving %lu bytes of manifest data from %d", recv_size_right,
                right);
      hioi_timed_call(MPI_Recv (remote_data, recv_size_right, MPI_CHAR, right, 1002, comm, MPI_STATUS_IGNORE));
      hioi_log (context, HIO_VERBOSE_DEBUG_LOW, "merging manifest data from %d", right);
      hioi_timed_call(hioi_manifest_merge_data2 (data_out, data_size_out, remote_data, recv_size_right));
    }

    hioi_log (context, HIO_VERBOSE_DEBUG_LOW, "receiving %lu bytes of manifest data from %d", recv_size_left,
              left);
    hioi_timed_call(MPI_Recv (remote_data, recv_size_left, MPI_CHAR, left, 1002, comm, MPI_STATUS_IGNORE));
    hioi_log (context, HIO_VERBOSE_DEBUG_LOW, "merging manifest data from %d", left);
    hioi_timed_call(hioi_manifest_merge_data2 (data_out, data_size_out, remote_data, recv_size_left));
    free (remote_data);
  }

  if (parent >= 0) {
    send_size = *data_size_out;
    hioi_log (context, HIO_VERBOSE_DEBUG_LOW, "sending %lu bytes of manifest data from %d to %d", send_size,
              c_rank, parent);

    MPI_Send (&send_size, 1, MPI_LONG, parent, 1001, comm);
    MPI_Send (*data_out, send_size, MPI_CHAR, parent, 1002, comm);

    free (*data_out);
    *data_out = NULL;
    *data_size_out = 0;
  }

  return HIO_SUCCESS;
}
#endif

int hioi_dataset_gather_manifest (hio_dataset_t dataset, unsigned char **data_out, size_t *data_size_out,
                                  bool compress_data, bool simple) {
#if HIO_MPI_HAVE(1)
  hio_context_t context = hioi_object_context (&dataset->ds_object);

  return hioi_dataset_gather_manifest_comm (dataset, context->c_comm, data_out, data_size_out, compress_data, simple);
#else
  return hioi_manifest_serialize (dataset, data_out, data_size_out, compress_data, simple);
#endif
}

#if HIO_MPI_HAVE(1)
int hioi_dataset_scatter_comm (hio_dataset_t dataset, MPI_Comm comm, const unsigned char *manifest, size_t manifest_size, int rc) {
  hio_context_t context = hioi_object_context (&dataset->ds_object);
  int rank;
  long ar_data[5];

  if (!hioi_context_using_mpi (context)) {
    return HIO_SUCCESS;
  }

  MPI_Comm_rank (comm, &rank);

  ar_data[0] = rc;
  ar_data[1] = (long) manifest_size;
  ar_data[2] = dataset->ds_flags;
  ar_data[3] = dataset->ds_fsattr.fs_scount;
  ar_data[4] = dataset->ds_fsattr.fs_ssize;

  rc = MPI_Bcast (ar_data, 5, MPI_LONG, 0, comm);
  if (MPI_SUCCESS != rc) {
    return hioi_err_mpi (rc);
  }

  if (HIO_SUCCESS != ar_data[0]) {
    return ar_data[0];
  }

  manifest_size = (size_t) ar_data[1];

  if (manifest_size) {
    if (0 != rank) {
      manifest = malloc (manifest_size);
      assert (NULL != manifest);
    }

    rc = MPI_Bcast ((void *) manifest, manifest_size, MPI_BYTE, 0, comm);
    if (MPI_SUCCESS != rc) {
      return hioi_err_mpi (rc);
    }

    rc = hioi_manifest_deserialize (dataset, manifest, manifest_size);
    if (HIO_SUCCESS != rc) {
      hioi_log (context, HIO_VERBOSE_DEBUG_LOW, "hioi_dataset_scatter_comm: failed to deserialize incoming manifest. rc: %d",
                rc);
    }

    if (0 != rank) {
      free ((void *) manifest);
    }
  }

  /* copy flags determined by rank 0 */
  dataset->ds_flags = ar_data[2];
  dataset->ds_fsattr.fs_scount = ar_data[3];
  dataset->ds_fsattr.fs_ssize = ar_data[4];

  return rc;
}

int hioi_dataset_scatter_unique (hio_dataset_t dataset, const unsigned char *manifest, size_t manifest_size, int rc) {
  hio_context_t context = (hio_context_t) dataset->ds_object.parent;
  int *ranks = NULL, *all_ranks, rank_count = 0, io_leader, mpirc;
  MPI_Comm io_comm;

  /* first reduce the current error code */
  mpirc = MPI_Allreduce (MPI_IN_PLACE, &rc, 1, MPI_INT, MPI_MIN, context->c_comm);
  if (MPI_SUCCESS != mpirc) {
    return hioi_err_mpi (mpirc);
  }

  if (HIO_SUCCESS != rc) {
    return rc;
  }

  if (manifest) {
    rc = hioi_manifest_ranks (manifest, manifest_size, &ranks, &rank_count);
    if (HIO_SUCCESS != rc) {
      return rc;
    }
  }

  all_ranks = calloc (context->c_size, sizeof (int));
  if (NULL == all_ranks) {
    free (ranks);
    return HIO_ERR_OUT_OF_RESOURCE;
  }

  if (ranks) {
    for (int i = 0 ; i < rank_count ; ++i) {
      if (ranks[i] >= context->c_size) {
        free (all_ranks);
        free (ranks);
        return HIO_ERR_BAD_PARAM;
      }

      all_ranks[ranks[i]] = context->c_rank;
    }
  }

  free (ranks);

  rc = MPI_Allreduce (MPI_IN_PLACE, all_ranks, context->c_size, MPI_INT, MPI_MAX, context->c_comm);
  if (MPI_SUCCESS != rc) {
    free (all_ranks);
    return hioi_err_mpi (rc);
  }

  io_leader = all_ranks[context->c_rank];
  free (all_ranks);

  rc = MPI_Comm_split (context->c_comm, io_leader, 0, &io_comm);
  if (MPI_SUCCESS != rc) {
    return hioi_err_mpi (rc);
  }

  if (MPI_COMM_NULL == io_comm) {
    /* this rank has no data in the dataset */
    return HIO_SUCCESS;
  }

  rc = hioi_dataset_scatter_comm (dataset, context->c_comm, manifest, manifest_size, rc);
  MPI_Comm_free (&io_comm);

  return rc;
}

#endif /* HIO_MPI_HAVE(1) */

int hioi_dataset_open_internal (hio_module_t *module, hio_dataset_t dataset) {
  /* get timestamp before open call */
  uint64_t rotime = hioi_gettime ();
  int rc;

  hioi_log (module->context, HIO_VERBOSE_DEBUG_LOW, "Opening dataset %s::%" PRIu64 " with flags 0x%x "
            "with backend module %p", dataset->ds_object.identifier, dataset->ds_id, dataset->ds_flags,
            (void *) module);

  /* Several things need to be done here:
   * 1) check if the user is requesting a specific dataset or the newest available,
   * 2) check if the dataset specified already exists in any module,
   * 3) if the dataset does not exist and we are creating then use the current
   *    module to open (create) the dataset. */
  rc = module->dataset_open (module, dataset);
  if (HIO_SUCCESS != rc) {
    hioi_log (module->context, HIO_VERBOSE_DEBUG_LOW, "Failed to open dataset %s::%" PRIu64
              " on data root %s", dataset->ds_object.identifier, dataset->ds_id, module->data_root);
    return rc;
  }

  dataset->ds_rotime = rotime;

  return HIO_SUCCESS;
}

int hioi_dataset_close_internal (hio_dataset_t dataset) {
  hio_context_t context = hioi_object_context (&dataset->ds_object);
  hio_element_t element;
  int rc;

  /* close any open elements */
  hioi_list_foreach(element, dataset->ds_elist, struct hio_element, e_list) {
    if (element->e_open_count) {
      hioi_log (context, HIO_VERBOSE_WARN, "element %s still open at dataset close",
                hioi_object_identifier (&element->e_object));
      /* ensure the element is actually closed */
      element->e_open_count = 1;
      hioi_element_close_internal (element);
    }
  }

  rc = dataset->ds_close (dataset);

  return rc;
}
