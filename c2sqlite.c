#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <clang-c/Index.h>
#include <clang-c/Platform.h>
#include <sqlite3.h>

#define SQL_CREATE_FUNCTION_DECLARATIONS_TABLE "CREATE TABLE IF NOT EXISTS function_declaration (name VARCHAR, file VARCHAR, line INTEGER, col INTEGER);"
#define SQL_INSERT_FUNCTION_DECLARATION "INSERT OR REPLACE INTO function_declaration (name, file, line, col) VALUES (?, ?, ?, ?);"

#define SQL_CREATE_FUNCTION_CALLING_TABLE "CREATE TABLE IF NOT EXISTS function_calling (caller VARCHAR, callee VARCHAR, file VARCHAR, line INTEGER, col INTEGER);"
#define SQL_INSERT_FUNCTION_CALLING "INSERT OR REPLACE INTO function_calling (caller, callee, file, line, col) VALUES (?, ?, ?, ?, ?);"

#define SQL_CREATE_FUNCTION_PARAM_TABLE "CREATE TABLE IF NOT EXISTS function_param (function VARCHAR, name VARCHAR, type VARCHAR, id INTEGER);"
#define SQL_INSERT_FUNCTION_PARAM "INSERT OR REPLACE INTO function_param (function, name, type, id) VALUES (?, ?, ?, ?);"

sqlite3*
db_open(char *dbfile)
{
	char *errmsg;
	sqlite3 *db;

	if (sqlite3_open(dbfile, &db) != SQLITE_OK) {
		fprintf(stderr, "could not open %s\n", dbfile);
		exit(-1);
	}

	if (sqlite3_exec(db, SQL_CREATE_FUNCTION_DECLARATIONS_TABLE, NULL, 0, &errmsg) != SQLITE_OK) {
		fprintf(stderr, "error at %s: %s\n", SQL_CREATE_FUNCTION_DECLARATIONS_TABLE, sqlite3_errmsg(db));
		exit(-1);
	}

	if (sqlite3_exec(db, SQL_CREATE_FUNCTION_CALLING_TABLE, NULL, 0, &errmsg) != SQLITE_OK) {
		fprintf(stderr, "error at %s: %s\n", SQL_CREATE_FUNCTION_CALLING_TABLE, sqlite3_errmsg(db));
		exit(-1);
	}

	if (sqlite3_exec(db, SQL_CREATE_FUNCTION_PARAM_TABLE, NULL, 0, &errmsg) != SQLITE_OK) {
		fprintf(stderr, "error at %s: %s\n", SQL_CREATE_FUNCTION_PARAM_TABLE, sqlite3_errmsg(db));
		exit(-1);
	}

	return db;
}

void
db_close(sqlite3* db)
{
	sqlite3_close(db);
}

void
db_begin(sqlite3* db)
{
	if (sqlite3_exec(db, "BEGIN TRANSACTION;", NULL, 0, NULL) != SQLITE_OK)
	{
		fprintf(stderr, "error at %s: %s\n", "BEGIN TRANSACTION;", sqlite3_errmsg(db));
		exit(-1);
	}
}

void
db_end(sqlite3* db)
{
	if (sqlite3_exec(db, "END TRANSACTION;", NULL, 0, NULL) != SQLITE_OK)
	{
		fprintf(stderr, "error at %s: %s\n", "END TRANSACTION;", sqlite3_errmsg(db));
		exit(-1);
        }
}

void
db_add_funcparam(sqlite3* db, const char *function, const char *name, const char* type, int id)
{
	static sqlite3_stmt *stmt;

	if (stmt == NULL) {
		sqlite3_prepare_v2(db, SQL_INSERT_FUNCTION_PARAM, -1, &stmt, 0);
	}

	sqlite3_bind_text(stmt, 1, function, -1, SQLITE_STATIC);
	sqlite3_bind_text(stmt, 2, name, -1, SQLITE_STATIC);
	sqlite3_bind_text(stmt, 3, type, -1, SQLITE_STATIC);
	sqlite3_bind_int(stmt, 4, id);

	if (sqlite3_step(stmt) != SQLITE_DONE) {
		fprintf(stderr, "Can't insert: %s\n", sqlite3_errmsg(db));
		exit(-1);
	}

	sqlite3_reset(stmt);

}

void
db_add_funccall(sqlite3* db, const char *from, const char *to, const char *file, int line, int col)
{
	static sqlite3_stmt *stmt;

	if (stmt == NULL) {
		sqlite3_prepare_v2(db, SQL_INSERT_FUNCTION_CALLING, -1, &stmt, 0);
	}

	sqlite3_bind_text(stmt, 1, from, -1, SQLITE_STATIC);
	sqlite3_bind_text(stmt, 2, to, -1, SQLITE_STATIC);
	sqlite3_bind_text(stmt, 3, file, -1, SQLITE_STATIC);
	sqlite3_bind_int(stmt, 4, line);
	sqlite3_bind_int(stmt, 5, col);

	if (sqlite3_step(stmt) != SQLITE_DONE) {
		fprintf(stderr, "Can't insert: %s\n", sqlite3_errmsg(db));
		exit(-1);
	}

	sqlite3_reset(stmt);
}

void
db_add_funcdecl(sqlite3* db, const char *name, const char *file, int line, int col)
{
	static sqlite3_stmt *stmt;

	if (stmt == NULL) {
		sqlite3_prepare_v2(db, SQL_INSERT_FUNCTION_DECLARATION, -1, &stmt, 0);
	}

	sqlite3_bind_text(stmt, 1, name, -1, SQLITE_STATIC);
	sqlite3_bind_text(stmt, 2, file, -1, SQLITE_STATIC);
	sqlite3_bind_int(stmt, 3, line);
	sqlite3_bind_int(stmt, 4, col);

	if (sqlite3_step(stmt) != SQLITE_DONE) {
		fprintf(stderr, "Can't insert: %s\n", sqlite3_errmsg(db));
		exit(-1);
	}

	sqlite3_reset(stmt);
}

struct cursorVisitor_userdata {
	int depth;
	sqlite3 *db;

	char *function_name;
};

enum CXChildVisitResult
functionDeclVisitor(CXCursor cursor, CXCursor parent, CXClientData client_data)
{
	struct cursorVisitor_userdata *cvu = (struct cursorVisitor_userdata*) client_data;
	enum CXCursorKind kind = clang_getCursorKind(cursor);
	CXType type = clang_getCursorType(cursor);
	CXString type_spelling;
	CXString name;

	if (kind == CXCursor_ParmDecl){
		name = clang_getCursorSpelling(cursor);
		type_spelling = clang_getTypeSpelling(type);
		db_add_funcparam(cvu->db, cvu->function_name, clang_getCString(name), clang_getCString(type_spelling), type.kind);
		clang_disposeString(name);
		clang_disposeString(type_spelling);
	}

	return CXChildVisit_Continue;
}

enum CXChildVisitResult
cursorVisitor(CXCursor cursor, CXCursor parent, CXClientData client_data)
{
	struct cursorVisitor_userdata *cvu = (struct cursorVisitor_userdata*) client_data;
	enum CXCursorKind kind = clang_getCursorKind(cursor);
	CXString name = clang_getCursorSpelling(cursor);
	int ret = CXChildVisit_Recurse;

	CXString filename;
	unsigned int line, column;

	CXSourceLocation location = clang_getCursorLocation(cursor);   
	clang_getPresumedLocation(location, &filename, &line, &column);
	if (kind == CXCursor_FunctionDecl) {
		free(cvu->function_name);
		cvu->function_name = strdup(clang_getCString(name));
		db_add_funcdecl(cvu->db, cvu->function_name, clang_getCString(filename), line, column);
		clang_visitChildren(cursor, *functionDeclVisitor, cvu);
	} else if (kind == CXCursor_CallExpr) {
		db_add_funccall(cvu->db, cvu->function_name, clang_getCString(name), clang_getCString(filename), line, column);

		ret = CXChildVisit_Continue;
	}

	clang_disposeString(name);
	return ret;
}

int
main(int argc, const char *argv[])
{
	struct cursorVisitor_userdata cvu;
	CXTranslationUnit TU;
	CXIndex index;

	unlink("test.db");
	cvu.db = db_open("test.db");
	cvu.depth = 0;
	cvu.function_name = NULL;
	db_begin(cvu.db);

	index = clang_createIndex(0, 0);

	for (int i = 1; i < argc; i++) {
		TU = clang_parseTranslationUnit(index, argv[i], NULL, 0, 0, 0, CXTranslationUnit_Incomplete);

		if (TU == NULL) {
			fprintf(stderr, "clang_parseTranslationUnit for %s failed\n", argv[i]);
			exit(-1);
		}

		CXCursor rootCursor = clang_getTranslationUnitCursor(TU);

		clang_visitChildren(rootCursor, *cursorVisitor, &cvu);

		clang_disposeTranslationUnit(TU);
	}

	clang_disposeIndex(index);

	db_end(cvu.db);
	db_close(cvu.db);

	return 0;
}
