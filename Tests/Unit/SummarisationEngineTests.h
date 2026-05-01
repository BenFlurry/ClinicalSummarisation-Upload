#pragma once

// Verifies summarisation load delegates model path to injected backend.
TEST(SummarisationEngine, DelegatesLoadModelToBackend) {
    auto backend = std::make_shared<test_fakes::SummarisationBackend>();
    SummarisationEngine engine;
    engine.SetBackendForTesting(backend);

    Helpers::SetBasePathOverride("C:\\Models");
    engine.loadModel();
    Helpers::ClearBasePathOverride();

    EXPECT_EQ(backend->loadedModelPath, std::filesystem::path("C:\\Models\\Med42-int4"));
}

// Verifies summarisation prompt and generation parameters are forwarded to backend.
TEST(SummarisationEngine, DelegatesGenerateToBackend) {
    auto backend = std::make_shared<test_fakes::SummarisationBackend>();
    backend->response = "SOAP";

    SummarisationEngine engine;
    engine.SetBackendForTesting(backend);

    auto result = engine.GenerateFromTranscription("patient has pain");

    EXPECT_EQ(result, "SOAP");
    EXPECT_NE(backend->lastPrompt.find("TRANSCRIPT:\npatient has pain"), std::string::npos);
    EXPECT_EQ(backend->lastMaxTokens, 1024);
    EXPECT_NEAR(backend->lastTemperature, 0.3f, 1e-6f);
}

// Verifies summarisation backend can return empty output.
TEST(SummarisationEngine, HandlesEmptyBackendOutput) {
    auto backend = std::make_shared<test_fakes::SummarisationBackend>();
    backend->response = "";

    SummarisationEngine engine;
    engine.SetBackendForTesting(backend);

    EXPECT_TRUE(engine.GenerateFromTranscription("anything").empty());
}

// Verifies multiline transcript is forwarded into prompt.
TEST(SummarisationEngine, ForwardsMultilineTranscript) {
    auto backend = std::make_shared<test_fakes::SummarisationBackend>();
    SummarisationEngine engine;
    engine.SetBackendForTesting(backend);

    (void)engine.GenerateFromTranscription("line1\nline2");

    EXPECT_NE(backend->lastPrompt.find("line1\nline2"), std::string::npos);
}

// Verifies system prompt header is included.
TEST(SummarisationEngine, IncludesSystemPromptHeader) {
    auto backend = std::make_shared<test_fakes::SummarisationBackend>();
    SummarisationEngine engine;
    engine.SetBackendForTesting(backend);

    (void)engine.GenerateFromTranscription("x");

    EXPECT_NE(backend->lastPrompt.find("<|system|>"), std::string::npos);
}

// Verifies assistant marker is included in composed prompt.
TEST(SummarisationEngine, IncludesAssistantMarker) {
    auto backend = std::make_shared<test_fakes::SummarisationBackend>();
    SummarisationEngine engine;
    engine.SetBackendForTesting(backend);

    (void)engine.GenerateFromTranscription("x");

    EXPECT_NE(backend->lastPrompt.find("<|assistant|>"), std::string::npos);
}

// Verifies empty transcript still composes a valid prompt.
TEST(SummarisationEngine, HandlesEmptyTranscriptInput) {
    auto backend = std::make_shared<test_fakes::SummarisationBackend>();
    SummarisationEngine engine;
    engine.SetBackendForTesting(backend);

    (void)engine.GenerateFromTranscription("");

    EXPECT_NE(backend->lastPrompt.find("TRANSCRIPT:\n"), std::string::npos);
}
