#pragma once

// Verifies single chunk roundtrip.
TEST(AudioTranscriptionBridge, PushAndPopSingleChunk) {
    AudioTranscriptionBridge bridge;
    AudioChunk c;
    c.audioData = { 1, 2, 3 };
    c.isLastChunk = true;

    bridge.Push(c);
    auto r = bridge.Pop();

    ASSERT_EQ(r.audioData.size(), 3u);
    EXPECT_TRUE(r.isLastChunk);
}

// Verifies FIFO ordering.
TEST(AudioTranscriptionBridge, FIFOOrdering) {
    AudioTranscriptionBridge b;
    for (int i = 0; i < 5; ++i) {
        AudioChunk c;
        c.audioData = { float(i) };
        c.isLastChunk = (i == 4);
        b.Push(c);
    }
    for (int i = 0; i < 5; ++i) {
        auto r = b.Pop();
        EXPECT_FLOAT_EQ(r.audioData[0], float(i));
    }
}

// Verifies blocking pop.
TEST(AudioTranscriptionBridge, PopBlocksUntilPush) {
    AudioTranscriptionBridge b;
    bool popped = false;
    AudioChunk r;

    std::thread t([&] {
        r = b.Pop();
        popped = true;
        });

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_FALSE(popped);

    AudioChunk c;
    c.audioData = { 42 };
    b.Push(c);
    t.join();

    EXPECT_TRUE(popped);
}

// Verifies empty payload handling.
TEST(AudioTranscriptionBridge, EmptyAudioDataChunk) {
    AudioTranscriptionBridge b;
    AudioChunk c;
    c.isLastChunk = true;

    b.Push(c);
    auto r = b.Pop();

    EXPECT_TRUE(r.audioData.empty());
}

// Verifies false isLast flag is preserved through queueing.
TEST(AudioTranscriptionBridge, PreservesIsLastFalse) {
    AudioTranscriptionBridge b;
    AudioChunk c;
    c.audioData = { 9 };
    c.isLastChunk = false;

    b.Push(c);
    auto r = b.Pop();

    EXPECT_FALSE(r.isLastChunk);
}

// Verifies two-chunk order and final marker are preserved.
TEST(AudioTranscriptionBridge, PreservesOrderAndLastMarker) {
    AudioTranscriptionBridge b;

    AudioChunk first;
    first.audioData = { 1.0f };
    first.isLastChunk = false;

    AudioChunk last;
    last.audioData = { 2.0f };
    last.isLastChunk = true;

    b.Push(first);
    b.Push(last);

    auto r1 = b.Pop();
    auto r2 = b.Pop();

    EXPECT_FLOAT_EQ(r1.audioData[0], 1.0f);
    EXPECT_FALSE(r1.isLastChunk);
    EXPECT_FLOAT_EQ(r2.audioData[0], 2.0f);
    EXPECT_TRUE(r2.isLastChunk);
}

// Verifies pop can consume pre-buffered multiple items without blocking.
TEST(AudioTranscriptionBridge, PopsPreBufferedItems) {
    AudioTranscriptionBridge b;
    for (int i = 0; i < 3; ++i) {
        AudioChunk c;
        c.audioData = { static_cast<float>(10 + i) };
        c.isLastChunk = false;
        b.Push(c);
    }

    EXPECT_FLOAT_EQ(b.Pop().audioData[0], 10.0f);
    EXPECT_FLOAT_EQ(b.Pop().audioData[0], 11.0f);
    EXPECT_FLOAT_EQ(b.Pop().audioData[0], 12.0f);
}

// Verifies concurrent producers preserve total item count.
TEST(AudioTranscriptionBridge, ConcurrentProducersComplete) {
    AudioTranscriptionBridge b;
    constexpr int total = 50;

    std::thread p1([&] {
        for (int i = 0; i < total / 2; ++i) {
            AudioChunk c; c.audioData = { static_cast<float>(i) }; b.Push(c);
        }
    });
    std::thread p2([&] {
        for (int i = total / 2; i < total; ++i) {
            AudioChunk c; c.audioData = { static_cast<float>(i) }; b.Push(c);
        }
    });

    p1.join();
    p2.join();

    int seen = 0;
    for (int i = 0; i < total; ++i) {
        auto c = b.Pop();
        if (!c.audioData.empty()) ++seen;
    }

    EXPECT_EQ(seen, total);
}
