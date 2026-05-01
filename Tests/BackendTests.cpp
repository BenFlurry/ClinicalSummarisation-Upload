#include "pch.h"
#include "gtest/gtest.h"

#include "AudioTranscriptionBridge.h"
#include "AudioStructures.h"
#include "AudioUtils.h"
#include "SpeakerEncoder.h"
#include "Helpers.h"
#include "GuidelineRAG.h"
#include "AudioRecorder.h"
#include "DoctorEmbedding.h"
#include "TranscriptionEngine.h"
#include "SummarisationEngine.h"

#include <thread>
#include <vector>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>

#include "TestSupport/BackendTestFakes.h"

#include "Unit/AudioTranscriptionBridgeTests.h"
#include "Unit/AudioUtilsAndCosineTests.h"
#include "Unit/HelpersAndStructuresTests.h"
#include "Unit/SpeakerEncoderTests.h"
#include "Unit/AudioRecorderTests.h"
#include "Unit/DoctorEmbeddingTests.h"
#include "Unit/GuidelineRagTests.h"
#include "Unit/SummarisationEngineTests.h"
#include "Unit/TranscriptionEngineTests.h"

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

