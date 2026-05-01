#include "pch.h"
#include "DoctorEmbedding.h"
#include "SpeakerEncoder.h"

#include <winrt/Windows.Storage.h>
#include <dpapi.h>
#include <fstream>
#include <thread>

#pragma comment(lib, "crypt32.lib")

void DoctorEmbedding::FinishEnrollmentEarly() { m_finishEarly = true; }
void DoctorEmbedding::CancelEnrollment() { m_cancel = true; }
void DoctorEmbedding::SetBackendForTesting(std::shared_ptr<IDoctorEmbeddingBackend> backend) { m_testBackend = std::move(backend); }

void DoctorEmbedding::SetStorageDirectory(const std::string& dir) { m_storageDirectoryOverride = dir; }


// mini audio callback (required for miniaudio)
void DoctorEmbedding::data_callback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount)
{
    EnrollmentContext* context = (EnrollmentContext*)pDevice->pUserData;
    if (!context->isRecording) return;

    const float* samples = (const float*)pInput;

    // append audio
    context->audioBuffer.insert(context->audioBuffer.end(), samples, samples + frameCount);

    // stop if we hit 30 seconds
    if (context->audioBuffer.size() >= context->maxSamples) {
        context->isRecording = false;
    }
}

// enroll the doctors voice, record and calculate embedding
winrt::Windows::Foundation::IAsyncAction DoctorEmbedding::EnrollNewSpeakerAsync(SpeakerEncoder* encoder) {
    m_speachEmbedding.clear();    
    co_await winrt::resume_background();

    m_finishEarly = false;
    m_cancel = false;

    if (m_testBackend) {
        auto captured = m_testBackend->CaptureEnrollmentAudio(m_finishEarly, m_cancel);
        if (!captured.empty() && !m_cancel) {
            std::vector<float> newProfile = encoder->GetEmbedding(captured);
            SaveToDisk(newProfile);
        }
        co_return;
    }

	EnrollmentContext context;
	context.audioBuffer.reserve(16000 * 30);
	context.isRecording = true;

	// configure miniaudio.h
	ma_device_config config = ma_device_config_init(ma_device_type_capture);
	config.capture.format = ma_format_f32;
	config.capture.channels = 1;
	config.sampleRate = 16000;
	config.dataCallback = data_callback;
	config.pUserData = &context;

	ma_device device;
	if (ma_device_init(NULL, &config, &device) != MA_SUCCESS) {
		OutputDebugString(L"Enrollment Error: Failed to init microphone.\n");
		co_return;
	}

	if (ma_device_start(&device) != MA_SUCCESS) {
		ma_device_uninit(&device);
		co_return;
	}

	int safetyTimeout = 0;
    // loop up to 35 seconds in 0.1s increments
	while (context.isRecording && safetyTimeout < 350) { 
		if (m_cancel) {
			ma_device_uninit(&device);
			co_return;
		}

		if (m_finishEarly) { break; }
		std::this_thread::sleep_for(std::chrono::milliseconds(100));
		safetyTimeout++;
	}

    // stop recording
	ma_device_uninit(&device); 

    // generate profile
	if (!context.audioBuffer.empty() && !m_cancel) {
		std::vector<float> newProfile = encoder->GetEmbedding(context.audioBuffer);
		SaveToDisk(newProfile);

	}
}

// load the doctors speach embedding
std::vector<float> DoctorEmbedding::getSpeachEmbedding()
{
    // check if in memory
    if (!m_speachEmbedding.empty()) {
        return m_speachEmbedding;
    }

    // load from disk if not in memory
    m_speachEmbedding = LoadFromDisk();
    return m_speachEmbedding;
}

// get the doctors embedding location from system files
std::string DoctorEmbedding::getFilePath() {
    if (!m_storageDirectoryOverride.empty()) {
        return m_storageDirectoryOverride + "\\doctor_voice.dat";
    }

    if (m_testBackend) {
        return m_testBackend->ResolveDefaultFilePath();
    }

    auto localFolder = winrt::Windows::Storage::ApplicationData::Current().LocalFolder().Path();
    return winrt::to_string(localFolder) + "\\doctor_voice.dat";
}

void DoctorEmbedding::SaveToDisk(const std::vector<float>& embedding) {
    // save into memory
    m_speachEmbedding = embedding; 

    if (embedding.empty()) return;

    if (m_testBackend) {
        (void)m_testBackend->SaveProfile(getFilePath(), embedding);
        return;
    }

    // prepare data to encrypt
    DATA_BLOB dataIn;
    dataIn.cbData = static_cast<DWORD>(embedding.size() * sizeof(float));
    dataIn.pbData = reinterpret_cast<BYTE*>(const_cast<float*>(embedding.data()));

    DATA_BLOB dataOut;

    // handle encryption
    if (CryptProtectData(&dataIn, L"DoctorVoiceProfile", NULL, NULL, NULL, CRYPTPROTECT_UI_FORBIDDEN, &dataOut)) {

        std::ofstream file(getFilePath(), std::ios::binary);
        if (file.is_open()) {
            file.write(reinterpret_cast<char*>(dataOut.pbData), dataOut.cbData);
            file.close();
        }
        LocalFree(dataOut.pbData);
    }
}

bool DoctorEmbedding::IsProfileEnrolled() {
    std::string path = DoctorEmbedding::getFilePath();

    // open the file and jump to the end 
    std::ifstream file(path, std::ios::binary | std::ios::ate);

    // check if it open and has a size greater than 0
    return file.is_open() && file.tellg() > 0;
}

// load doctor embedding from disk, returns an empty vector if file doesnt exist or empty
std::vector<float> DoctorEmbedding::LoadFromDisk() {
    if (m_testBackend) {
        return m_testBackend->LoadProfile(getFilePath());
    }

    std::ifstream file(getFilePath(), std::ios::binary | std::ios::ate);
    if (!file.is_open()) return {};

    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);

    std::vector<BYTE> encryptedBytes(size);
    if (!file.read(reinterpret_cast<char*>(encryptedBytes.data()), size)) return {};

    // prepare data to decrypt
    DATA_BLOB dataIn;
    dataIn.cbData = static_cast<DWORD>(encryptedBytes.size());
    dataIn.pbData = encryptedBytes.data();

    DATA_BLOB dataOut;

    std::vector<float> result;

    // decrypt
    if (CryptUnprotectData(&dataIn, NULL, NULL, NULL, NULL, 0, &dataOut)) {
        size_t floatCount = dataOut.cbData / sizeof(float);
        result.resize(floatCount);
        memcpy(result.data(), dataOut.pbData, dataOut.cbData);
        LocalFree(dataOut.pbData);
    }

    return result;
}
