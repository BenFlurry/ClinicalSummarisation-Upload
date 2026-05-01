#pragma once

// Verifies doctor embedding stores override directory.
TEST(DoctorEmbedding, SetStorageDirectoryAndProfileFlagPath) {
    DoctorEmbedding embedding;
    embedding.SetStorageDirectory("C:\\TmpVoice");
    EXPECT_FALSE(embedding.IsProfileEnrolled());
}

// Verifies doctor embedding delegates enrollment capture and save to injected backend.
TEST(DoctorEmbedding, DelegatesEnrollAndSaveToBackend) {
    auto fakeEmbedding = std::make_shared<test_fakes::DoctorEmbeddingBackend>();
    auto fakeEncoder = std::make_shared<test_fakes::SpeakerEncoderBackend>();
    fakeEncoder->embedding = { 0.9f, 0.1f };

    SpeakerEncoder encoder;
    encoder.SetBackendForTesting(fakeEncoder);

    DoctorEmbedding doctor;
    doctor.SetBackendForTesting(fakeEmbedding);

    auto action = doctor.EnrollNewSpeakerAsync(&encoder);
    action.get();

    EXPECT_EQ(fakeEmbedding->lastSavedPath, fakeEmbedding->defaultPath);
    ASSERT_EQ(fakeEmbedding->lastSavedProfile.size(), 2u);
    EXPECT_NEAR(fakeEmbedding->lastSavedProfile[0], 0.9f, 1e-6f);
}

// Verifies doctor embedding delegates load path through backend when memory cache is empty.
TEST(DoctorEmbedding, DelegatesLoadToBackend) {
    auto fakeEmbedding = std::make_shared<test_fakes::DoctorEmbeddingBackend>();
    DoctorEmbedding doctor;
    doctor.SetBackendForTesting(fakeEmbedding);

    auto profile = doctor.getSpeachEmbedding();

    ASSERT_EQ(profile.size(), 3u);
    EXPECT_NEAR(profile[2], 9.0f, 1e-6f);
}

// Verifies enrollment with empty captured audio does not save profile.
TEST(DoctorEmbedding, EmptyCapturedAudioSkipsSave) {
    auto fakeEmbedding = std::make_shared<test_fakes::DoctorEmbeddingBackend>();
    fakeEmbedding->capturedAudio.clear();
    auto fakeEncoder = std::make_shared<test_fakes::SpeakerEncoderBackend>();

    SpeakerEncoder encoder;
    encoder.SetBackendForTesting(fakeEncoder);

    DoctorEmbedding doctor;
    doctor.SetBackendForTesting(fakeEmbedding);

    auto action = doctor.EnrollNewSpeakerAsync(&encoder);
    action.get();

    EXPECT_TRUE(fakeEmbedding->lastSavedProfile.empty());
}

// Verifies loading from backend is cached after first fetch.
TEST(DoctorEmbedding, LoadFromBackendIsCached) {
    auto fakeEmbedding = std::make_shared<test_fakes::DoctorEmbeddingBackend>();
    fakeEmbedding->loadedProfile = { 1.0f, 2.0f };

    DoctorEmbedding doctor;
    doctor.SetBackendForTesting(fakeEmbedding);

    auto first = doctor.getSpeachEmbedding();
    fakeEmbedding->loadedProfile = { 9.0f, 9.0f };
    auto second = doctor.getSpeachEmbedding();

    EXPECT_EQ(first, second);
    EXPECT_NEAR(second[0], 1.0f, 1e-6f);
}

// Verifies backend default path is used when no storage override is set.
TEST(DoctorEmbedding, UsesBackendDefaultPathForSave) {
    auto fakeEmbedding = std::make_shared<test_fakes::DoctorEmbeddingBackend>();
    auto fakeEncoder = std::make_shared<test_fakes::SpeakerEncoderBackend>();

    SpeakerEncoder encoder;
    encoder.SetBackendForTesting(fakeEncoder);

    DoctorEmbedding doctor;
    doctor.SetBackendForTesting(fakeEmbedding);
    auto action = doctor.EnrollNewSpeakerAsync(&encoder);
    action.get();

    EXPECT_EQ(fakeEmbedding->lastSavedPath, fakeEmbedding->defaultPath);
}
