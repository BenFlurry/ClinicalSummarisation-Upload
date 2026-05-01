#pragma once

// Verifies speaker encoder delegates initialization to injected backend.
TEST(SpeakerEncoder, DelegatesInitializeToBackend) {
    auto fake = std::make_shared<test_fakes::SpeakerEncoderBackend>();
    SpeakerEncoder encoder;
    encoder.SetBackendForTesting(fake);
    encoder.Initialize("model.xml", "CPU");

    EXPECT_EQ(fake->initModel, "model.xml");
    EXPECT_EQ(fake->initDevice, "CPU");
    EXPECT_TRUE(encoder.is_loaded);
}

// Verifies speaker encoder delegates embedding to injected backend.
TEST(SpeakerEncoder, DelegatesEmbeddingToBackend) {
    auto fake = std::make_shared<test_fakes::SpeakerEncoderBackend>();
    SpeakerEncoder encoder;
    encoder.SetBackendForTesting(fake);

    std::vector<float> input{ 3.0f, 4.0f };
    auto out = encoder.GetEmbedding(input);

    EXPECT_EQ(fake->lastInput, input);
    EXPECT_EQ(out, fake->embedding);
}

// Verifies encoder can return empty embeddings from backend.
TEST(SpeakerEncoder, DelegatesEmptyEmbedding) {
    auto fake = std::make_shared<test_fakes::SpeakerEncoderBackend>();
    fake->embedding.clear();
    SpeakerEncoder encoder;
    encoder.SetBackendForTesting(fake);

    auto out = encoder.GetEmbedding({ 1.0f });
    EXPECT_TRUE(out.empty());
}

// Verifies repeated initialize calls update backend args.
TEST(SpeakerEncoder, RepeatedInitializeUpdatesArgs) {
    auto fake = std::make_shared<test_fakes::SpeakerEncoderBackend>();
    SpeakerEncoder encoder;
    encoder.SetBackendForTesting(fake);

    encoder.Initialize("a.xml", "CPU");
    encoder.Initialize("b.xml", "GPU");

    EXPECT_EQ(fake->initModel, "b.xml");
    EXPECT_EQ(fake->initDevice, "GPU");
}

// Verifies embedding input is captured exactly for boundary-size vectors.
TEST(SpeakerEncoder, EmbeddingCapturesSingleSampleInput) {
    auto fake = std::make_shared<test_fakes::SpeakerEncoderBackend>();
    SpeakerEncoder encoder;
    encoder.SetBackendForTesting(fake);

    std::vector<float> input{ 0.25f };
    (void)encoder.GetEmbedding(input);

    ASSERT_EQ(fake->lastInput.size(), 1u);
    EXPECT_NEAR(fake->lastInput[0], 0.25f, 1e-6f);
}
