/**
 * @file DesignReader.h
 * @brief Native LEF / DEF / Bookshelf readers (TODO #43, replacing the Limbo parsers). Each
 *        reads one input and fires a DesignSink's callbacks in file order with ParseRecords. They
 *        cover the subset of each format our benchmarks use; anything outside it is a logged
 *        error and a false return, never a silent skip of data a sink consumes. Meow.
 */
#pragma once
#include "Common.h"
#include "ParseRecords.h"

AIEPLACE_NAMESPACE_BEGIN

bool readLefFile(const fs::path& lef_file, DesignSink& sink);
bool readDefFile(const fs::path& def_file, DesignSink& sink);
/// Reads every file the .aux lists, in Limbo's order (.scl, .nodes, .nets, .wts, .pl), then
/// fires bookshelf_end().
bool readBookshelfAux(const fs::path& aux_file, DesignSink& sink);
/// One Bookshelf file, by its suffix (.scl / .nodes / .nets / .wts / .pl); no bookshelf_end().
/// For a sink that needs only some of a design's files. Meow.
bool readBookshelfFile(const fs::path& file, DesignSink& sink);

AIEPLACE_NAMESPACE_END
