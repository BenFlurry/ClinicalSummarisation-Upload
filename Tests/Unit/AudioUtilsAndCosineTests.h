#pragma once

// Verifies scaling path.
TEST(AudioUtils, NormaliseScalesMidAmplitude) {
    std::vector<float> a{ 0.1f, -0.2f, 0.4f };
    AudioUtils::NormaliseAudio(a);
    EXPECT_NEAR(std::fabs(a[2]), 0.9f, 1e-4f);
}

// Verifies quiet threshold path.
TEST(AudioUtils, NormaliseSkipsQuietAudio) {
    std::vector<float> a{ 0.0001f };
    auto o = a;
    AudioUtils::NormaliseAudio(a);
    EXPECT_EQ(a, o);
}

// Verifies loud threshold path.
TEST(AudioUtils, NormaliseSkipsAlreadyLoudAudio) {
    std::vector<float> a{ 0.95f };
    auto o = a;
    AudioUtils::NormaliseAudio(a);
    EXPECT_EQ(a, o);
}

// Verifies lower boundary max_amp==0.001 is not scaled.
TEST(AudioUtils, NormaliseSkipsLowerBoundary) {
    std::vector<float> a{ 0.001f, -0.001f };
    auto o = a;
    AudioUtils::NormaliseAudio(a);
    EXPECT_EQ(a, o);
}

// Verifies upper boundary max_amp==0.9 is not scaled.
TEST(AudioUtils, NormaliseSkipsUpperBoundary) {
    std::vector<float> a{ 0.9f, -0.45f };
    auto o = a;
    AudioUtils::NormaliseAudio(a);
    EXPECT_EQ(a, o);
}

// Verifies normalization keeps sample signs intact.
TEST(AudioUtils, NormalisePreservesSigns) {
    std::vector<float> a{ -0.3f, 0.15f };
    AudioUtils::NormaliseAudio(a);
    EXPECT_LT(a[0], 0.0f);
    EXPECT_GT(a[1], 0.0f);
}

// Verifies empty audio buffers are handled safely.
TEST(AudioUtils, NormaliseHandlesEmptyBuffer) {
    std::vector<float> a;
    AudioUtils::NormaliseAudio(a);
    EXPECT_TRUE(a.empty());
}

// Verifies single sample normalization reaches target amplitude.
TEST(AudioUtils, NormaliseSingleSample) {
    std::vector<float> a{ 0.45f };
    AudioUtils::NormaliseAudio(a);
    EXPECT_NEAR(a[0], 0.9f, 1e-6f);
}

// Verifies normalization keeps zeros unchanged.
TEST(AudioUtils, NormaliseAllZerosNoChange) {
    std::vector<float> a{ 0.0f, 0.0f, 0.0f };
    AudioUtils::NormaliseAudio(a);
    EXPECT_FLOAT_EQ(a[0], 0.0f);
    EXPECT_FLOAT_EQ(a[1], 0.0f);
    EXPECT_FLOAT_EQ(a[2], 0.0f);
}

// Verifies cosine=1 for identical vectors.
TEST(CosineSimilarity, IdenticalVectors) {
    std::vector<float> v{ 1, 2, 3 };
    EXPECT_NEAR(SpeakerEncoder::CosineSimilarity(v, v), 1.0f, 1e-5f);
}

// Verifies orthogonal vectors.
TEST(CosineSimilarity, OrthogonalVectors) {
    std::vector<float> a{ 1, 0 };
    std::vector<float> b{ 0, 1 };
    EXPECT_NEAR(SpeakerEncoder::CosineSimilarity(a, b), 0.0f, 1e-5f);
}

// Verifies opposite vectors.
TEST(CosineSimilarity, OppositeVectors) {
    std::vector<float> a{ 1, 2, 3 };
    std::vector<float> b{ -1, -2, -3 };
    EXPECT_NEAR(SpeakerEncoder::CosineSimilarity(a, b), -1.0f, 1e-5f);
}

// Verifies mismatch branch.
TEST(CosineSimilarity, DifferentSizesReturnZero) {
    EXPECT_FLOAT_EQ(SpeakerEncoder::CosineSimilarity({ 1, 2 }, { 1, 2, 3 }), 0.0f);
}

// Verifies empty branch.
TEST(CosineSimilarity, EmptyVectorsReturnZero) {
    EXPECT_FLOAT_EQ(SpeakerEncoder::CosineSimilarity({}, {}), 0.0f);
}

// Verifies zero denominator branch.
TEST(CosineSimilarity, ZeroVectorReturnZero) {
    EXPECT_FLOAT_EQ(SpeakerEncoder::CosineSimilarity({ 0, 0, 0 }, { 1, 2, 3 }), 0.0f);
}

// Verifies scale invariance.
TEST(CosineSimilarity, ScaledDirection) {
    EXPECT_NEAR(SpeakerEncoder::CosineSimilarity({ 1, 2, 3 }, { 100, 200, 300 }), 1.0f, 1e-5f);
}

// Verifies cosine similarity is symmetric.
TEST(CosineSimilarity, SymmetricResult) {
    std::vector<float> a{ 2, -1, 0.5f };
    std::vector<float> b{ 4, 3, -2 };
    EXPECT_NEAR(SpeakerEncoder::CosineSimilarity(a, b), SpeakerEncoder::CosineSimilarity(b, a), 1e-6f);
}

// Verifies one-dimensional positive vectors return full similarity.
TEST(CosineSimilarity, OneDimensionalPositive) {
    EXPECT_NEAR(SpeakerEncoder::CosineSimilarity({ 5.0f }, { 9.0f }), 1.0f, 1e-6f);
}

// Verifies one-dimensional opposite vectors return negative similarity.
TEST(CosineSimilarity, OneDimensionalOpposite) {
    EXPECT_NEAR(SpeakerEncoder::CosineSimilarity({ 5.0f }, { -9.0f }), -1.0f, 1e-6f);
}

// Verifies mixed sign vectors produce bounded similarity.
TEST(CosineSimilarity, MixedSignsWithinBounds) {
    auto s = SpeakerEncoder::CosineSimilarity({ 1.0f, -3.0f, 2.0f }, { -4.0f, 6.0f, 1.0f });
    EXPECT_GE(s, -1.0f);
    EXPECT_LE(s, 1.0f);
}
