#pragma once

// Verifies cancellation while loop is waiting returns empty transcript.
TEST(TranscriptionEngine, CancelDuringLoopReturnsEmpty) {
    AudioTranscriptionBridge bridge;
    auto backend = std::make_shared<test_fakes::TranscriptionBackend>();
    backend->generatedChunks = { {"ignored", 0.0f, 1.0f} };

    TranscriptionEngine engine(&bridge, backend);
    std::string output;

    std::thread worker([&]() {
        output = engine.ProcessLoop();
        });

    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    engine.Cancel();

    AudioChunk unblock;
    unblock.audioData = { 0.1f };
    unblock.isLastChunk = true;
    bridge.Push(unblock);

    worker.join();
    EXPECT_TRUE(output.empty());
}

// Verifies transcription engine labels a long segment as doctor when similarity is high.
TEST(TranscriptionEngine, LabelsDoctorWithHighSimilarity) {
    AudioTranscriptionBridge bridge;
    auto backend = std::make_shared<test_fakes::TranscriptionBackend>();
    backend->generatedChunks = { {"hello", 0.0f, 1.0f} };
    backend->embeddingToReturn = { 1.0f, 0.0f };

    TranscriptionEngine engine(&bridge, backend);
    engine.SetDoctorProfile({ 1.0f, 0.0f });

    AudioChunk c;
    c.audioData.assign(20000, 0.2f);
    c.isLastChunk = true;
    bridge.Push(c);

    auto text = engine.ProcessLoop();
    EXPECT_NE(text.find("[Doctor"), std::string::npos);
}

// Verifies transcription engine labels a long segment as patient when similarity is low.
TEST(TranscriptionEngine, LabelsPatientWithLowSimilarity) {
    AudioTranscriptionBridge bridge;
    auto backend = std::make_shared<test_fakes::TranscriptionBackend>();
    backend->generatedChunks = { {"response", 0.0f, 1.0f} };
    backend->embeddingToReturn = { -1.0f, 0.0f };

    TranscriptionEngine engine(&bridge, backend);
    engine.SetDoctorProfile({ 1.0f, 0.0f });

    AudioChunk c;
    c.audioData.assign(20000, 0.2f);
    c.isLastChunk = true;
    bridge.Push(c);

    auto text = engine.ProcessLoop();
    EXPECT_NE(text.find("[Patient"), std::string::npos);
}

// Verifies transcription engine keeps unknown label for short segments.
TEST(TranscriptionEngine, KeepsUnknownForShortSegments) {
    AudioTranscriptionBridge bridge;
    auto backend = std::make_shared<test_fakes::TranscriptionBackend>();
    backend->generatedChunks = { {"short", 0.0f, 0.2f} };

    TranscriptionEngine engine(&bridge, backend);
    engine.SetDoctorProfile({ 1.0f, 0.0f });

    AudioChunk c;
    c.audioData.assign(5000, 0.2f);
    c.isLastChunk = true;
    bridge.Push(c);

    auto text = engine.ProcessLoop();
    EXPECT_NE(text.find("[[Unknown]] short"), std::string::npos);
}

// Verifies transcription backend model initialization path is passed through.
TEST(TranscriptionEngine, InitialiseModelDelegatesPath) {
    AudioTranscriptionBridge bridge;
    auto backend = std::make_shared<test_fakes::TranscriptionBackend>();
    TranscriptionEngine engine(&bridge, backend);

    Helpers::SetBasePathOverride("C:\\Models");
    engine.InitialiseModel();
    Helpers::ClearBasePathOverride();

    EXPECT_EQ(backend->initialisedModelPath, std::filesystem::path("C:\\Models\\whisper-medium-i4"));
}

// Verifies multiple generated chunks are appended into transcript.
TEST(TranscriptionEngine, AppendsMultipleChunks) {
    AudioTranscriptionBridge bridge;
    auto backend = std::make_shared<test_fakes::TranscriptionBackend>();
    backend->generatedChunks = { {"a", 0.0f, 1.0f}, {"b", 0.0f, 1.0f} };
    backend->embeddingToReturn = { 1.0f, 0.0f };

    TranscriptionEngine engine(&bridge, backend);
    engine.SetDoctorProfile({ 1.0f, 0.0f });

    AudioChunk c;
    c.audioData.assign(20000, 0.2f);
    c.isLastChunk = true;
    bridge.Push(c);

    auto text = engine.ProcessLoop();
    EXPECT_NE(text.find("a"), std::string::npos);
    EXPECT_NE(text.find("b"), std::string::npos);
}

// Verifies cancellation flag accessor reflects cancellation calls.
TEST(TranscriptionEngine, IsCancelledReflectsState) {
    AudioTranscriptionBridge bridge;
    auto backend = std::make_shared<test_fakes::TranscriptionBackend>();
    TranscriptionEngine engine(&bridge, backend);

    EXPECT_FALSE(engine.IsCancelled());
    engine.Cancel();
    EXPECT_TRUE(engine.IsCancelled());
}

// Verifies generated chunks function is invoked once per input chunk.
TEST(TranscriptionEngine, GenerateCalledForInputChunk) {
    AudioTranscriptionBridge bridge;
    auto backend = std::make_shared<test_fakes::TranscriptionBackend>();
    backend->generatedChunks = { {"x", 0.0f, 0.2f} };

    TranscriptionEngine engine(&bridge, backend);

    AudioChunk c;
    c.audioData.assign(1000, 0.2f);
    c.isLastChunk = true;
    bridge.Push(c);

    (void)engine.ProcessLoop();
    EXPECT_EQ(backend->generateCalls, 1);
}

// Verifies out-of-range timestamps do not crash and still produce output line.
TEST(TranscriptionEngine, OutOfRangeTimestampsAreClamped) {
    AudioTranscriptionBridge bridge;
    auto backend = std::make_shared<test_fakes::TranscriptionBackend>();
    backend->generatedChunks = { {"edge", 100.0f, 101.0f} };

    TranscriptionEngine engine(&bridge, backend);

    AudioChunk c;
    c.audioData.assign(1000, 0.2f);
    c.isLastChunk = true;
    bridge.Push(c);

    auto text = engine.ProcessLoop();
    EXPECT_NE(text.find("edge"), std::string::npos);
}
