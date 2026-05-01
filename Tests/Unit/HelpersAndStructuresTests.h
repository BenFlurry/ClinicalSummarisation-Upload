#pragma once

// Verifies override path usage.
TEST(Helpers, GetModelPathWithOverride) {
    Helpers::SetBasePathOverride("C:\\TestModels");
    EXPECT_EQ(Helpers::GetModelPath("m"), std::filesystem::path("C:\\TestModels\\m"));
    Helpers::ClearBasePathOverride();
}

// Verifies slash normalization.
TEST(Helpers, GetModelPathSubfolder) {
    Helpers::SetBasePathOverride("C:\\Models");
    EXPECT_EQ(Helpers::GetModelPath("a/b"), std::filesystem::path("C:\\Models\\a\\b"));
    Helpers::ClearBasePathOverride();
}

// Verifies directory creation branch.
TEST(Helpers, GetGuidelinesDataPathCreatesFolder) {
    auto p = test_fakes::TempDir();
    Helpers::SetBasePathOverride(p);
    auto d = Helpers::GetGuidelinesDataPath();
    EXPECT_TRUE(std::filesystem::exists(d));
    Helpers::ClearBasePathOverride();
}

// Verifies clear override behavior.
TEST(Helpers, OverrideCanBeCleared) {
    Helpers::SetBasePathOverride("C:\\A");
    Helpers::ClearBasePathOverride();
    Helpers::SetBasePathOverride("C:\\B");
    EXPECT_EQ(Helpers::GetModelPath("x"), std::filesystem::path("C:\\B\\x"));
    Helpers::ClearBasePathOverride();
}

// Verifies same override returns same data folder path each call.
TEST(Helpers, GetGuidelinesDataPathStableAcrossCalls) {
    auto p = test_fakes::TempDir();
    Helpers::SetBasePathOverride(p);
    auto d1 = Helpers::GetGuidelinesDataPath();
    auto d2 = Helpers::GetGuidelinesDataPath();
    EXPECT_EQ(d1, d2);
    Helpers::ClearBasePathOverride();
}

// Verifies nested model path joining works.
TEST(Helpers, GetModelPathNestedSegments) {
    Helpers::SetBasePathOverride("C:\\Models");
    auto p = Helpers::GetModelPath("a/b/c/model.xml");
    EXPECT_EQ(p, std::filesystem::path("C:\\Models\\a\\b\\c\\model.xml"));
    Helpers::ClearBasePathOverride();
}

// Verifies guidelines data path points to guidelines_data suffix.
TEST(Helpers, GetGuidelinesDataPathSuffix) {
    auto p = test_fakes::TempDir();
    Helpers::SetBasePathOverride(p);
    auto d = Helpers::GetGuidelinesDataPath();
    EXPECT_EQ(d.filename(), std::filesystem::path("guidelines_data"));
    Helpers::ClearBasePathOverride();
}

// Verifies default isLastChunk value.
TEST(AudioChunk, DefaultIsNotLast) {
    AudioChunk c;
    EXPECT_FALSE(c.isLastChunk);
}

// Verifies default audio vector empty.
TEST(AudioChunk, DefaultAudioIsEmpty) {
    AudioChunk c;
    EXPECT_TRUE(c.audioData.empty());
}

// Verifies chunk audio is mutable as expected.
TEST(AudioChunk, AudioDataCanBeAssigned) {
    AudioChunk c;
    c.audioData = { 1.0f, 2.0f };
    ASSERT_EQ(c.audioData.size(), 2u);
    EXPECT_FLOAT_EQ(c.audioData[1], 2.0f);
}
