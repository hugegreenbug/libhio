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
#include <stdio.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <bzlib.h>

#include <json.h>

#define HIO_MANIFEST_VERSION "2.1"
#define HIO_MANIFEST_COMPAT  "2.0"

#define HIO_MANIFEST_PROP_VERSION     "hio_manifest_version"
#define HIO_MANIFEST_PROP_COMPAT      "hio_manifest_compat"
#define HIO_MANIFEST_PROP_IDENTIFIER  "identifier"
#define HIO_MANIFEST_PROP_DATASET_ID  "dataset_id"
#define HIO_MANIFEST_PROP_SIZE        "size"
#define HIO_MANIFEST_PROP_HIO_VERSION "hio_version"
#define HIO_MANIFEST_PROP_RANK        "rank"

#define HIO_MANIFEST_KEY_DATASET_MODE "hio_dataset_mode"
#define HIO_MANIFEST_KEY_FILE_MODE    "hio_file_mode"
#define HIO_MANIFEST_KEY_BLOCK_SIZE   "block_size"
#define HIO_MANIFEST_KEY_MTIME        "hio_mtime"
#define HIO_MANIFEST_KEY_COMM_SIZE    "hio_comm_size"
#define HIO_MANIFEST_KEY_STATUS       "hio_status"
#define HIO_SEGMENT_KEY_FILE_OFFSET   "loff"
#define HIO_SEGMENT_KEY_APP_OFFSET0   "off"
#define HIO_SEGMENT_KEY_LENGTH        "len"
#define HIO_SEGMENT_KEY_FILE_INDEX    "findex"

/* manifest helper functions */
static void hioi_manifest_set_number (json_object *parent, const char *name, unsigned long value) {
  json_object *new_object = json_object_new_int64 ((int64_t) value);

  assert (NULL != new_object);
  json_object_object_add (parent, name, new_object);
}

static void hioi_manifest_set_signed_number (json_object *parent, const char *name, long value) {
  json_object *new_object = json_object_new_int64 (value);

  assert (NULL != new_object);
  json_object_object_add (parent, name, new_object);
}

static void hioi_manifest_set_string (json_object *parent, const char *name, const char *value) {
  json_object *new_object = json_object_new_string (value);

  assert (NULL != new_object);
  json_object_object_add (parent, name, new_object);
}

static json_object *hio_manifest_new_array (json_object *parent, const char *name) {
  json_object *new_object = json_object_new_array ();

  if (NULL == new_object) {
    return NULL;
  }

  json_object_object_add (parent, name, new_object);

  return new_object;
}

static json_object *hioi_manifest_find_object (json_object *parent, const char *name) {
  json_object *object;

  if (json_object_object_get_ex (parent, name, &object)) {
    return object;
  }

  return NULL;
}

static int hioi_manifest_get_string (json_object *parent, const char *name, const char **string) {
  json_object *object;

  object = hioi_manifest_find_object (parent, name);
  if (NULL == object) {
    return HIO_ERR_NOT_FOUND;
  }

  *string = json_object_get_string (object);
  if (!*string) {
    return HIO_ERROR;
  }

  return HIO_SUCCESS;
}

static int hioi_manifest_get_number (json_object *parent, const char *name, unsigned long *value) {
  json_object *object;

  object = hioi_manifest_find_object (parent, name);
  if (NULL == object) {
    return HIO_ERR_NOT_FOUND;
  }

  *value = (unsigned long) json_object_get_int64 (object);

  return HIO_SUCCESS;
}

static int hioi_manifest_get_signed_number (json_object *parent, const char *name, long *value) {
  json_object *object;

  object = hioi_manifest_find_object (parent, name);
  if (NULL == object) {
    return HIO_ERR_NOT_FOUND;
  }

  *value = (long) json_object_get_int64 (object);

  return HIO_SUCCESS;
}

/**
 * @brief Generate a json manifest from an hio dataset
 *
 * @param[in] dataset hio dataset handle
 *
 * @returns json object representing the dataset's manifest
 */
static json_object *hio_manifest_generate_2_0 (hio_dataset_t dataset) {
  json_object *elements, *top;
  hio_context_t context = hioi_object_context (&dataset->ds_object);
  hio_object_t hio_object = &dataset->ds_object;
  hio_element_t element;
  char *string_tmp;
  int rc;

  top = json_object_new_object ();
  if (NULL == top) {
    return NULL;
  }

  hioi_manifest_set_string (top, HIO_MANIFEST_PROP_VERSION, HIO_MANIFEST_VERSION);
  hioi_manifest_set_string (top, HIO_MANIFEST_PROP_COMPAT, HIO_MANIFEST_COMPAT);
  hioi_manifest_set_string (top, HIO_MANIFEST_PROP_HIO_VERSION, PACKAGE_VERSION);
  hioi_manifest_set_string (top, HIO_MANIFEST_PROP_IDENTIFIER, hio_object->identifier);
  hioi_manifest_set_number (top, HIO_MANIFEST_PROP_DATASET_ID, (unsigned long) dataset->ds_id);

  if (HIO_SET_ELEMENT_UNIQUE == dataset->ds_mode) {
    hioi_manifest_set_string (top, HIO_MANIFEST_KEY_DATASET_MODE, "unique");
  } else {
    hioi_manifest_set_string (top, HIO_MANIFEST_KEY_DATASET_MODE, "shared");
  }

  rc = hio_config_get_value (&dataset->ds_object, "dataset_file_mode", &string_tmp);
  assert (HIO_SUCCESS == rc);

  hioi_manifest_set_string (top, HIO_MANIFEST_KEY_FILE_MODE, string_tmp);
  free (string_tmp);

  hioi_manifest_set_number (top, HIO_MANIFEST_KEY_COMM_SIZE, (unsigned long) context->c_size);
  hioi_manifest_set_signed_number (top, HIO_MANIFEST_KEY_STATUS, (long) dataset->ds_status);
  hioi_manifest_set_number (top, HIO_MANIFEST_KEY_MTIME, (unsigned long) time (NULL));

  if (HIO_FILE_MODE_BASIC == dataset->ds_fmode) {
    /* NTH: for now do not write elements for basic mode. this may change in future versions */
    return top;
  }

  elements = hio_manifest_new_array (top, "elements");
  if (NULL == elements) {
    json_object_put (top);
    return NULL;
  }

  hioi_list_foreach(element, dataset->ds_elist, struct hio_element, e_list) {
    json_object *element_object = json_object_new_object ();
    if (NULL == element_object) {
      json_object_put (top);
      return NULL;
    }

    hioi_manifest_set_string (element_object, HIO_MANIFEST_PROP_IDENTIFIER, element->e_object.identifier);
    hioi_manifest_set_number (element_object, HIO_MANIFEST_PROP_SIZE, (unsigned long) element->e_size);
    if (HIO_SET_ELEMENT_UNIQUE == dataset->ds_mode) {
      hioi_manifest_set_number (element_object, HIO_MANIFEST_PROP_RANK, (unsigned long) element->e_rank);
    }

    json_object_array_add (elements, element_object);

    if (element->e_scount) {
      json_object *segments_object = hio_manifest_new_array (element_object, "segments");
      if (NULL == segments_object) {
        json_object_put (top);
        return NULL;
      }

      for (int i = 0 ; i < element->e_scount ; ++i) {
        json_object *segment_object = json_object_new_object ();
        hio_manifest_segment_t *segment = element->e_sarray + i;
        if (NULL == segment_object) {
          json_object_put (top);
          return NULL;
        }

        hioi_manifest_set_number (segment_object, HIO_SEGMENT_KEY_FILE_OFFSET,
                                  (unsigned long) segment->seg_foffset);
        hioi_manifest_set_number (segment_object, HIO_SEGMENT_KEY_APP_OFFSET0,
                                  (unsigned long) segment->seg_offset);
        hioi_manifest_set_number (segment_object, HIO_SEGMENT_KEY_LENGTH,
                                  (unsigned long) segment->seg_length);
        hioi_manifest_set_number (segment_object, HIO_SEGMENT_KEY_FILE_INDEX,
                                  (unsigned long) segment->seg_file_index);
        json_object_array_add (segments_object, segment_object);
      }
    }
  }

  if (dataset->ds_file_count) {
    json_object *files_object = hio_manifest_new_array (top, "files");
    if (NULL == files_object) {
      json_object_put (top);
      return NULL;
    }

    for (int i = 0 ; i < dataset->ds_file_count ; ++i) {
      hio_manifest_file_t *file = dataset->ds_flist + i;
      json_object *file_object = json_object_new_string (file->f_name);
      if (NULL == file_object) {
        json_object_put (top);
        return NULL;
      }

      json_object_array_add (files_object, file_object);
    }
  }

  return top;
}

/**
 * @brief Serialize a json object
 *
 * @param[in] json_object json object pointer
 * @param[out] data serialized data out
 * @param[out] data_size length of serialized data
 * @param[in] compress_data whether to compress the serialized data
 *
 * @returns HIO_SUCCESS on success
 * @returns HIO_ERR_OUT_OF_RESOURCE if run out of memory
 */
static int hioi_manifest_serialize_json (json_object *json_object, unsigned char **data, size_t *data_size,
                                         bool compress_data) {
  const char *serialized;
  unsigned int serialized_len;
  int rc;

  serialized = json_object_to_json_string (json_object);
  serialized_len = strlen (serialized) + 1;
  if (compress_data) {
    unsigned int compressed_size = serialized_len;
    char *tmp;

    tmp = malloc (serialized_len);
    if (NULL == tmp) {
      return HIO_ERR_OUT_OF_RESOURCE;
    }

    rc = BZ2_bzBuffToBuffCompress (tmp, &compressed_size, (char *) serialized, serialized_len, 3, 0, 0);
    if (BZ_OK != rc) {
      free (tmp);
      return HIO_ERROR;
    }

    *data = realloc (tmp, compressed_size);
    if (NULL == *data) {
      free (tmp);
      return HIO_ERR_OUT_OF_RESOURCE;
    }

    *data_size = compressed_size;
  } else {
    *data_size = serialized_len;

    *data = calloc (*data_size, 1);
    if (NULL == *data) {
      return HIO_ERR_OUT_OF_RESOURCE;
    }

    memcpy (*data, serialized, *data_size - 1);
  }

  return HIO_SUCCESS;
}

int hioi_manifest_serialize (hio_dataset_t dataset, unsigned char **data, size_t *data_size, bool compress_data) {
  json_object *json_object;
  int rc;

  json_object = hio_manifest_generate_2_0 (dataset);
  if (NULL == json_object) {
    return HIO_ERROR;
  }

  rc = hioi_manifest_serialize_json (json_object, data, data_size, compress_data);
  json_object_put (json_object);

  return rc;
}

int hioi_manifest_save (hio_dataset_t dataset, const char *path) {
  const char *extension = strrchr (path, '.') + 1;
  unsigned char *data;
  size_t data_size;
  int rc;

  if (0 == strcmp (extension, "bz2")) {
    rc = hioi_manifest_serialize (dataset, &data, &data_size, true);
  } else {
    rc = hioi_manifest_serialize (dataset, &data, &data_size, false);
  }

  if (HIO_SUCCESS != rc) {
    return rc;
  }

  int fd = open (path, O_WRONLY | O_CREAT, 0644);
  if (0 > fd) {
    return hioi_err_errno (errno);
  }

  errno = 0;
  rc = write (fd, data, data_size);
  close (fd);
  free (data);

  if (0 > rc) {
    return hioi_err_errno (errno);
  }

  return rc == data_size ? HIO_SUCCESS : HIO_ERR_TRUNCATE;
}

static int hioi_manifest_parse_file_2_1 (hio_dataset_t dataset, json_object *file_object) {
  const char *tmp_string;

  tmp_string = json_object_get_string (file_object);
  if (NULL == tmp_string) {
    hioi_err_push (HIO_ERROR, &dataset->ds_object, "Error parsing manifest file");
    return HIO_ERROR;
  }

  return hioi_dataset_add_file (dataset, tmp_string);
}


static int hioi_manifest_parse_segment_2_1 (hio_element_t element, json_object *files, json_object *segment_object) {
  unsigned long file_offset, app_offset0, length, file_index;
  int rc;

  rc = hioi_manifest_get_number (segment_object, HIO_SEGMENT_KEY_FILE_OFFSET, &file_offset);
  if (HIO_SUCCESS != rc) {
    return rc;
  }

  rc = hioi_manifest_get_number (segment_object, HIO_SEGMENT_KEY_APP_OFFSET0, &app_offset0);
  if (HIO_SUCCESS != rc) {
    return rc;
  }

  rc = hioi_manifest_get_number (segment_object, HIO_SEGMENT_KEY_LENGTH, &length);
  if (HIO_SUCCESS != rc) {
    return rc;
  }

  rc = hioi_manifest_get_number (segment_object, HIO_SEGMENT_KEY_FILE_INDEX, &file_index);
  if (HIO_SUCCESS != rc) {
    hioi_err_push (HIO_ERROR, &element->e_object, "Manfest segment missing file_index property");
    return rc;
  }

  if (files) {
    /* verify the file index is valid */
    if (file_index >= json_object_array_length (files)) {
      hioi_err_push (HIO_ERROR, &element->e_object, "Manifest segment specified invalid file index");
      return HIO_ERROR;
    }
  }

  return hioi_element_add_segment (element, file_index, file_offset, app_offset0, length);
}

static int hioi_manifest_parse_segments_2_1 (hio_element_t element, json_object *files, json_object *object) {
  hio_context_t context = hioi_object_context (&element->e_object);
  int segment_count = json_object_array_length (object);

  hioi_log (context, HIO_VERBOSE_DEBUG_MED, "parsing %d segments in element %s", segment_count,
            hioi_object_identifier (&element->e_object));

  for (int i = 0 ; i < segment_count ; ++i) {
    json_object *segment_object = json_object_array_get_idx (object, i);
    int rc = hioi_manifest_parse_segment_2_1 (element, files, segment_object);
    if (HIO_SUCCESS != rc) {
      return rc;
    }
  }

  return HIO_SUCCESS;
}

static int hioi_manifest_parse_element_2_0 (hio_dataset_t dataset, json_object *files, json_object *element_object) {
  hio_context_t context = hioi_object_context (&dataset->ds_object);
  hio_element_t element = NULL;
  json_object *segments_object;
  const char *tmp_string;
  unsigned long value;
  int rc, rank;

  rc = hioi_manifest_get_string (element_object, HIO_MANIFEST_PROP_IDENTIFIER, &tmp_string);
  if (HIO_SUCCESS != rc) {
    hioi_err_push (HIO_ERROR, &dataset->ds_object, "manifest element missing identifier property");
    return HIO_ERROR;
  }

  hioi_log (context, HIO_VERBOSE_DEBUG_LOW, "parsing manifest element: %s", tmp_string);

  if (HIO_SET_ELEMENT_UNIQUE == dataset->ds_mode) {
    rc = hioi_manifest_get_number (element_object, HIO_MANIFEST_PROP_RANK, &value);
    if (HIO_SUCCESS != rc) {
      hioi_object_release (&element->e_object);
      return HIO_ERR_BAD_PARAM;
    }

    if (value != context->c_rank) {
      /* nothing to do */
      return HIO_SUCCESS;
    }

    rank = value;
  } else {
    rank = -1;
  }

  element = hioi_element_alloc (dataset, (const char *) tmp_string, rank);
  if (NULL == element) {
    return HIO_ERR_OUT_OF_RESOURCE;
  }

  rc = hioi_manifest_get_number (element_object, HIO_MANIFEST_PROP_SIZE, &value);
  if (HIO_SUCCESS != rc) {
    hioi_object_release (&element->e_object);
    return HIO_ERR_BAD_PARAM;
  }

  if (dataset->ds_mode == HIO_SET_ELEMENT_UNIQUE || value > element->e_size) {
    element->e_size = value;
  }

  segments_object = hioi_manifest_find_object (element_object, "segments");
  if (NULL != segments_object) {
    rc = hioi_manifest_parse_segments_2_1 (element, files, segments_object);
    if (HIO_SUCCESS != rc) {
      hioi_object_release (&element->e_object);
      return rc;
    }
  }

  hioi_dataset_add_element (dataset, element);

  hioi_log (context, HIO_VERBOSE_DEBUG_LOW, "found element with identifier %s in manifest",
	    element->e_object.identifier);

  return HIO_SUCCESS;
}

static int hioi_manifest_parse_elements_2_0 (hio_dataset_t dataset, json_object *files, json_object *object) {
  hio_context_t context = hioi_object_context (&dataset->ds_object);
  int element_count = json_object_array_length (object);

  hioi_log (context, HIO_VERBOSE_DEBUG_MED, "parsing %d elements in manifest", element_count);
  for (int i = 0 ; i < element_count ; ++i) {
    json_object *element_object = json_object_array_get_idx (object, i);
    int rc = hioi_manifest_parse_element_2_0 (dataset, files, element_object);
    if (HIO_SUCCESS != rc) {
      return rc;
    }
  }

  return HIO_SUCCESS;
}

static int hioi_manifest_parse_files_2_1 (hio_dataset_t dataset, json_object *object) {
  hio_context_t context = hioi_object_context (&dataset->ds_object);
  int file_count = json_object_array_length (object);

  hioi_log (context, HIO_VERBOSE_DEBUG_MED, "parsing %d file entries in manifest", file_count);
  for (int i = 0 ; i < file_count ; ++i) {
    json_object *file_object = json_object_array_get_idx (object, i);
    int rc = hioi_manifest_parse_file_2_1 (dataset, file_object);
    if (0 > rc) {
      return rc;
    }
  }

  return HIO_SUCCESS;
}

static int hioi_manifest_parse_2_0 (hio_dataset_t dataset, json_object *object) {
  hio_context_t context = hioi_object_context (&dataset->ds_object);
  json_object *elements_object, *files_object;
  unsigned long mode = 0, size;
  const char *tmp_string;
  long status;
  int rc;

  /* check for compatibility with this manifest version */
  rc = hioi_manifest_get_string (object, HIO_MANIFEST_PROP_COMPAT, &tmp_string);
  if (HIO_SUCCESS != rc) {
    hioi_err_push (rc, &dataset->ds_object, "manifest missing required %s key",
                   HIO_MANIFEST_PROP_COMPAT);
    return rc;
  }

  hioi_log (context, HIO_VERBOSE_DEBUG_LOW, "compatibility version of manifest: %s",
            (char *) tmp_string);

  if (strcmp ((char *) tmp_string, "2.0")) {
    /* incompatible version */
    return HIO_ERROR;
  }

  rc = hioi_manifest_get_string (object, HIO_MANIFEST_KEY_DATASET_MODE, &tmp_string);
  if (HIO_SUCCESS != rc) {
    hioi_err_push (rc, &dataset->ds_object, "manifest missing required %s key",
                   HIO_MANIFEST_KEY_DATASET_MODE);
    return rc;
  }

  if (0 == strcmp (tmp_string, "unique")) {
    mode = HIO_SET_ELEMENT_UNIQUE;
  } else if (0 == strcmp (tmp_string, "shared")) {
    mode = HIO_SET_ELEMENT_SHARED;
  } else {
    hioi_err_push (HIO_ERR_BAD_PARAM, &dataset->ds_object,
                   "unknown dataset mode specified in manifest: %s", (const char *) tmp_string);
    return HIO_ERR_BAD_PARAM;
  }

  if (mode != dataset->ds_mode) {
    hioi_err_push (HIO_ERR_BAD_PARAM, &dataset->ds_object,
                   "mismatch in dataset mode. requested: %d, actual: %d", mode,
                   dataset->ds_mode);
    return HIO_ERR_BAD_PARAM;
  }

  if (HIO_SET_ELEMENT_UNIQUE == mode) {
    /* verify that the same number of ranks are in use */
    rc = hioi_manifest_get_number (object, HIO_MANIFEST_KEY_COMM_SIZE, &size);
    if (HIO_SUCCESS != rc) {
      hioi_err_push (HIO_ERR_BAD_PARAM, &dataset->ds_object, "manifest missing required %s key",
                     HIO_MANIFEST_KEY_COMM_SIZE);
      return HIO_ERR_BAD_PARAM;
    }

    if (size != context->c_size) {
      hioi_err_push (HIO_ERR_BAD_PARAM, &dataset->ds_object, "communicator size does not match dataset",
                     HIO_MANIFEST_KEY_COMM_SIZE);
      return HIO_ERR_BAD_PARAM;
    }
  }

  rc = hioi_manifest_get_string (object, HIO_MANIFEST_KEY_FILE_MODE, &tmp_string);
  if (HIO_SUCCESS != rc) {
    hioi_err_push (HIO_ERR_BAD_PARAM, &dataset->ds_object, "file mode was not specified in manifest");
    return HIO_ERR_BAD_PARAM;
  }

  rc = hio_config_set_value (&dataset->ds_object, "dataset_file_mode", (const char *) tmp_string);
  if (HIO_SUCCESS != rc) {
    hioi_err_push (HIO_ERR_BAD_PARAM, &dataset->ds_object, "bad file mode: %s", tmp_string);
    return HIO_ERR_BAD_PARAM;
  }

  rc = hioi_manifest_get_signed_number (object, HIO_MANIFEST_KEY_STATUS, &status);
  if (HIO_SUCCESS != rc) {
    return HIO_ERR_BAD_PARAM;
  }

  dataset->ds_status = status;

  files_object = hioi_manifest_find_object (object, "files");
  if (files_object) {
    rc = hioi_manifest_parse_files_2_1 (dataset, files_object);
    if (HIO_SUCCESS != rc) {
      return rc;
    }

    files_object = NULL;
  }

  /* find and parse all elements covered by this manifest */
  elements_object = hioi_manifest_find_object (object, "elements");
  if (NULL == elements_object) {
    /* no elements in this file. odd but still valid */
    return HIO_SUCCESS;
  }

  return hioi_manifest_parse_elements_2_0 (dataset, files_object, elements_object);
}

static int hioi_manifest_parse_header_2_0 (hio_context_t context, hio_dataset_header_t *header, json_object *object) {
  const char *tmp_string;
  unsigned long value;
  long svalue;
  int rc;

  /* check for compatibility with this manifest version */
  rc = hioi_manifest_get_string (object, HIO_MANIFEST_PROP_COMPAT, &tmp_string);
  if (HIO_SUCCESS != rc) {
    return rc;
  }

  hioi_log (context, HIO_VERBOSE_DEBUG_LOW, "compatibility version of manifest: %s", (char *) tmp_string);

  if (strcmp ((char *) tmp_string, "2.0")) {
    /* incompatible version */
    return HIO_ERROR;
  }

  /* fill in header */
  rc = hioi_manifest_get_string (object, HIO_MANIFEST_KEY_DATASET_MODE, &tmp_string);
  if (HIO_SUCCESS != rc) {
    return rc;
  }

  if (0 == strcmp (tmp_string, "unique")) {
    value = HIO_SET_ELEMENT_UNIQUE;
  } else if (0 == strcmp (tmp_string, "shared")) {
    value = HIO_SET_ELEMENT_SHARED;
  } else {
    hioi_err_push (HIO_ERR_BAD_PARAM, &context->c_object, "unknown dataset mode specified in manifest: "
                  "%s", tmp_string);
    return HIO_ERR_BAD_PARAM;
  }

  if (HIO_SUCCESS != rc) {
    return HIO_ERR_BAD_PARAM;
  }

  header->ds_mode = value;

  rc = hioi_manifest_get_string (object, HIO_MANIFEST_KEY_FILE_MODE, &tmp_string);
  if (HIO_SUCCESS != rc) {
    hioi_err_push (HIO_ERR_BAD_PARAM, &context->c_object, "file mode was not specified in manifest");
    return HIO_ERR_BAD_PARAM;
  }

  if (0 == strcmp ((const char *) tmp_string, "basic")) {
    value = HIO_FILE_MODE_BASIC;
  } else if (0 == strcmp ((const char *) tmp_string, "optimized")) {
    value = HIO_FILE_MODE_OPTIMIZED;
  } else {
    hioi_err_push (HIO_ERR_BAD_PARAM, &context->c_object, "unrecognized file mode in manifest: %s",
                  tmp_string);
    return HIO_ERR_BAD_PARAM;
  }

  header->ds_fmode = value;

  rc = hioi_manifest_get_signed_number (object, HIO_MANIFEST_KEY_STATUS, &svalue);

  if (HIO_SUCCESS != rc) {
    return HIO_ERR_BAD_PARAM;
  }

  header->ds_status = svalue;

  rc = hioi_manifest_get_number (object, HIO_MANIFEST_KEY_MTIME, &value);

  if (HIO_SUCCESS != rc) {
    return HIO_ERR_BAD_PARAM;
  }

  header->ds_mtime = value;

  rc = hioi_manifest_get_number (object, HIO_MANIFEST_PROP_DATASET_ID, &value);

  if (HIO_SUCCESS != rc) {
    return HIO_ERR_BAD_PARAM;
  }

  header->ds_id = value;

  return HIO_SUCCESS;
}

static int hioi_manifest_decompress (unsigned char **data, size_t data_size) {
  const size_t increment = 8192;
  char *uncompressed, *tmp;
  bz_stream strm;
  size_t size;
  int rc;

  uncompressed = malloc (increment);
  if (NULL == uncompressed) {
    return HIO_ERR_OUT_OF_RESOURCE;
  }

  strm.bzalloc = NULL;
  strm.bzfree = NULL;
  strm.opaque = NULL;
  strm.next_in = (char *) *data;
  strm.avail_in = data_size;
  strm.next_out = uncompressed;
  strm.avail_out = size = increment;

  BZ2_bzDecompressInit (&strm, 0, 0);

  do {
    rc = BZ2_bzDecompress (&strm);
    if (BZ_OK != rc) {
      BZ2_bzDecompressEnd (&strm);
      if (BZ_STREAM_END == rc) {
        break;
      }
      free (uncompressed);
      return HIO_ERROR;
    }

    tmp = realloc (uncompressed, size + increment);
    if (NULL == tmp) {
      BZ2_bzDecompressEnd (&strm);
      free (uncompressed);
      return HIO_ERR_OUT_OF_RESOURCE;
    }

    uncompressed = tmp;

    strm.next_out = uncompressed + size;
    strm.avail_out = increment;
    size += increment;
  } while (1);

  *data = (unsigned char *) uncompressed;
  return HIO_SUCCESS;
}

int hioi_manifest_deserialize (hio_dataset_t dataset, const unsigned char *data, size_t data_size) {
  bool free_data = false;
  json_object *object;
  int rc;

  if (data_size < 2) {
    return HIO_ERR_BAD_PARAM;
  }

  if ('B' == data[0] && 'Z' == data[1]) {
    /* gz compressed */
    rc = hioi_manifest_decompress ((unsigned char **) &data, data_size);
    if (HIO_SUCCESS != rc) {
      return rc;
    }

    free_data = true;
  }

  object = json_tokener_parse ((char *) data);
  if (NULL == object) {
    if (free_data) {
      free ((char *) data);
    }
    return HIO_ERROR;
  }

  rc = hioi_manifest_parse_2_0 (dataset, object);
  if (free_data) {
    free ((char *) data);
  }

  return rc;
}

int hioi_manifest_read (const char *path, unsigned char **manifest_out, size_t *manifest_size_out)
{
  unsigned char *buffer = NULL;
  size_t file_size;
  FILE *fh;
  int rc;

  if (access (path, F_OK)) {
    return HIO_ERR_NOT_FOUND;
  }

  if (access (path, R_OK)) {
    return HIO_ERR_PERM;
  }

  fh = fopen (path, "r");
  if (NULL == fh) {
    return hioi_err_errno (errno);
  }

  (void) fseek (fh, 0, SEEK_END);
  file_size = ftell (fh);
  (void) fseek (fh, 0, SEEK_SET);

  if (0 == file_size) {
    fclose (fh);
    return HIO_ERR_BAD_PARAM;
  }

  buffer = malloc (file_size);
  if (NULL == buffer) {
    fclose (fh);
    return HIO_ERR_OUT_OF_RESOURCE;
  }

  rc = fread (buffer, 1, file_size, fh);
  fclose (fh);
  if (file_size != rc) {
    free (buffer);
    return HIO_ERROR;
  }

  *manifest_out = buffer;
  *manifest_size_out = file_size;

  return HIO_SUCCESS;
}

int hioi_manifest_load (hio_dataset_t dataset, const char *path) {
  hio_context_t context = hioi_object_context (&dataset->ds_object);
  unsigned char *manifest;
  size_t manifest_size;
  int rc;

  hioi_log (context, HIO_VERBOSE_DEBUG_LOW, "Loading dataset manifest for %s:%lu from %s",
	    dataset->ds_object.identifier, dataset->ds_id, path);

  rc = hioi_manifest_read (path, &manifest, &manifest_size);
  if (HIO_SUCCESS != rc) {
    return rc;
  }

  rc = hioi_manifest_deserialize (dataset, manifest, manifest_size);
  free (manifest);

  return rc;
}


static bool hioi_manifest_compare_json (json_object *object1, json_object *object2, const char *key) {
  const char *value1, *value2;
  int rc;

  rc = hioi_manifest_get_string (object1, key, &value1);
  if (HIO_SUCCESS != rc) {
    return false;
  }

  rc = hioi_manifest_get_string (object2, key, &value2);
  if (HIO_SUCCESS != rc) {
    return false;
  }

  return 0 == strcmp (value1, value2);
}

static int hioi_manifest_array_find_matching (json_object *array, json_object *object, const char *key) {
  int array_size = json_object_array_length (array);
  const char *value = NULL;

  if (NULL != key) {
    (void) hioi_manifest_get_string (object, key, &value);
  } else {
    value = json_object_get_string (object);
  }

  for (int i = 0 ; i < array_size ; ++i) {
    json_object *array_object = json_object_array_get_idx (array, i);
    const char *value1 = NULL;

    assert (array_object);
    if (NULL != key) {
      (void) hioi_manifest_get_string (array_object, key, &value1);
    } else {
      value1 = json_object_get_string (array_object);
    }

    if (0 == strcmp (value, value1)) {
      return i;
    }
  }

  return HIO_ERR_NOT_FOUND;
}

static int segment_compare (const void *arg1, const void *arg2) {
  json_object * const *segment1 = (json_object * const *) arg1;
  json_object * const *segment2 = (json_object * const *) arg2;
  unsigned long base1 = 0, base2 = 0;

  (void) hioi_manifest_get_number (*segment1, HIO_SEGMENT_KEY_APP_OFFSET0, &base1);
  (void) hioi_manifest_get_number (*segment2, HIO_SEGMENT_KEY_APP_OFFSET0, &base2);

  if (base1 > base2) {
    return 1;
  }

  return (base1 < base2) ? -1 : 0;
}

static int hioi_manifest_merge_internal (json_object *object1, json_object *object2) {
  json_object *elements1, *elements2, *files1, *files2;
  int *file_index_reloc = NULL, rc, manifest_mode;
  const char *tmp_string;

  /* sanity check. make sure the manifest meta-data matches */
  if (!hioi_manifest_compare_json (object1, object2, HIO_MANIFEST_KEY_DATASET_MODE) ||
      !hioi_manifest_compare_json (object1, object2, HIO_MANIFEST_PROP_HIO_VERSION) ||
      !hioi_manifest_compare_json (object1, object2, HIO_MANIFEST_PROP_DATASET_ID)) {
    return HIO_ERR_BAD_PARAM;
  }

  rc = hioi_manifest_get_string (object1, HIO_MANIFEST_KEY_DATASET_MODE, &tmp_string);
  if (HIO_SUCCESS != rc) {
    return HIO_ERR_BAD_PARAM;
  }

  if (0 == strcmp (tmp_string, "unique")) {
    manifest_mode = HIO_SET_ELEMENT_UNIQUE;
  } else if (0 == strcmp (tmp_string, "shared")) {
    manifest_mode = HIO_SET_ELEMENT_SHARED;
  } else {
    return HIO_ERR_BAD_PARAM;
  }

  elements1 = hioi_manifest_find_object (object1, "elements");
  elements2 = hioi_manifest_find_object (object2, "elements");
  files1 = hioi_manifest_find_object (object1, "files");
  files2 = hioi_manifest_find_object (object2, "files");

  /* NTH: if the second manifest has a file list but the current manifest does not
   * we need to create the array in the current manifest. this can happen if not all
   * ranks in a manifest region write to the dataset. */
  if (NULL == files1 && NULL != files2) {
    /* move the array from object2 to object. the reference count needs to be
     * incremented before it is deleted to ensure it is not freed prematurely */
    json_object_get (files2);
    json_object_object_del (object2, "files");
    json_object_object_add (object1, "files", files2);
    files1 = files2;
    files2 = NULL;
  }

  /* merge the file list if necessary. keeping track of any updated file index */
  if (NULL != files2) {
    int files1_count = json_object_array_length (files1);
    int files2_count = json_object_array_length (files2);

    /* allocate the index translation table */
    file_index_reloc = calloc (files2_count, sizeof (int));
    if (NULL == file_index_reloc) {
      return HIO_ERR_OUT_OF_RESOURCE;
    }

    for (int i = 0, new_index = files1_count ; i < files2_count ; ++i) {
      json_object *file2;

      file2 = json_object_array_get_idx (files2, i);
      assert (file2);

      /* check to see if this file already exists in the array */
      rc = hioi_manifest_array_find_matching (files1, file2, NULL);
      if (0 > rc) {
        /* increment the reference count. it will be decremented when the array is deleted from object2 */
        json_object_get (file2);
        json_object_array_add (files1, file2);
        file_index_reloc[i] = new_index++;
      } else {
        file_index_reloc[i] = rc;
      }
    }

    json_object_object_del (object2, "files");
  }

  if (NULL == elements1 && NULL != elements2) {
    /* move the array from object2 to object. the reference count needs to be
     * incremented before it is deleted to ensure it is not freed prematurely and
     * the reference count remains correct after it is added to object1. */
    json_object_get (elements2);
    json_object_object_del (object2, "elements");
    json_object_object_add (object1, "elements", elements2);
    elements1 = elements2;
    elements2 = NULL;
  }

  /* check if elements need to be merged */
  if (NULL != elements2) {
    int elements2_count = json_object_array_length (elements2);

    for (int i = 0 ; i < elements2_count ; ++i) {
      json_object *segments, *element = json_object_array_get_idx (elements2, i);
      int segment_count = 0;

      assert (NULL != element);

      segments = hioi_manifest_find_object (element, "segments");
      if (NULL != segments) {
        segment_count = json_object_array_length (segments);

        /* remove the segments from the element in the object2. get a reference first
         * so the array doesn't get freed before we are done with it */
        json_object_get (segments);
        json_object_object_del (element, "segments");

        if (file_index_reloc) {
          /* need to update the file indicies */
          for (int j = 0 ; j < segment_count ; ++j) {
            json_object *segment = json_object_array_get_idx (segments, j);
            unsigned long file_index = (unsigned long) -1;
            assert (NULL != segment);

            (void) hioi_manifest_get_number (segment, HIO_SEGMENT_KEY_FILE_INDEX, &file_index);
            if (file_index != file_index_reloc[file_index]) {
              json_object_object_del (segment, HIO_SEGMENT_KEY_FILE_INDEX);
              hioi_manifest_set_number (segment, HIO_SEGMENT_KEY_FILE_INDEX, file_index_reloc[file_index]);
            }
          }
        }
      }

      if (HIO_SET_ELEMENT_UNIQUE != manifest_mode) {
        /* check if this element already exists */
        rc = hioi_manifest_array_find_matching (elements1, element, HIO_MANIFEST_PROP_IDENTIFIER);
      } else {
        /* assuming the manifests are from different ranks for now.. I may changet this
         * in the future. */
        rc = HIO_ERR_NOT_FOUND;
      }

      if (0 <= rc) {
        json_object *element1 = json_object_array_get_idx (elements1, rc);
        json_object *segments1 = hioi_manifest_find_object (element1, "segments");
        unsigned long element_size = 0, element1_size = 0;

        if (NULL != segments1) {
          for (int j = 0 ; j < segment_count ; ++j) {
            json_object *segment = json_object_array_get_idx (segments, j);
            json_object_get (segment);
            json_object_array_add (segments1, segment);
          }

          json_object_put (segments);
          /* re-sort the segment array by base pointer */
          json_object_array_sort (segments1, segment_compare);
        } else {
          json_object_object_add (element1, "segments", segments);
        }

        (void) hioi_manifest_get_number (element, HIO_MANIFEST_PROP_SIZE, &element_size);
        (void) hioi_manifest_get_number (element1, HIO_MANIFEST_PROP_SIZE, &element1_size);
        if (element_size > element1_size) {
          /* use the larger of the two sizes */
          json_object_object_del (element1, HIO_MANIFEST_PROP_SIZE);
          hioi_manifest_set_number (element1, HIO_MANIFEST_PROP_SIZE, element_size);
        }
      } else {
        json_object_get (element);
        json_object_array_add (elements1, element);
      }

    }

    /* remove the elements array from object2 */
    json_object_object_del (object2, "elements");
  }

  return HIO_SUCCESS;
}

int hioi_manifest_merge_data2 (unsigned char **data1, size_t *data1_size, const unsigned char *data2, size_t data2_size) {
  bool free_data2 = false, compressed = false;
  json_object *object1, *object2;
  unsigned char *data1_save;
  int rc;

  if (NULL == data1[0]) {
    if (NULL != data2 && data2_size) {
      data1[0] = malloc (data2_size);
      if (NULL == data1[0]) {
        return HIO_ERR_OUT_OF_RESOURCE;
      }

      memcpy (data1[0], data2, data2_size);
    } else {
      data1[0] = NULL;
    }

    *data1_size = data2_size;
    return HIO_SUCCESS;
  }

  /* decompress the data if necessary */
  if ('B' == data1[0][0] && 'Z' == data1[0][1]) {
    data1_save = data1[0];
    /* bz2 compressed */
    rc = hioi_manifest_decompress (data1, *data1_size);
    if (HIO_SUCCESS != rc) {
      return rc;
    }

    compressed = true;
    free (data1_save);
  }

  object1 = json_tokener_parse ((char *) data1[0]);
  if (NULL == object1) {
    return HIO_ERROR;
  }

  /* decompress the data if necessary */
  if ('B' == data2[0] && 'Z' == data2[1]) {
    /* bz2 compressed */
    rc = hioi_manifest_decompress ((unsigned char **) &data2, data2_size);
    if (HIO_SUCCESS != rc) {
      return rc;
    }

    /* data2 was malloc'd by decompress so it needs to be freed */
    free_data2 = true;
  }

  object2 = json_tokener_parse ((char *) data2);
  if (NULL == object2) {
    return HIO_ERROR;
  }

  rc = hioi_manifest_merge_internal (object1, object2);
  if (free_data2) {
    free ((void *) data2);
  }
  if (HIO_SUCCESS != rc) {
    return rc;
  }

  data1_save = data1[0];
  rc = hioi_manifest_serialize_json (object1, data1, data1_size, compressed);
  free (data1_save);
  if (HIO_SUCCESS != rc) {
    return rc;
  }

  return rc;
}

int hioi_manifest_read_header (hio_context_t context, hio_dataset_header_t *header, const char *path) {
  unsigned char *manifest;
  size_t manifest_size;
  json_object *object;
  int rc;

  hioi_log (context, HIO_VERBOSE_DEBUG_LOW, "loading json dataset manifest header from %s", path);

  if (access (path, F_OK)) {
    return HIO_ERR_NOT_FOUND;
  }

  if (access (path, R_OK)) {
    return HIO_ERR_PERM;
  }

  rc = hioi_manifest_read (path, &manifest, &manifest_size);
  if (HIO_SUCCESS != rc) {
    return rc;
  }

  if ('B' == manifest[0] && 'Z' == manifest[1]) {
    unsigned char *data = manifest;
    /* gz compressed */
    rc = hioi_manifest_decompress ((unsigned char **) &data, manifest_size);
    if (HIO_SUCCESS != rc) {
      return rc;
    }
    free (manifest);
    manifest = data;
  }

  object = json_tokener_parse ((char *) manifest);
  if (NULL == object) {
    free (manifest);
    return HIO_ERROR;
  }

  rc = hioi_manifest_parse_header_2_0 (context, header, object);
  json_object_put (object);
  free (manifest);

  return rc;
}
