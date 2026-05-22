#ifndef FASTKV_INDEX_JSON_INDEX_H
#define FASTKV_INDEX_JSON_INDEX_H

#include "fastkv/error.h"
#include "fastkv/types.h"

struct fastkv_db;

/*
 * Buat secondary index berbasis field JSON.
 *
 * Setiap kali ada put/delete, nilai JSON di-parse dan field yang ditunjuk
 * oleh json_field digunakan sebagai index key.
 *
 * Contoh: nilai {"nama": "ali", "usia": 30}
 *   fastkv_json_index_create(db, "idx_nama", "nama", &idx)
 *   → index key = "ali"
 *
 * Hanya mendukung objek JSON flat (satu level), field string atau angka.
 */
fastkv_err_t fastkv_json_index_create(
    struct fastkv_db *db, const char *index_name, const char *json_field, fastkv_index_t **out);

#endif /* FASTKV_INDEX_JSON_INDEX_H */
