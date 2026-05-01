#pragma once

// Verifies audio recorder delegates Start to injected backend.
TEST(AudioRecorder, DelegatesStartToBackend) {
    AudioTranscriptionBridge b;
    AudioRecorder recorder(&b);
    auto fake = std::make_shared<test_fakes::AudioRecorderBackend>();
    recorder.SetBackendForTesting(fake);

    recorder.Start();

    EXPECT_TRUE(fake->startCalled);
    EXPECT_EQ(fake->startCount, 1);
}

// Verifies audio recorder delegates Stop to injected backend.
TEST(AudioRecorder, DelegatesStopToBackend) {
    AudioTranscriptionBridge b;
    AudioRecorder recorder(&b);
    auto fake = std::make_shared<test_fakes::AudioRecorderBackend>();
    recorder.SetBackendForTesting(fake);

    recorder.Stop();

    EXPECT_TRUE(fake->stopCalled);
    EXPECT_EQ(fake->stopCount, 1);
}

// Verifies backend start can be called multiple times.
TEST(AudioRecorder, DelegatesStartMultipleTimes) {
    AudioTranscriptionBridge b;
    AudioRecorder recorder(&b);
    auto fake = std::make_shared<test_fakes::AudioRecorderBackend>();
    recorder.SetBackendForTesting(fake);

    recorder.Start();
    recorder.Start();

    EXPECT_TRUE(fake->startCalled);
    EXPECT_EQ(fake->startCount, 2);
}

// Verifies backend stop can be called safely without a prior start.
TEST(AudioRecorder, DelegatesStopWithoutStart) {
    AudioTranscriptionBridge b;
    AudioRecorder recorder(&b);
    auto fake = std::make_shared<test_fakes::AudioRecorderBackend>();
    recorder.SetBackendForTesting(fake);

    recorder.Stop();

    EXPECT_TRUE(fake->stopCalled);
    EXPECT_EQ(fake->stopCount, 1);
}

// Verifies start and stop both route through seam on same instance.
TEST(AudioRecorder, DelegatesStartThenStop) {
    AudioTranscriptionBridge b;
    AudioRecorder recorder(&b);
    auto fake = std::make_shared<test_fakes::AudioRecorderBackend>();
    recorder.SetBackendForTesting(fake);

    recorder.Start();
    recorder.Stop();

    EXPECT_EQ(fake->startCount, 1);
    EXPECT_EQ(fake->stopCount, 1);
}
