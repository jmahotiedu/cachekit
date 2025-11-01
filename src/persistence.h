#ifndef CK_PERSISTENCE_H
#define CK_PERSISTENCE_H

#include "store.h"

#define CK_RDB_MAGIC    "CACHEKIT"
#define CK_RDB_VERSION  1
#define CK_RDB_DEFAULT  "dump.ckdb"

/* type markers in binary format */
#define CK_RDB_TYPE_STRING  0x01
#define CK_RDB_TYPE_INT     0x02
#define CK_RDB_TYPE_LIST    0x03
#define CK_RDB_TYPE_HASH    0x04
#define CK_RDB_EOF          0xFF

/* save all data to file, returns 0 on success */
int persistence_save(store_t *s, const char *filename);

/* load data from file into store, returns 0 on success, -1 on error */
int persistence_load(store_t *s, const char *filename);

#endif
