#include "pch.h"
#include "GuidelineRAG.h"
#include "Helpers.h"

#include <openvino/openvino.hpp>
#include "openvino/genai/tokenizer.hpp"

// hnswlib is header-only
#include "hnswlib.h"

// sqlite3 amalgamation
#include "sqlite3.h"

#include <numeric>
#include <cmath>
#include <algorithm>

struct GuidelineRAG::Impl {
    ov::Core core;
    ov::CompiledModel model;
    ov::InferRequest request;
    ov::genai::Tokenizer tokenizer;

    hnswlib::L2Space* space = nullptr;
    hnswlib::HierarchicalNSW<float>* index = nullptr;

    sqlite3* db = nullptr;
    int embeddingDim = 768;

    Impl(const std::filesystem::path& tokenizerPath)
        : tokenizer(tokenizerPath) {}

    ~Impl() {
        if (index) delete index;
        if (space) delete space;
        if (db) sqlite3_close(db);
    }
};

GuidelineRAG::GuidelineRAG() {}

GuidelineRAG::~GuidelineRAG() {
    if (m_impl) delete m_impl;
}

void GuidelineRAG::SetBackendForTesting(std::shared_ptr<IGuidelineRAGBackend> backend) {
    m_testBackend = std::move(backend);
}

void GuidelineRAG::Initialize(const std::filesystem::path& modelBasePath, const std::filesystem::path& dataPath) {
    if (m_testBackend) {
        m_testBackend->Initialize(modelBasePath, dataPath);
        return;
    }

    m_basePath = modelBasePath;
    m_dataPath = dataPath;
    std::filesystem::path medcptPath = modelBasePath / "medcpt";
    std::filesystem::path indexPath = dataPath / "nice_guidelines.bin";
    std::filesystem::path dbPath = dataPath / "guidelines.sqlite";

    OutputDebugStringA(("RAG: medcpt path = " + medcptPath.string() + "\n").c_str());
    OutputDebugStringA(("RAG: index path = " + indexPath.string() + "\n").c_str());
    OutputDebugStringA(("RAG: db path = " + dbPath.string() + "\n").c_str());

    m_impl = new Impl(medcptPath);

    std::filesystem::path modelXml = medcptPath / "openvino_model.xml";
    if (!std::filesystem::exists(modelXml)) {
        throw std::runtime_error("MedCPT model not found: " + modelXml.string());
    }
    m_impl->model = m_impl->core.compile_model(modelXml, "CPU");
    m_impl->request = m_impl->model.create_infer_request();

    if (std::filesystem::exists(indexPath)) {
        m_impl->space = new hnswlib::L2Space(m_impl->embeddingDim);
        m_impl->index = new hnswlib::HierarchicalNSW<float>(m_impl->space, indexPath.string());
        m_impl->index->setEf(50);
    }

    if (std::filesystem::exists(dbPath)) {
        int rc = sqlite3_open_v2(dbPath.string().c_str(), &m_impl->db, SQLITE_OPEN_READWRITE, nullptr);
        if (rc != SQLITE_OK) {
            std::string err = "Failed to open guidelines database: " + std::string(sqlite3_errmsg(m_impl->db));
            sqlite3_close(m_impl->db);
            m_impl->db = nullptr;
            throw std::runtime_error(err);
        }
    }
}

std::vector<float> GuidelineRAG::EmbedText(const std::string& text) {
    if (m_testBackend) {
        return m_testBackend->EmbedText(text);
    }

    ov::Tensor tokenizedInputIds = m_impl->tokenizer.encode(text).input_ids;
    auto shape = tokenizedInputIds.get_shape();
    size_t batchSize = shape.size() > 0 ? shape[0] : 1;
    size_t tokenizedSeqLen = shape.size() > 1 ? shape[1] : 1;
    if (batchSize == 0) batchSize = 1;
    if (tokenizedSeqLen == 0) tokenizedSeqLen = 1;

    auto getStaticSeqLen = [this](const char* inputName) -> size_t {
        try {
            auto partialShape = m_impl->model.input(inputName).get_partial_shape();
            if (partialShape.rank().is_static() && partialShape.rank().get_length() >= 2 && partialShape[1].is_static()) {
                return static_cast<size_t>(partialShape[1].get_length());
            }
        }
        catch (...) {}
        return 0;
    };

    size_t inputIdsSeqLen = getStaticSeqLen("input_ids");
    if (inputIdsSeqLen == 0) inputIdsSeqLen = tokenizedSeqLen;
    size_t attentionMaskSeqLen = getStaticSeqLen("attention_mask");
    if (attentionMaskSeqLen == 0) attentionMaskSeqLen = inputIdsSeqLen;
    size_t tokenTypeSeqLen = getStaticSeqLen("token_type_ids");
    if (tokenTypeSeqLen == 0) tokenTypeSeqLen = inputIdsSeqLen;

    auto* origIds = tokenizedInputIds.data<int64_t>();

    auto makePaddedInput = [&](size_t targetSeqLen) {
        ov::Tensor t(ov::element::i64, ov::Shape{ batchSize, targetSeqLen });
        auto* dst = t.data<int64_t>();
        std::fill(dst, dst + batchSize * targetSeqLen, 0);
        size_t copyLen = std::min(tokenizedSeqLen, targetSeqLen);
        for (size_t b = 0; b < batchSize; ++b) {
            std::copy_n(origIds + (b * tokenizedSeqLen), copyLen, dst + (b * targetSeqLen));
        }
        return t;
    };

    auto makeAttentionMask = [&](size_t targetSeqLen) {
        ov::Tensor t(ov::element::i64, ov::Shape{ batchSize, targetSeqLen });
        auto* dst = t.data<int64_t>();
        std::fill(dst, dst + batchSize * targetSeqLen, 0);
        size_t validLen = std::min(tokenizedSeqLen, targetSeqLen);
        for (size_t b = 0; b < batchSize; ++b) {
            std::fill(dst + (b * targetSeqLen), dst + (b * targetSeqLen) + validLen, 1);
        }
        return t;
    };

    auto makeTokenTypes = [&](size_t targetSeqLen) {
        ov::Tensor t(ov::element::i64, ov::Shape{ batchSize, targetSeqLen });
        auto* dst = t.data<int64_t>();
        std::fill(dst, dst + batchSize * targetSeqLen, 0);
        return t;
    };

    auto runInfer = [&](size_t idsLen, size_t maskLen, size_t typeLen) {
        ov::Tensor inputIds = makePaddedInput(idsLen);
        ov::Tensor attentionMask = makeAttentionMask(maskLen);
        ov::Tensor tokenTypeIds = makeTokenTypes(typeLen);

        m_impl->request.set_tensor("input_ids", inputIds);
        m_impl->request.set_tensor("attention_mask", attentionMask);
        m_impl->request.set_tensor("token_type_ids", tokenTypeIds);
        m_impl->request.infer();
    };

    try {
        runInfer(inputIdsSeqLen, attentionMaskSeqLen, tokenTypeSeqLen);
    }
    catch (...) {
        runInfer(1, 1, 1);
    }

    ov::Tensor output = m_impl->request.get_output_tensor();
    const float* outData = output.data<float>();
    int dim = m_impl->embeddingDim;

    std::vector<float> embedding(outData, outData + dim);
    float norm = 0.0f;
    for (float v : embedding) norm += v * v;
    norm = std::sqrt(norm);
    if (norm > 0.0f) {
        for (float& v : embedding) v /= norm;
    }

    return embedding;
}

std::vector<GuidelineResult> GuidelineRAG::Query(const std::string& summaryText, int topK) {
    if (m_testBackend) {
        return m_testBackend->Query(summaryText, topK);
    }

    std::vector<GuidelineResult> results;
    if (!m_impl || !m_impl->db || !m_impl->index) return results;

    size_t elementCount = m_impl->index->cur_element_count;
    if (elementCount == 0) return results;
    int effectiveK = std::min(topK, static_cast<int>(elementCount));

    std::vector<float> queryVec = EmbedText(summaryText);
    auto hnswResult = m_impl->index->searchKnn(queryVec.data(), effectiveK);

    std::vector<std::pair<float, size_t>> hits;
    while (!hnswResult.empty()) {
        hits.push_back(hnswResult.top());
        hnswResult.pop();
    }
    std::reverse(hits.begin(), hits.end());

    const char* sql = "SELECT chunk_id, guideline_name, page_number, bbox_x0, bbox_y0, bbox_x1, bbox_y1, chunk_text FROM guidelines WHERE chunk_id = ?;";
    sqlite3_stmt* stmt = nullptr;
    int prepRc = sqlite3_prepare_v2(m_impl->db, sql, -1, &stmt, nullptr);
    if (prepRc != SQLITE_OK || !stmt) return results;

    for (auto& [dist, chunkId] : hits) {
        sqlite3_reset(stmt);
        sqlite3_bind_int(stmt, 1, static_cast<int>(chunkId));

        int stepRc = sqlite3_step(stmt);
        if (stepRc == SQLITE_ROW) {
            GuidelineResult r{};
            r.chunkId = sqlite3_column_int(stmt, 0);
            const char* nameText = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
            r.guidelineName = nameText ? nameText : "";
            r.pageNumber = sqlite3_column_int(stmt, 2);
            r.bboxX0 = static_cast<float>(sqlite3_column_double(stmt, 3));
            r.bboxY0 = static_cast<float>(sqlite3_column_double(stmt, 4));
            r.bboxX1 = static_cast<float>(sqlite3_column_double(stmt, 5));
            r.bboxY1 = static_cast<float>(sqlite3_column_double(stmt, 6));
            const char* chunkTextRaw = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 7));
            r.chunkText = chunkTextRaw ? chunkTextRaw : "";
            r.distance = dist;
            results.push_back(r);
        }
    }

    sqlite3_finalize(stmt);
    return results;
}

void GuidelineRAG::ReloadIndex() {
    if (m_testBackend) {
        m_testBackend->ReloadIndex();
        return;
    }

    if (!m_impl) return;

    if (m_impl->index) { delete m_impl->index; m_impl->index = nullptr; }
    if (m_impl->space) { delete m_impl->space; m_impl->space = nullptr; }
    if (m_impl->db) { sqlite3_close(m_impl->db); m_impl->db = nullptr; }

    std::filesystem::path indexPath = m_dataPath / "nice_guidelines.bin";
    std::filesystem::path dbPath = m_dataPath / "guidelines.sqlite";

    if (std::filesystem::exists(indexPath)) {
        m_impl->space = new hnswlib::L2Space(m_impl->embeddingDim);
        m_impl->index = new hnswlib::HierarchicalNSW<float>(m_impl->space, indexPath.string());
        m_impl->index->setEf(50);
    }

    if (std::filesystem::exists(dbPath)) {
        int rc = sqlite3_open_v2(dbPath.string().c_str(), &m_impl->db, SQLITE_OPEN_READWRITE, nullptr);
        if (rc != SQLITE_OK) {
            sqlite3_close(m_impl->db);
            m_impl->db = nullptr;
        }
    }
}

bool GuidelineRAG::HasIndex() const {
    if (m_testBackend) {
        return m_testBackend->HasIndex();
    }
    return m_impl && m_impl->db && m_impl->index && m_impl->index->cur_element_count > 0;
}
