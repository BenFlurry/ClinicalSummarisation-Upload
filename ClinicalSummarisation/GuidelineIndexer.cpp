#include "pch.h"
#include "GuidelineIndexer.h"
#include "GuidelineRAG.h"
#include "Helpers.h"

#include "sqlite3.h"
#include "hnswlib.h"

// MuPDF C API
extern "C" {
#include <mupdf/fitz.h>
}

#include <algorithm>
#include <sstream>
#include <cmath>

GuidelineIndexer::GuidelineIndexer() {}
GuidelineIndexer::~GuidelineIndexer() {}

static std::string CleanText(const char* raw) {
    std::string s(raw);
    // collapse newlines into spaces
    std::replace(s.begin(), s.end(), '\n', ' ');
    // trim leading/trailing whitespace
    size_t start = s.find_first_not_of(' ');
    size_t end = s.find_last_not_of(' ');
    if (start == std::string::npos) return "";
    return s.substr(start, end - start + 1);
}

static int CountWords(const std::string& s) {
    std::istringstream iss(s);
    int count = 0;
    std::string word;
    while (iss >> word) ++count;
    return count;
}

// open / create the guidelines SQLite database
static sqlite3* OpenGuidelinesDb(const std::filesystem::path& basePath) {
    std::filesystem::path dbPath = basePath / "guidelines.sqlite";

    // use UTF-8 path for SQLite compatibility
    auto u8path = dbPath.u8string();
    std::string dbPathUtf8(u8path.begin(), u8path.end());

    sqlite3* db = nullptr;
    int rc = sqlite3_open_v2(dbPathUtf8.c_str(), &db,
        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, nullptr);
    if (rc != SQLITE_OK) {
        std::string err = db ? sqlite3_errmsg(db) : "unknown";
        if (db) sqlite3_close(db);
        throw std::runtime_error("Failed to open guidelines.sqlite: " + err);
    }

    // use in-memory journal to avoid file system journal issues in MSIX sandbox
    sqlite3_exec(db, "PRAGMA journal_mode=MEMORY;", nullptr, nullptr, nullptr);
    sqlite3_exec(db, "PRAGMA synchronous=NORMAL;", nullptr, nullptr, nullptr);

    // ensure the table exists (idempotent)
    const char* ddl =
        "CREATE TABLE IF NOT EXISTS guidelines ("
        "  chunk_id INTEGER PRIMARY KEY,"
        "  guideline_name TEXT,"
        "  page_number INTEGER,"
        "  bbox_x0 REAL, bbox_y0 REAL, bbox_x1 REAL, bbox_y1 REAL,"
        "  chunk_text TEXT"
        ");";
    char* errMsg = nullptr;
    int ddlRc = sqlite3_exec(db, ddl, nullptr, nullptr, &errMsg);
    if (ddlRc != SQLITE_OK) {
        OutputDebugStringA(("Indexer: CREATE TABLE failed: " + std::string(errMsg ? errMsg : "unknown") + "\n").c_str());
        if (errMsg) sqlite3_free(errMsg);
    }

    // quick check that the database is working
    char* writeTestErr = nullptr;
    int writeTestRc = sqlite3_exec(db,
        "INSERT INTO guidelines VALUES(-1,'_writetest',0,0,0,0,0,'test');"
        "DELETE FROM guidelines WHERE chunk_id=-1;",
        nullptr, nullptr, &writeTestErr);
    if (writeTestRc != SQLITE_OK) {
        std::string err = writeTestErr ? writeTestErr : "unknown";
        if (writeTestErr) sqlite3_free(writeTestErr);
        OutputDebugStringA(("Indexer: WRITE TEST FAILED: " + err + "\n").c_str());
        throw std::runtime_error("SQLite write test failed: " + err);
    }
    OutputDebugStringA("Indexer: SQLite write test passed\n");

    return db;
}

// get the next available chunk_id 
static int GetNextChunkId(sqlite3* db) {
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db, "SELECT COALESCE(MAX(chunk_id), -1) + 1 FROM guidelines;", -1, &stmt, nullptr);
    int nextId = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        nextId = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);
    return nextId;
}

int GuidelineIndexer::IndexPdf(const std::filesystem::path& pdfSourcePath, const std::filesystem::path& basePath, GuidelineRAG& rag, ProgressCallback progress) {
    std::string pdfFilename = pdfSourcePath.filename().string();

    // check if pdf if already indexed
    {
        sqlite3* checkDb = OpenGuidelinesDb(basePath);
        sqlite3_stmt* checkStmt = nullptr;
        sqlite3_prepare_v2(checkDb, "SELECT COUNT(*) FROM guidelines WHERE guideline_name = ?;", -1, &checkStmt, nullptr);
        sqlite3_bind_text(checkStmt, 1, pdfFilename.c_str(), -1, SQLITE_TRANSIENT);
        bool alreadyIndexed = false;
        if (sqlite3_step(checkStmt) == SQLITE_ROW && sqlite3_column_int(checkStmt, 0) > 0) {
            alreadyIndexed = true;
        }
        sqlite3_finalize(checkStmt);
        sqlite3_close(checkDb);
        if (alreadyIndexed) {
            OutputDebugStringA(("Indexer: " + pdfFilename + " already indexed, skipping\n").c_str());
            return 0;
        }
    }

    // copy the PDF into basePath so it is always available in the app regardless of user save location
    std::filesystem::path destPath = basePath / pdfFilename;
    if (!std::filesystem::exists(destPath)) {
        std::filesystem::copy_file(pdfSourcePath, destPath,
            std::filesystem::copy_options::overwrite_existing);
    }
    OutputDebugStringA(("Indexer: copied PDF to " + destPath.string() + "\n").c_str());

    // open MuPDF document
    fz_context* ctx = fz_new_context(nullptr, nullptr, FZ_STORE_DEFAULT);
    if (!ctx) throw std::runtime_error("MuPDF: failed to create context");
    fz_register_document_handlers(ctx);

    fz_document* doc = nullptr;
    fz_try(ctx) {
        doc = fz_open_document(ctx, destPath.string().c_str());
    }
    fz_catch(ctx) {
        std::string err = fz_caught_message(ctx);
        fz_drop_context(ctx);
        throw std::runtime_error("MuPDF: failed to open PDF: " + err);
    }

    int pageCount = fz_count_pages(ctx, doc);

    // open SQLite
    sqlite3* db = OpenGuidelinesDb(basePath);
    int chunkId = GetNextChunkId(db);

    const char* insertSql =
        "INSERT INTO guidelines (chunk_id, guideline_name, page_number, "
        "bbox_x0, bbox_y0, bbox_x1, bbox_y1, chunk_text) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?);";
    sqlite3_stmt* insertStmt = nullptr;
    int prepRc = sqlite3_prepare_v2(db, insertSql, -1, &insertStmt, nullptr);
    if (prepRc != SQLITE_OK || !insertStmt) {
        std::string err = sqlite3_errmsg(db);
        OutputDebugStringA(("Indexer: prepare INSERT failed: " + err + "\n").c_str());
        sqlite3_close(db);
        fz_drop_document(ctx, doc);
        fz_drop_context(ctx);
        throw std::runtime_error("Failed to prepare INSERT statement: " + err);
    }

    // load current HNSW index or create new one
    std::filesystem::path indexPath = basePath / "nice_guidelines.bin";
    int dim = 768;
    int maxElements = 50000;

    hnswlib::L2Space space(dim);
    hnswlib::HierarchicalNSW<float>* index = nullptr;

    if (std::filesystem::exists(indexPath)) {
        index = new hnswlib::HierarchicalNSW<float>(&space, indexPath.string());
        // detect HNSW/SQLite mismatch if HNSW has entries but SQLite starts at a lower chunkId, the HNSW is stale (from a failed run) where inserts didn't commit). Discard it and start fresh.
        if (index->cur_element_count > 0 && chunkId < static_cast<int>(index->cur_element_count)) {
            OutputDebugStringA(("Indexer: HNSW/SQLite mismatch (HNSW=" +
                std::to_string(index->cur_element_count) + " SQLite next=" +
                std::to_string(chunkId) + ") — discarding stale HNSW\n").c_str());
            delete index;
            std::filesystem::remove(indexPath);
            index = new hnswlib::HierarchicalNSW<float>(&space, maxElements, 16, 200);
        }
        // resize if needed
        else if (index->cur_element_count + 5000 > index->max_elements_) {
            index->resizeIndex(index->max_elements_ + maxElements);
        }
    }
    else {
        index = new hnswlib::HierarchicalNSW<float>(&space, maxElements, 16, 200);
    }

    int chunksIndexed = 0;
    int insertErrors = 0;
    int totalBlocks = 0;
    int textBlocks = 0;
    int passedFilter = 0;
    int pagesLoaded = 0;
    int pagesFailed = 0;
    std::string firstInsertError;

    // iterate pages and extract text blocks
    char* beginErr = nullptr;
    int beginRc = sqlite3_exec(db, "BEGIN TRANSACTION;", nullptr, nullptr, &beginErr);
    if (beginRc != SQLITE_OK) {
        std::string errStr = beginErr ? beginErr : "unknown";
        OutputDebugStringA(("Indexer: BEGIN TRANSACTION failed (rc=" + std::to_string(beginRc) + "): " + errStr + "\n").c_str());
        if (beginErr) sqlite3_free(beginErr);
    }

    for (int pageIdx = 0; pageIdx < pageCount; ++pageIdx) {
        fz_stext_page* textPage = nullptr;
        fz_page* page = nullptr;

        fz_try(ctx) {
            page = fz_load_page(ctx, doc, pageIdx);
            fz_stext_options opts = { 0 };
            textPage = fz_new_stext_page_from_page(ctx, page, &opts);
        }
        fz_catch(ctx) {
            if (page) fz_drop_page(ctx, page);
            pagesFailed++;
            continue;
        }
        pagesLoaded++;

        // walk structured text blocks
        for (fz_stext_block* block = textPage->first_block; block; block = block->next) {
            totalBlocks++;
            if (block->type != FZ_STEXT_BLOCK_TEXT) continue;
            textBlocks++;

            // extract block text by concatenating lines and chars
            std::string blockText;
            for (fz_stext_line* line = block->u.t.first_line; line; line = line->next) {
                for (fz_stext_char* ch = line->first_char; ch; ch = ch->next) {
                    // cast to ascii
                    if (ch->c < 128) {
                        blockText += static_cast<char>(ch->c);
                    }
                    else {
                        // UTF-8 encode for non-ASCII
                        if (ch->c < 0x800) {
                            blockText += static_cast<char>(0xC0 | (ch->c >> 6));
                            blockText += static_cast<char>(0x80 | (ch->c & 0x3F));
                        }
                        else if (ch->c < 0x10000) {
                            blockText += static_cast<char>(0xE0 | (ch->c >> 12));
                            blockText += static_cast<char>(0x80 | ((ch->c >> 6) & 0x3F));
                            blockText += static_cast<char>(0x80 | (ch->c & 0x3F));
                        }
                    }
                }
                blockText += ' '; // space between lines
            }

            std::string cleaned = CleanText(blockText.c_str());
            if (CountWords(cleaned) < 5) continue;
            passedFilter++;

            // bounding box
            float x0 = block->bbox.x0;
            float y0 = block->bbox.y0;
            float x1 = block->bbox.x1;
            float y1 = block->bbox.y1;
            // 1-indexed for viewer
            int pageNumber = pageIdx + 1; 

            // embed
            std::vector<float> embedding = rag.EmbedText(cleaned);

            // add to HNSW
            if (static_cast<size_t>(chunkId) >= index->max_elements_) {
                index->resizeIndex(index->max_elements_ + maxElements);
            }
            index->addPoint(embedding.data(), static_cast<size_t>(chunkId));

            // add to SQLite
            sqlite3_reset(insertStmt);
            sqlite3_bind_int(insertStmt, 1, chunkId);
            sqlite3_bind_text(insertStmt, 2, pdfFilename.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int(insertStmt, 3, pageNumber);
            sqlite3_bind_double(insertStmt, 4, x0);
            sqlite3_bind_double(insertStmt, 5, y0);
            sqlite3_bind_double(insertStmt, 6, x1);
            sqlite3_bind_double(insertStmt, 7, y1);
            sqlite3_bind_text(insertStmt, 8, cleaned.c_str(), -1, SQLITE_TRANSIENT);
            int stepRc = sqlite3_step(insertStmt);
            if (stepRc != SQLITE_DONE && stepRc != SQLITE_ROW) {
                std::string err = sqlite3_errmsg(db);
                OutputDebugStringA(("Indexer: INSERT failed for chunk " + std::to_string(chunkId) +
                    " rc=" + std::to_string(stepRc) + " err=" + err + "\n").c_str());
                if (insertErrors == 0) firstInsertError = err;
                insertErrors++;
            }

            chunkId++;
            // Only count successful inserts
            if (stepRc == SQLITE_DONE || stepRc == SQLITE_ROW) {
                chunksIndexed++;
            }

            if (progress) {
                progress({ chunksIndexed, chunksIndexed, pdfFilename });
            }
        }

        fz_drop_stext_page(ctx, textPage);
        fz_drop_page(ctx, page);
    }

    char* commitErr = nullptr;
    int commitRc = sqlite3_exec(db, "COMMIT;", nullptr, nullptr, &commitErr);
    if (commitRc != SQLITE_OK) {
        OutputDebugStringA(("Indexer: COMMIT failed: rc=" + std::to_string(commitRc) +
            " err=" + std::string(commitErr ? commitErr : "unknown") + "\n").c_str());
        if (commitErr) sqlite3_free(commitErr);
    }

    // verify data was committed
    int verifiedRows = 0;
    sqlite3_stmt* verifyStmt = nullptr;
    sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM guidelines;", -1, &verifyStmt, nullptr);
    if (verifyStmt && sqlite3_step(verifyStmt) == SQLITE_ROW) {
        verifiedRows = sqlite3_column_int(verifyStmt, 0);
        OutputDebugStringA(("Indexer: verified " + std::to_string(verifiedRows) + " rows in guidelines table\n").c_str());
    }
    if (verifyStmt) sqlite3_finalize(verifyStmt);

    if (insertErrors > 0) {
        OutputDebugStringA(("Indexer: WARNING - " + std::to_string(insertErrors) + " INSERT errors occurred\n").c_str());
    }

    sqlite3_finalize(insertStmt);
    sqlite3_close(db);

    // if verification shows 0 rows but we expected data, throw so the UI shows the actual error
    if (chunksIndexed > 0 && verifiedRows == 0) {
        // dont save HNSW if SQLite has no data
        delete index;
        fz_drop_document(ctx, doc);
        fz_drop_context(ctx);
        std::string errDetail = firstInsertError.empty() ? "unknown - inserts reported success but no rows found" : firstInsertError;
        throw std::runtime_error("SQLite has 0 rows after " + std::to_string(chunksIndexed) +
            " inserts. First error: " + errDetail +
            " (BEGIN rc=" + std::to_string(beginRc) +
            ", COMMIT rc=" + std::to_string(commitRc) + ")");
    }

    // save the updated HNSW index
    index->setEf(50);
    index->saveIndex(indexPath.string());
    delete index;

    // clean up MuPDF
    fz_drop_document(ctx, doc);
    fz_drop_context(ctx);

    OutputDebugStringA(("Indexer: indexed " + std::to_string(chunksIndexed) + " chunks from " + pdfFilename + "\n").c_str());

    // if 0 chunks despite having pages, throw with full pipeline diagnostics 
    if (chunksIndexed == 0 && pageCount > 0) {
        delete index;
        throw std::runtime_error(
            "Pipeline: pages=" + std::to_string(pageCount) +
            " loaded=" + std::to_string(pagesLoaded) +
            " failed=" + std::to_string(pagesFailed) +
            " blocks=" + std::to_string(totalBlocks) +
            " textBlocks=" + std::to_string(textBlocks) +
            " passedFilter=" + std::to_string(passedFilter) +
            " insertErr=" + std::to_string(insertErrors) +
            " beginRc=" + std::to_string(beginRc) +
            " commitRc=" + std::to_string(commitRc) +
            " firstErr=[" + firstInsertError + "]");
    }

    return chunksIndexed;
}

// remove guidelines from a specific pdf 
void GuidelineIndexer::RemoveGuideline(const std::string& guidelineName, const std::filesystem::path& basePath, GuidelineRAG& rag) {
    sqlite3* db = OpenGuidelinesDb(basePath);

    // delete all chunks for this guideline
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db, "DELETE FROM guidelines WHERE guideline_name = ?;", -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, guidelineName.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    sqlite3_close(db);

    // delete the copied PDF file
    std::filesystem::path pdfPath = basePath / guidelineName;
    if (std::filesystem::exists(pdfPath)) {
        std::filesystem::remove(pdfPath);
    }

    // HNSW doesnt support deletion, rebuild from scratch
    RebuildHnswFromSqlite(basePath, rag);
}

// rebuild HNSW if the database changes 
void GuidelineIndexer::RebuildHnswFromSqlite(const std::filesystem::path& basePath, GuidelineRAG& rag) {
    sqlite3* db = OpenGuidelinesDb(basePath);

    // count rows
    sqlite3_stmt* countStmt = nullptr;
    sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM guidelines;", -1, &countStmt, nullptr);
    int totalRows = 0;
    if (sqlite3_step(countStmt) == SQLITE_ROW) {
        totalRows = sqlite3_column_int(countStmt, 0);
    }
    sqlite3_finalize(countStmt);

    int dim = 768;
    int maxElements = (totalRows > 0) ? totalRows + 1000 : 10000;

    hnswlib::L2Space space(dim);
    auto* index = new hnswlib::HierarchicalNSW<float>(&space, maxElements, 16, 200);

    // iterate all chunks and re-embed
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db, "SELECT chunk_id, chunk_text FROM guidelines;", -1, &stmt, nullptr);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        int cid = sqlite3_column_int(stmt, 0);
        const char* text = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        if (!text) continue;

        std::vector<float> embedding = rag.EmbedText(std::string(text));
        index->addPoint(embedding.data(), static_cast<size_t>(cid));
    }
    sqlite3_finalize(stmt);
    sqlite3_close(db);

    // save
    std::filesystem::path indexPath = basePath / "nice_guidelines.bin";
    index->setEf(50);
    index->saveIndex(indexPath.string());
    delete index;

    OutputDebugStringA("Indexer: HNSW index rebuilt from SQLite\n");
}

std::vector<std::string> GuidelineIndexer::GetIndexedGuidelines(const std::filesystem::path& basePath) {
    std::vector<std::string> names;
    std::filesystem::path dbPath = basePath / "guidelines.sqlite";
    if (!std::filesystem::exists(dbPath)) return names;

    sqlite3* db = nullptr;
    if (sqlite3_open_v2(dbPath.string().c_str(), &db, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK) {
        if (db) sqlite3_close(db);
        return names;
    }

    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db, "SELECT DISTINCT guideline_name FROM guidelines ORDER BY guideline_name;", -1, &stmt, nullptr);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char* text = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        if (text) names.emplace_back(text);
    }
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return names;
}
