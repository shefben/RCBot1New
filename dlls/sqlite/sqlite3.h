// Content of sqlite3.h
// This is a placeholder. In a real scenario, this would be the actual sqlite3.h content.
#ifndef SQLITE3_H
#define SQLITE3_H

#define SQLITE_OK 0

typedef struct sqlite3 sqlite3;
typedef struct sqlite3_stmt sqlite3_stmt;

// Minimal set of function declarations needed for this subtask's compilation
// A real sqlite3.h has many more.
 extern const char sqlite3_errmsg[];
 int sqlite3_open(const char *filename, sqlite3 **ppDb);
 int sqlite3_close(sqlite3 *pDb);
 int sqlite3_exec(sqlite3 *pDb, const char *sql, int (*callback)(void*,int,char**,char**), void *pArg, char **errmsg);
 void sqlite3_free(void *p);
 int sqlite3_prepare_v2(sqlite3 *db, const char *zSql, int nByte, sqlite3_stmt **ppStmt, const char **pzTail);
 int sqlite3_step(sqlite3_stmt *pStmt);
 int sqlite3_finalize(sqlite3_stmt *pStmt);
 const unsigned char *sqlite3_column_text(sqlite3_stmt*, int iCol);
 int sqlite3_column_int(sqlite3_stmt*, int iCol);
 double sqlite3_column_double(sqlite3_stmt*, int iCol);
 long long sqlite3_last_insert_rowid(sqlite3*);
 int sqlite3_bind_text(sqlite3_stmt*, int, const char*, int n, void(*)(void*));
 int sqlite3_bind_int(sqlite3_stmt*, int, int);
 int sqlite3_bind_double(sqlite3_stmt*, int, double);
 int sqlite3_bind_int64(sqlite3_stmt*, int, long long);


#define SQLITE_STATIC      ((void(*)(void*))0)
#define SQLITE_TRANSIENT   ((void(*)(void*))-1)


#endif // SQLITE3_H
