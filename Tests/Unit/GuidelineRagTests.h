#pragma once

// Verifies initialize delegation through interface seam.
TEST(GuidelineRAG, UsesInjectedBackendForInitialize) {
    GuidelineRAG rag;
    auto backend = std::make_shared<test_fakes::RagBackend>();
    rag.SetBackendForTesting(backend);
    rag.Initialize("C:\\m", "C:\\d");
    EXPECT_TRUE(backend->initialized);
}

// Verifies embed delegation through seam.
TEST(GuidelineRAG, UsesInjectedBackendForEmbedText) {
    GuidelineRAG rag;
    auto backend = std::make_shared<test_fakes::RagBackend>();
    rag.SetBackendForTesting(backend);

    auto emb = rag.EmbedText("x");

    ASSERT_EQ(emb.size(), 3u);
    EXPECT_NEAR(emb[2], 0.3f, 1e-6f);
}

// Verifies query delegation through seam.
TEST(GuidelineRAG, UsesInjectedBackendForQuery) {
    GuidelineRAG rag;
    auto backend = std::make_shared<test_fakes::RagBackend>();
    backend->queryResults = { {1, "g", 2, 0, 0, 1, 1, "txt", 0.1f} };
    rag.SetBackendForTesting(backend);

    auto r = rag.Query("sum", 3);

    ASSERT_EQ(r.size(), 1u);
    EXPECT_EQ(r[0].guidelineName, "g");
}

// Verifies reload delegation through seam.
TEST(GuidelineRAG, UsesInjectedBackendForReload) {
    GuidelineRAG rag;
    auto backend = std::make_shared<test_fakes::RagBackend>();
    rag.SetBackendForTesting(backend);
    rag.ReloadIndex();

    EXPECT_TRUE(backend->reloaded);
}

// Verifies has-index delegation through seam.
TEST(GuidelineRAG, UsesInjectedBackendForHasIndex) {
    GuidelineRAG rag;
    auto backend = std::make_shared<test_fakes::RagBackend>();
    backend->hasIndex = true;
    rag.SetBackendForTesting(backend);

    EXPECT_TRUE(rag.HasIndex());
}

// Verifies null-impl safe path.
TEST(GuidelineRAG, QueryReturnsEmptyWhenNoBackendAndUninitialized) {
    GuidelineRAG rag;
    auto r = rag.Query("sum", 3);

    EXPECT_TRUE(r.empty());
}

// Verifies query text and topK are forwarded to injected backend.
TEST(GuidelineRAG, QueryForwardsInputAndTopK) {
    GuidelineRAG rag;
    auto backend = std::make_shared<test_fakes::RagBackend>();
    rag.SetBackendForTesting(backend);
    (void)rag.Query("abc", 5);

    EXPECT_EQ(backend->lastQueryText, "abc");
    EXPECT_EQ(backend->lastTopK, 5);
}

// Verifies embed text is forwarded to injected backend.
TEST(GuidelineRAG, EmbedTextForwardsInput) {
    GuidelineRAG rag;
    auto backend = std::make_shared<test_fakes::RagBackend>();
    rag.SetBackendForTesting(backend);
    (void)rag.EmbedText("clinical");

    EXPECT_EQ(backend->lastEmbedText, "clinical");
}

// Verifies default topK value is forwarded when omitted.
TEST(GuidelineRAG, QueryUsesDefaultTopKWhenOmitted) {
    GuidelineRAG rag;
    auto backend = std::make_shared<test_fakes::RagBackend>();
    rag.SetBackendForTesting(backend);

    (void)rag.Query("summary");

    EXPECT_EQ(backend->lastTopK, 3);
}

// Verifies backend false index state is reflected by wrapper.
TEST(GuidelineRAG, HasIndexFalseFromBackend) {
    GuidelineRAG rag;
    auto backend = std::make_shared<test_fakes::RagBackend>();
    backend->hasIndex = false;
    rag.SetBackendForTesting(backend);

    EXPECT_FALSE(rag.HasIndex());
}

// Verifies backend true index state can be toggled after injection.
TEST(GuidelineRAG, HasIndexReflectsBackendToggle) {
    GuidelineRAG rag;
    auto backend = std::make_shared<test_fakes::RagBackend>();
    rag.SetBackendForTesting(backend);
    EXPECT_FALSE(rag.HasIndex());
    backend->hasIndex = true;
    EXPECT_TRUE(rag.HasIndex());
}
