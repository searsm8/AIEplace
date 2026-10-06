/**
 * @file DesignReader.h
 * @brief Native LEF / DEF / Bookshelf readers (TODO #43, replacing the Limbo parsers). Each
 *        reads one input and fires DataBase's callbacks in file order with ParseRecords. They
 *        cover the subset of each format our benchmarks use; anything outside it is a logged
 *        error and a false return, never a silent skip of data DataBase consumes. Meow.
 */
#pragma once
#include "Common.h"

AIEPLACE_NAMESPACE_BEGIN

class DataBase;

bool readLefFile(const fs::path& lef_file, DataBase& db);
bool readDefFile(const fs::path& def_file, DataBase& db);
/// Reads every file the .aux lists, in Limbo's order (.scl, .nodes, .nets, .wts, .pl), then
/// fires bookshelf_end().
bool readBookshelfAux(const fs::path& aux_file, DataBase& db);

AIEPLACE_NAMESPACE_END
